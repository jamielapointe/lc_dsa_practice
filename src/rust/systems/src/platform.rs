//! Operating-system capability flags and architectural constants for the systems track.
//!
//! Mirrors `src/include/systems/platform.hpp` and `platform.h`.
//! Supports Linux (`x86_64`, `aarch64`) and macOS Apple Silicon (`arm64`).

/// Cache-line size used to separate data written by different cores or
/// processes (false-sharing avoidance) in shared layouts. Apple Silicon
/// uses a 128-byte coherence granule (`hw.cachelinesize`).
#[cfg(all(target_os = "macos", target_arch = "aarch64"))]
pub const CACHE_LINE_SIZE: usize = 128;

/// Cache-line size for Linux and other platforms (64 bytes).
#[cfg(not(all(target_os = "macos", target_arch = "aarch64")))]
pub const CACHE_LINE_SIZE: usize = 64;

/// Longest portable POSIX shared-memory object name, including the leading `/`.
/// macOS allows 31 characters; 30 leaves headroom and is accepted by Linux
/// (`NAME_MAX`) and QNX.
pub const MAX_SHM_NAME_LENGTH: usize = 30;

/// True if running on Linux.
pub const IS_LINUX: bool = cfg!(target_os = "linux");

/// True if running on macOS.
pub const IS_MACOS: bool = cfg!(target_os = "macos");

/// True if running on 64-bit ARM (`AArch64`).
pub const IS_AARCH64: bool = cfg!(target_arch = "aarch64");

/// True if running on 64-bit x86 (`x86_64`).
pub const IS_X86_64: bool = cfg!(target_arch = "x86_64");

/// True if `MAP_POPULATE` is available for `mmap`. Only supported on Linux.
pub const HAS_MAP_POPULATE: bool = IS_LINUX;

/// Queries the host OS virtual memory page size.
/// Defaults to 16384 on macOS Apple Silicon or 4096 on other platforms on failure.
#[must_use]
#[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
pub fn page_size() -> usize {
    // SAFETY: sysconf is a safe POSIX query.
    let size = unsafe { libc::sysconf(libc::_SC_PAGESIZE) };
    if size > 0 {
        size as usize
    } else if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        16384
    } else {
        4096
    }
}
