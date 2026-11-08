//! POSIX operating-system adapter for shared-memory segments.
//!
//! Enforces SHMPS-REQ-021: All OS calls (`shm_open`, `ftruncate`, `mmap`,
//! `munmap`, `mlock`, `shm_unlink`, `getpgid`, `proc_pidinfo`) are strictly isolated to this module.

#![allow(unsafe_code)]

use crate::platform::{self, MAX_SHM_NAME_LENGTH};
use crate::shm_pubsub::error::ShmPubSubError;
use core::sync::atomic::{AtomicU8, Ordering, compiler_fence};
use std::ffi::CString;
#[cfg(target_os = "linux")]
use std::fs::File;
#[cfg(target_os = "linux")]
use std::io::Read;
use std::time::Duration;

/// Validates a channel name per SHMPS-REQ-002:
/// Must start with `/`, contain no other `/`, have at least 1 character after `/`,
/// contain no null bytes, and length <= `MAX_SHM_NAME_LENGTH` (30).
///
/// # Errors
/// Returns `ShmPubSubError::InvalidArgument` if the format is malformed,
/// or `ShmPubSubError::NameTooLong` if length exceeds 30 bytes.
pub fn validate_name(name: &str) -> Result<(), ShmPubSubError> {
    if name.len() < 2 || !name.starts_with('/') || name[1..].contains('/') || name.contains('\0') {
        return Err(ShmPubSubError::InvalidArgument);
    }
    if name.len() > MAX_SHM_NAME_LENGTH {
        return Err(ShmPubSubError::NameTooLong);
    }
    Ok(())
}

/// Executes a syscall returning `c_int` with automatic retry if interrupted by `EINTR`.
fn retry_eintr<F: FnMut() -> libc::c_int>(mut f: F) -> libc::c_int {
    loop {
        let res = f();
        if res != -1 {
            return res;
        }
        let err = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
        if err != libc::EINTR {
            return res;
        }
    }
}

/// Helper RAII guard that closes an open file descriptor when dropped.
struct FdGuard(libc::c_int);

impl Drop for FdGuard {
    fn drop(&mut self) {
        if self.0 >= 0 {
            // SAFETY: self.0 is a valid, open file descriptor.
            unsafe {
                libc::close(self.0);
            }
        }
    }
}

/// Pre-faults newly allocated physical pages by writing dummy zero bytes (SHMPS-REQ-023).
fn prefault_by_writing(base: *mut u8, bytes: usize) {
    let step = platform::page_size();
    let mut offset = 0;
    while offset < bytes {
        // SAFETY: base is valid for bytes bytes; offset is within bounds.
        unsafe {
            let ptr = base.add(offset).cast::<AtomicU8>();
            (*ptr).store(0, Ordering::Relaxed);
        }
        offset += step;
    }
}

/// Pre-faults an existing mapping into this process's page table by reading (SHMPS-REQ-023).
fn prefault_by_reading(base: *mut u8, bytes: usize) {
    let step = platform::page_size();
    let mut offset = 0;
    let mut sink: u8 = 0;
    while offset < bytes {
        // SAFETY: base is valid for bytes bytes; offset is within bounds.
        unsafe {
            let ptr = base.add(offset).cast::<AtomicU8>();
            sink |= (*ptr).load(Ordering::Relaxed);
        }
        offset += step;
    }
    compiler_fence(Ordering::SeqCst);
    let _ = sink;
}

/// Locks pages into physical RAM via `mlock` with exponential retry on `EAGAIN` (SHMPS-REQ-024).
fn lock_pages(base: *mut u8, bytes: usize) -> bool {
    let mut delay = Duration::from_millis(1);
    for attempt in 0..=3 {
        // SAFETY: base and bytes define a mapped memory range.
        let ret = unsafe { libc::mlock(base.cast(), bytes) };
        if ret == 0 {
            return true;
        }
        let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
        if errno != libc::EAGAIN || attempt == 3 {
            return false;
        }
        std::thread::sleep(delay);
        delay *= 2;
    }
    false
}

/// RAII wrapper for a mapped POSIX shared-memory segment.
/// Unmaps memory on Drop without unlinking the name (SHMPS-REQ-019).
#[derive(Debug)]
pub struct SharedMemoryRegion {
    base: *mut u8,
    size: usize,
}

// SAFETY: Mapped shared memory region is safe to send across threads.
unsafe impl Send for SharedMemoryRegion {}
// SAFETY: Access to mapped shared memory is guarded by atomics and seqlocks.
unsafe impl Sync for SharedMemoryRegion {}

impl SharedMemoryRegion {
    /// Creates a new POSIX shared-memory segment, truncates it to `bytes`, maps it,
    /// faults every page in with writes, and wires pages via `mlock`.
    ///
    /// # Errors
    /// Returns `ShmPubSubError::InvalidArgument`, `ShmPubSubError::AlreadyExists`,
    /// `ShmPubSubError::MemoryLockFailed`, or `ShmPubSubError::OsError`.
    #[allow(clippy::cast_possible_wrap)]
    pub fn create(name: &str, bytes: usize, memory_lock: bool) -> Result<Self, ShmPubSubError> {
        validate_name(name)?;
        if bytes == 0 {
            return Err(ShmPubSubError::InvalidArgument);
        }

        let c_name = CString::new(name).map_err(|_| ShmPubSubError::InvalidArgument)?;

        // SAFETY: shm_open called with valid C string, standard POSIX flags and permissions.
        let fd = retry_eintr(|| unsafe {
            libc::shm_open(c_name.as_ptr(), libc::O_CREAT | libc::O_EXCL | libc::O_RDWR, 0o600)
        });

        if fd < 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            return Err(if errno == libc::EEXIST {
                ShmPubSubError::AlreadyExists
            } else {
                ShmPubSubError::OsError(errno)
            });
        }
        let _guard = FdGuard(fd);

        // From here on we own a new segment; any failure must unlink the name.
        let unlink_on_fail = || {
            // SAFETY: shm_unlink called with valid C string.
            unsafe {
                libc::shm_unlink(c_name.as_ptr());
            }
        };

        // SAFETY: ftruncate called with valid open fd.
        let trunc_res = retry_eintr(|| unsafe { libc::ftruncate(fd, bytes as libc::off_t) });
        if trunc_res != 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            unlink_on_fail();
            return Err(ShmPubSubError::OsError(errno));
        }

        // SAFETY: mmap called with valid fd, MAP_SHARED, PROT_READ|PROT_WRITE.
        let base = unsafe {
            libc::mmap(
                core::ptr::null_mut(),
                bytes,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                fd,
                0,
            )
        };

        if base == libc::MAP_FAILED {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            unlink_on_fail();
            return Err(ShmPubSubError::OsError(errno));
        }

        let byte_base = base.cast::<u8>();

        // Pre-fault all physical pages.
        prefault_by_writing(byte_base, bytes);

        // Lock pages into RAM if requested.
        if memory_lock && !lock_pages(byte_base, bytes) {
            // SAFETY: munmap called with mapped address and size.
            unsafe {
                libc::munmap(byte_base.cast(), bytes);
            }
            unlink_on_fail();
            return Err(ShmPubSubError::MemoryLockFailed);
        }

        Ok(Self {
            base: byte_base,
            size: bytes,
        })
    }

    /// Maps an existing POSIX shared-memory segment, queries its size, wires pages,
    /// and faults them into page tables via read touches. Never writes.
    ///
    /// # Errors
    /// Returns `ShmPubSubError::InvalidArgument`, `ShmPubSubError::NotFound`,
    /// `ShmPubSubError::LayoutMismatch`, `ShmPubSubError::MemoryLockFailed`, or `ShmPubSubError::OsError`.
    #[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
    pub fn open(name: &str, memory_lock: bool) -> Result<Self, ShmPubSubError> {
        validate_name(name)?;
        let c_name = CString::new(name).map_err(|_| ShmPubSubError::InvalidArgument)?;

        // SAFETY: shm_open called with valid C string.
        let fd = retry_eintr(|| unsafe { libc::shm_open(c_name.as_ptr(), libc::O_RDWR, 0) });
        if fd < 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            return Err(if errno == libc::ENOENT {
                ShmPubSubError::NotFound
            } else {
                ShmPubSubError::OsError(errno)
            });
        }
        let _guard = FdGuard(fd);

        // SAFETY: stat_buf is plain POD data, all-zeros is valid.
        let mut stat_buf: libc::stat = unsafe { core::mem::zeroed() };
        // SAFETY: fstat called with valid fd and mutable raw pointer.
        let fstat_res = unsafe { libc::fstat(fd, &raw mut stat_buf) };
        if fstat_res != 0 || stat_buf.st_size <= 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            return Err(if stat_buf.st_size <= 0 {
                ShmPubSubError::LayoutMismatch
            } else {
                ShmPubSubError::OsError(errno)
            });
        }

        let bytes = stat_buf.st_size as usize;
        #[cfg(target_os = "linux")]
        let populate_flag = libc::MAP_POPULATE;
        #[cfg(not(target_os = "linux"))]
        let populate_flag = 0;

        // SAFETY: mmap called with valid fd, MAP_SHARED, PROT_READ|PROT_WRITE.
        let base = unsafe {
            libc::mmap(
                core::ptr::null_mut(),
                bytes,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED | populate_flag,
                fd,
                0,
            )
        };

        if base == libc::MAP_FAILED {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            return Err(ShmPubSubError::OsError(errno));
        }

        let byte_base = base.cast::<u8>();

        if memory_lock && !lock_pages(byte_base, bytes) {
            // SAFETY: munmap called with mapped pointer and size.
            unsafe {
                libc::munmap(byte_base.cast(), bytes);
            }
            return Err(ShmPubSubError::MemoryLockFailed);
        }

        // Touch pages by reading to ensure PTEs are present without mutating data.
        prefault_by_reading(byte_base, bytes);

        Ok(Self {
            base: byte_base,
            size: bytes,
        })
    }

    /// Unlinks a named POSIX shared-memory object (SHMPS-REQ-006).
    ///
    /// # Errors
    /// Returns `ShmPubSubError::InvalidArgument`, `ShmPubSubError::NotFound`, or `ShmPubSubError::OsError`.
    pub fn unlink(name: &str) -> Result<(), ShmPubSubError> {
        validate_name(name)?;
        let c_name = CString::new(name).map_err(|_| ShmPubSubError::InvalidArgument)?;

        // SAFETY: shm_unlink called with valid C string.
        let ret = unsafe { libc::shm_unlink(c_name.as_ptr()) };
        if ret != 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            return Err(if errno == libc::ENOENT {
                ShmPubSubError::NotFound
            } else {
                ShmPubSubError::OsError(errno)
            });
        }
        Ok(())
    }

    /// Base address of the mapped region.
    #[must_use]
    pub const fn base(&self) -> *mut u8 {
        self.base
    }

    /// Size of the mapped region in bytes.
    #[must_use]
    pub const fn size(&self) -> usize {
        self.size
    }

    /// Checks if a process `pid` exists and is not a zombie (SHMPS-REQ-007, SHMPS-REQ-020).
    #[must_use]
    #[allow(clippy::cast_possible_truncation)]
    pub fn process_alive(pid: i64) -> bool {
        if pid <= 0 {
            return false;
        }

        // SAFETY: getpgid only queries process group identity; makes no mutations.
        let ret = unsafe { libc::getpgid(pid as libc::pid_t) };
        if ret == -1 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            if errno == libc::ESRCH {
                return false;
            }
        }
        !is_zombie(pid)
    }

    /// Returns the calling process's PID.
    #[must_use]
    pub fn current_process_id() -> i64 {
        // SAFETY: getpid is a safe POSIX query.
        unsafe { i64::from(libc::getpid()) }
    }
}

impl Drop for SharedMemoryRegion {
    fn drop(&mut self) {
        if !self.base.is_null() && self.size > 0 {
            // SAFETY: self.base was returned by mmap with self.size bytes.
            unsafe {
                libc::munmap(self.base.cast(), self.size);
            }
            self.base = core::ptr::null_mut();
            self.size = 0;
        }
    }
}

/// Inspects whether a process is an unreaped zombie (`SZOMB` on macOS, `'Z'`/`'X'` on Linux).
#[allow(clippy::cast_possible_truncation, clippy::cast_possible_wrap)]
fn is_zombie(pid: i64) -> bool {
    #[cfg(target_os = "macos")]
    {
        // SAFETY: proc_bsdshortinfo is plain POD data, all-zeros is valid.
        let mut info: libc::proc_bsdshortinfo = unsafe { core::mem::zeroed() };
        // SAFETY: proc_pidinfo called with valid raw pointer and size for proc_bsdshortinfo.
        let ret = unsafe {
            libc::proc_pidinfo(
                pid as libc::c_int,
                libc::PROC_PIDT_SHORTBSDINFO,
                0,
                (&raw mut info).cast(),
                core::mem::size_of::<libc::proc_bsdshortinfo>() as libc::c_int,
            )
        };
        if ret <= 0 {
            return false;
        }
        info.pbsi_status == libc::SZOMB
    }

    #[cfg(target_os = "linux")]
    {
        let path = format!("/proc/{pid}/stat");
        let mut file = match File::open(&path) {
            Ok(f) => f,
            Err(_) => return false,
        };
        let mut buf = [0u8; 512];
        let bytes_read = match file.read(&mut buf) {
            Ok(n) if n > 0 => n,
            _ => return false,
        };

        // Format is: "<pid> (<comm>) <state> ...". Look for the last ')' to skip any spaces/parens in comm.
        let slice = &buf[..bytes_read];
        if let Some(pos) = slice.iter().rposition(|&b| b == b')') {
            if pos + 2 < slice.len() {
                let state = slice[pos + 2];
                return state == b'Z' || state == b'X';
            }
        }
        false
    }

    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        let _ = pid;
        false
    }
}
