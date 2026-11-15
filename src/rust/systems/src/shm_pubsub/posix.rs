//! POSIX operating-system adapter for shared-memory segments.
//!
//! Enforces SHMPS-REQ-021: All OS calls (`shm_open`, `ftruncate`, `mmap`,
//! `munmap`, `mlock`, `shm_unlink`, `getpgid`, `proc_pidinfo`) are strictly isolated to this module.

use crate::platform;
use crate::platform::MAX_SHM_NAME_LENGTH;
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
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        if self.0 >= 0 {
            // SAFETY:
            // 1. `self.0` is a valid non-negative file descriptor returned by `shm_open`.
            // 2. `FdGuard` owns the descriptor exclusively and closes it exactly once on drop.
            unsafe {
                libc::close(self.0);
            }
        }
    }
}

/// Pre-faults newly allocated physical pages by writing dummy zero bytes (SHMPS-REQ-023).
#[allow(unsafe_code)]
fn prefault_by_writing(base: *mut u8, bytes: usize) {
    let step = platform::page_size();
    let mut offset = 0;
    while offset < bytes {
        // SAFETY:
        // 1. `base` points to a newly mapped `MAP_SHARED` segment of `bytes` length and `offset < bytes`.
        // 2. `AtomicU8` has 1-byte alignment and size matching `u8`.
        // 3. The segment is not yet visible to readers (`magic` is 0), so relaxed atomic stores are race-free.
        unsafe {
            AtomicU8::from_ptr(base.add(offset)).store(0, Ordering::Relaxed);
        }
        offset += step;
    }
}

/// Pre-faults an existing mapping into this process's page table by reading (SHMPS-REQ-023).
#[allow(unsafe_code)]
fn prefault_by_reading(base: *mut u8, bytes: usize) {
    let step = platform::page_size();
    let mut offset = 0;
    let mut sink: u8 = 0;
    while offset < bytes {
        // SAFETY:
        // 1. `base` points to a valid `MAP_SHARED` mapping of `bytes` length and `offset < bytes`.
        // 2. `AtomicU8` has 1-byte alignment and size matching `u8`.
        // 3. Using `AtomicU8::load(Ordering::Relaxed)` faults the page table entry without mutating
        //    shared state or introducing data races against concurrent atomic writes.
        unsafe {
            sink |= AtomicU8::from_ptr(base.add(offset)).load(Ordering::Relaxed);
        }
        offset += step;
    }
    compiler_fence(Ordering::SeqCst);
    let _ = sink;
}

/// Locks pages into physical RAM via `mlock` with exponential retry on `EAGAIN` (SHMPS-REQ-024).
#[allow(unsafe_code)]
fn lock_pages(base: *mut u8, bytes: usize) -> bool {
    let mut delay = Duration::from_millis(1);
    for attempt in 0..=3 {
        // SAFETY:
        // 1. `base` is a page-aligned pointer returned by `mmap` for a valid region of `bytes` bytes.
        // 2. `mlock` pins virtual pages in physical RAM without modifying region contents.
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

// SAFETY:
// 1. `SharedMemoryRegion` owns a process-wide `MAP_SHARED` virtual memory mapping created by `mmap`.
// 2. POSIX `mmap` regions are valid across all threads within the process and can be unmapped from any thread.
#[allow(unsafe_code)]
unsafe impl Send for SharedMemoryRegion {}

// SAFETY:
// 1. `SharedMemoryRegion` exposes only its immutable `base` address and `size`.
// 2. All reads and writes to the underlying memory occur via atomics and seqlocks in `layout`.
#[allow(unsafe_code)]
unsafe impl Sync for SharedMemoryRegion {}

impl SharedMemoryRegion {
    /// Creates a new POSIX shared-memory segment, truncates it to `bytes`, maps it,
    /// faults every page in with writes, and wires pages via `mlock`.
    ///
    /// # Errors
    /// Returns `ShmPubSubError::InvalidArgument`, `ShmPubSubError::AlreadyExists`,
    /// `ShmPubSubError::MemoryLockFailed`, or `ShmPubSubError::OsError`.
    #[allow(unsafe_code, clippy::cast_possible_wrap)]
    pub fn create(name: &str, bytes: usize, memory_lock: bool) -> Result<Self, ShmPubSubError> {
        validate_name(name)?;
        if bytes == 0 {
            return Err(ShmPubSubError::InvalidArgument);
        }

        let c_name = CString::new(name).map_err(|_| ShmPubSubError::InvalidArgument)?;

        // SAFETY:
        // 1. `c_name` is a valid, NUL-terminated `CString` constructed from a validated SHM name.
        // 2. Standard POSIX `shm_open` flags and mode `0o600` satisfy the variadic `mode_t` argument.
        let fd = retry_eintr(|| unsafe {
            libc::shm_open(
                c_name.as_ptr(),
                libc::O_CREAT | libc::O_EXCL | libc::O_RDWR,
                0o600,
            )
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
            // SAFETY:
            // 1. `c_name` remains alive and valid until `unlink_on_fail` returns.
            // 2. Only this error path unlinks the newly created shared-memory object.
            unsafe {
                libc::shm_unlink(c_name.as_ptr());
            }
        };

        // SAFETY:
        // 1. `fd` is a valid writable file descriptor returned by `shm_open` above.
        // 2. `bytes > 0` is validated above and converted safely to `libc::off_t`.
        let trunc_res = retry_eintr(|| unsafe { libc::ftruncate(fd, bytes as libc::off_t) });
        if trunc_res != 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            unlink_on_fail();
            return Err(ShmPubSubError::OsError(errno));
        }

        // SAFETY:
        // 1. `fd` backs a shared-memory object already truncated to `bytes` bytes.
        // 2. `addr == NULL`, `PROT_READ | PROT_WRITE`, `MAP_SHARED`, and offset `0` are valid POSIX `mmap` arguments.
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
            // SAFETY:
            // 1. `byte_base` was just returned by `mmap` with length `bytes` (`!= MAP_FAILED`).
            // 2. No other reference to `byte_base` exists because `Self` has not yet been constructed.
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
    #[allow(unsafe_code, clippy::cast_possible_truncation, clippy::cast_sign_loss)]
    pub fn open(name: &str, memory_lock: bool) -> Result<Self, ShmPubSubError> {
        validate_name(name)?;
        let c_name = CString::new(name).map_err(|_| ShmPubSubError::InvalidArgument)?;

        // SAFETY:
        // 1. `c_name` is a valid, NUL-terminated `CString` constructed from a validated SHM name.
        // 2. `O_RDWR` without `O_CREAT` takes mode `0` and opens an existing segment without creating it.
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

        // SAFETY:
        // 1. `libc::stat` is a C ABI POD struct for which all-zero bit patterns are valid.
        // 2. All fields are overwritten immediately by `libc::fstat` before any read.
        let mut stat_buf: libc::stat = unsafe { core::mem::zeroed() };
        // SAFETY:
        // 1. `fd` is a valid open file descriptor owned by `_guard`.
        // 2. `&raw mut stat_buf` points to valid, properly aligned writable stack storage for `libc::stat`.
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

        // SAFETY:
        // 1. `fd` is an open shared-memory descriptor whose size was verified to be `bytes > 0`.
        // 2. `mmap` parameters (`NULL`, `PROT_READ | PROT_WRITE`, `MAP_SHARED | populate_flag`, offset `0`) are valid.
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
            // SAFETY:
            // 1. `byte_base` is a valid non-`MAP_FAILED` mapping of `bytes` bytes created above.
            // 2. No aliases exist because `Self` has not yet been constructed.
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
    #[allow(unsafe_code)]
    pub fn unlink(name: &str) -> Result<(), ShmPubSubError> {
        validate_name(name)?;
        let c_name = CString::new(name).map_err(|_| ShmPubSubError::InvalidArgument)?;

        // SAFETY:
        // 1. `c_name` is a valid, NUL-terminated `CString` representing a validated SHM object name.
        // 2. `shm_unlink` only removes the directory link and does not dereference process memory.
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
    #[allow(unsafe_code, clippy::cast_possible_truncation)]
    pub fn process_alive(pid: i64) -> bool {
        if pid <= 0 {
            return false;
        }

        // SAFETY:
        // 1. `pid > 0` is validated above before converting to `libc::pid_t`.
        // 2. `getpgid` is a read-only POSIX query that takes no pointers and performs no mutations.
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
    #[allow(unsafe_code)]
    pub fn current_process_id() -> i64 {
        // SAFETY:
        // 1. `getpid` takes no arguments and dereferences no pointers.
        // 2. The returned `libc::pid_t` widens losslessly into `i64`.
        unsafe { i64::from(libc::getpid()) }
    }
}

impl Drop for SharedMemoryRegion {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        if !self.base.is_null() && self.size > 0 {
            // SAFETY:
            // 1. `self.base` was returned by `mmap` with length `self.size` during `create` or `open`.
            // 2. `Drop::drop` runs only once after the final `Arc<SharedMemoryRegion>` strong reference
            //    is released, guaranteeing no outstanding `ChannelView` references exist.
            unsafe {
                libc::munmap(self.base.cast(), self.size);
            }
            self.base = core::ptr::null_mut();
            self.size = 0;
        }
    }
}

/// Inspects whether a process is an unreaped zombie (`SZOMB` on macOS, `'Z'`/`'X'` on Linux).
#[allow(
    unsafe_code,
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap
)]
fn is_zombie(pid: i64) -> bool {
    #[cfg(target_os = "macos")]
    {
        // SAFETY:
        // 1. `libc::proc_bsdshortinfo` is a plain C POD struct for which all-zero bit patterns are valid.
        // 2. `proc_pidinfo` initializes all read fields before `info.pbsi_status` is inspected.
        let mut info: libc::proc_bsdshortinfo = unsafe { core::mem::zeroed() };
        // SAFETY:
        // 1. `&raw mut info` points to valid, aligned writable storage of exact size `size_of::<proc_bsdshortinfo>()`.
        // 2. Passing the matching buffer size prevents any kernel write past `info`.
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
        let Ok(mut file) = File::open(&path) else {
            return false;
        };
        let mut buf = [0u8; 512];
        let Ok(bytes_read) = file.read(&mut buf) else {
            return false;
        };
        if bytes_read == 0 {
            return false;
        }

        // Format is: "<pid> (<comm>) <state> ...". Look for the last ')' to skip any spaces/parens in comm.
        let slice = &buf[..bytes_read];
        if let Some(pos) = slice.iter().rposition(|&b| b == b')')
            && pos + 2 < slice.len()
        {
            let state = slice[pos + 2];
            return state == b'Z' || state == b'X';
        }
        false
    }

    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        let _ = pid;
        false
    }
}
