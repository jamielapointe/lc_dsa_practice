# C++ and Rust Bidirectional Interoperability Architecture

An exhaustive architectural reference for veteran C and C++ systems engineers designing zero-overhead, memory-safe interoperability layers between modern C++23
and Rust 1.99.1 (Edition 2024).

---

## 1. Interoperability Toolchain Landscape

Systems architects must select the appropriate bridging mechanism based on interface granularity, language features, and safety guarantees:

| Toolchain / Pattern | Direction | Primary Use Case | Safety Invariants | Toolchain Integration |
| :--- | :--- | :--- | :--- | :--- |
| **`cxx` Crate** | Bidirectional (C++ <-> Rust) | Production systems bridges, subsystems (Chromium, Android, robotics). | **High**: Generates matching static assertions in C++ and Rust for layouts, sizes, and alignments. | `cxx_build` in `build.rs` or CMake integration. |
| **Raw C ABI (`unsafe extern "C"`)** | Bidirectional (C/POSIX <-> Rust) | Kernel interfaces, POSIX system calls, shared memory (`shm_open`), hardware MMIO. | **Manual**: Relies on developer-documented `// SAFETY:` proofs and `#[repr(C)]`. | Native `rustc` linker flags (`-l`, `build.rs`). |
| **`bindgen`** | C/C++ -> Rust | Ingesting large existing C headers (e.g. Linux UAPI, Vulkan, OpenSSL). | **Unsafe**: Generates raw pointer signatures; requires safe Rust wrapper layer. | Runs via `build.rs` or CLI using `libclang`. |
| **`cbindgen`** | Rust -> C/C++ | Exporting Rust libraries as shared (`.so`/`.dylib`) or static (`.a`) libraries to C/C++. | **ABI-Strict**: Emits C/C++ header files with exact types matching Rust `#[repr(C)]`. | CLI tool or `build.rs` integration. |
| **Google `crubit`** | Bidirectional (C++ <-> Rust) | Direct Clang AST integration for seamless zero-copy bindings without manual bridge IDL. | **High**: Full C++ type awareness (constructors, operators, template instantiations). | Google Bazel / LLVM toolchain integration. |

---

## 2. Safe Bidirectional Bridges with `cxx`

The `cxx` crate eliminates the fragility of manual FFI by constructing a bidirectional bridge verified by both the C++ and Rust compilers at build time.

```text
                  cxx Bridge Architecture:
+-------------------------------------------------------------------+
|                        #[cxx::bridge]                             |
|                                                                   |
|   unsafe extern "C++" {                extern "Rust" {            |
|       type CppEngine;                     type RustTelemetry;     |
|       fn step(Pin<&mut CppEngine>);       fn record_sample();     |
|   }                                    }                          |
+-------------------------------------------------------------------+
             |                                          |
             v                                          v
+-----------------------------+            +-----------------------------+
| C++ Compiler (Clang++ 23)   |            | Rust Compiler (rustc 1.99)  |
| - static_assert(sizeof)     |            | - static_assert(sizeof)     |
| - static_assert(alignof)    |            | - static_assert(alignof)    |
| - Inlines inline wrappers   |            | - Generates safe wrappers   |
+-----------------------------+            +-----------------------------+
```

### 2.1 The `#[cxx::bridge]` DSL

A single Rust module defines the entire foreign interface boundary:

```rust
// src/bridge.rs
#[cxx::bridge(namespace = "systems::interop")]
pub mod ffi {
    // Foreign C++ declarations imported into Rust (companion header: cxx_bridge/cxx_bridge.hpp):
    unsafe extern "C++" {
        include!("cxx_bridge.hpp");

        // Opaque C++ type: Rust knows pointer, but not internal byte layout
        type SensorController;

        // Factory function returning heap-allocated C++ object
        fn make_controller(sample_rate_hz: u32) -> UniquePtr<SensorController>;

        // Non-trivially relocatable C++ class: mutating methods MUST accept Pin<&mut T>!
        fn calibrate(self: Pin<&mut SensorController>, offset: f64) -> bool;

        // Read-only inspection method
        fn sample_rate(&self) -> u32;

        // Vector interop
        fn read_samples(self: Pin<&mut SensorController>) -> UniquePtr<CxxVector<f64>>;
    }

    // Shared POD struct: identical binary layout in both languages
    #[derive(Debug, Clone, Copy)]
    struct TelemetryFrame {
        timestamp_ns: u64,
        temperature_c: f32,
        voltage: f32,
        error_flags: u32,
    }

    // Exported Rust declarations exposed to C++:
    extern "Rust" {
        type TelemetryDispatcher;

        fn create_dispatcher() -> Box<TelemetryDispatcher>;
        fn dispatch_frame(self: &mut TelemetryDispatcher, frame: TelemetryFrame) -> bool;
    }
}
```

### 2.2 Memory Management with Smart Pointers

1. **`cxx::UniquePtr<T>`**:
   - Represents `std::unique_ptr<T>` in C++.
   - When instantiated in C++ and transferred to Rust, Rust holds a single machine pointer (8 bytes).
   - When the Rust `UniquePtr<T>` goes out of scope, its `Drop` implementation calls a C++ compiler-generated trampoline function that executes `delete ptr;`
     in the C++ runtime.
   - Rust cannot dereference `UniquePtr<T>` if `T` is an opaque type, guaranteeing encapsulation.
2. **`cxx::SharedPtr<T>`**:
   - Represents `std::shared_ptr<T>`.
   - Cross-language atomic reference counting (`std::shared_ptr` control block).
   - Cloning in Rust increments the C++ use count; dropping in Rust decrements the use count.
3. **`cxx::CxxVector<T>`**:
   - Represents `std::vector<T>`.
   - Rust can inspect the vector without copying via `.as_slice()`:

     ```rust
     let samples: UniquePtr<CxxVector<f64>> = controller.pin_mut().read_samples();
     let slice: &[f64] = samples.as_slice(); // Direct zero-copy slice borrow
     ```

### 2.3 Why Mutating Opaque C++ Types Requires `Pin<&mut T>`

In C++, non-POD classes may possess:

- Non-trivial move constructors.
- Internal pointers referencing member variables (`this->buf_ = &this->embedded_storage_[0]`).
- Registration of the `this` pointer in OS interrupt tables or callback registries.

Because Rust moves are bitwise `memcpy`, allowing Rust to receive an unpinned `&mut SensorController` would enable Rust code to move the underlying bytes (for
instance, calling `std::mem::swap`), invalidating the C++ object's address identity (`this`).

Therefore, `cxx` strictly mandates that any foreign C++ method taking a mutable receiver must be declared as:

```rust
fn calibrate(self: Pin<&mut SensorController>, offset: f64) -> bool;
```

This statically forbids Rust from performing relocation while permitting safe mutation of the pinned C++ object.

---

## 3. Raw C ABI & Layout Guarantees

For low-level POSIX systems interfaces, direct C ABI compatibility is achieved via `unsafe extern "C"` and `#[repr(C)]`.

### 3.1 `#[repr(C)]` Struct Invariants

By default, the Rust compiler reorders struct fields (`repr(Rust)`) to minimize padding and maximize cache density. When crossing the C ABI boundary,
`#[repr(C)]` enforces the platform's C struct layout rules (e.g. SysV ABI on x86_64, AAPCS64 on ARM64):

```rust
#[repr(C)]
pub struct DmaDescriptor {
    pub buffer_addr: u64, // Offset 0,  8 bytes
    pub length: u32,      // Offset 8,  4 bytes
    pub flags: u16,       // Offset 12, 2 bytes
    pub channel_id: u8,   // Offset 14, 1 byte
    // Compiler inserts 1 byte of padding at Offset 15 to align struct to 8 bytes!
}
```

### 3.2 Zero-Sized Types (ZST) across FFI

- In Rust, zero-sized types (`()`, `[u8; 0]`, empty structs `struct Marker;`) have `size_of::<T>() == 0` and `align_of::<T>() == 1`. They occupy zero bytes
  and pointer offsets do not advance.
- In ISO C++, an empty class `struct Empty {};` is required to have `sizeof(Empty) >= 1` so that distinct objects have distinct memory addresses.
- In ISO C, empty structs are invalid (though GCC/Clang support them as extensions with `sizeof == 0`).
- **Systems Invariant**: Never pass bare Rust ZSTs across FFI. If marker metadata is needed, use explicit fixed-width integers or opaque pointer handles.

### 3.3 Rust Edition 2024 C ABI Declarations

Rust Edition 2024 mandates explicit `unsafe` qualification on foreign function declarations and unmangled symbol exports:

```rust
// Declaring foreign C library functions:
unsafe extern "C" {
    pub fn shm_open(name: *const libc::c_char, oflag: libc::c_int, mode: libc::mode_t) -> libc::c_int;
    pub fn shm_unlink(name: *const libc::c_char) -> libc::c_int;
}

// Exporting a Rust function to C callers:
#[unsafe(no_mangle)]
pub unsafe extern "C" fn systems_fifo_init(capacity: usize) -> *mut FifoHandle {
    if capacity == 0 {
        return core::ptr::null_mut();
    }
    let fifo = Box::new(FifoHandle::new(capacity));
    Box::into_raw(fifo)
}
```

---

## 4. Automated Bindings & Toolchain Integration

### 4.1 `bindgen` (C/C++ to Rust)

`bindgen` parses C headers using `libclang` and emits equivalent Rust FFI bindings.

```rust
// build.rs (bindgen invocation pattern)
fn generate_posix_bindings() {
    let bindings = bindgen::Builder::default()
        .header("include/posix_ring_buffer.h")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .use_core()
        .derive_default(true)
        .derive_eq(true)
        .generate()
        .expect("Unable to generate bindings");

    let out_path = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("posix_bindings.rs")).unwrap();
}
```

**Key Bindgen Options for Systems Code**:

- `.use_core()`: Emits `#![no_std]` compatible bindings utilizing `core::ffi::c_void`.
- `.ctypes_prefix("libc")`: Maps standard C types to `libc::*`.
- `.blocklist_type(...)`: Excludes conflicting standard library system definitions.

### 4.2 `cbindgen` (Rust to C/C++)

`cbindgen` reads Rust source code and emits ANSI C or C++20/23 header files:

```toml
# cbindgen.toml
language = "C++"
namespace = "systems::ffi"
pragma_once = true
include_guard = "SYSTEMS_FFI_HPP"
cpp_compat = true
tab_width = 4
```

### 4.3 Google `crubit` Architectural Patterns

In large C++/Rust monorepos (such as Google Chromium and Android), manual IDLs (`#[cxx::bridge]`) introduce maintenance overhead as APIs evolve. Google
developed **`crubit`** to provide automatic, direct C++/Rust interop:

- **Direct Clang AST Translation**: Parses C++ types directly from Clang's internal AST representation rather than relying on an intermediate IDL.
- **Support for Complex C++ Types**: Generates safe Rust projections for non-trivial C++ types, including user-defined copy constructors, assignment
  operators, and templated structs.
- **Zero-Copy Calling Conventions**: Translates between SysV/AAPCS ABI and Rust's ABI without intermediate glue trampolines where compatible.
- **Conceptual Comparison**:
  - `cxx` is optimal for small, deliberate, hand-crafted systems bridges where explicit type boundary documentation is desired.
  - `crubit` is optimal for large-scale enterprise codebases where thousands of C++ APIs must be exposed to Rust transparently.

---

## 5. Cross-Language Safety Invariants

### 5.1 Panic and Exception Boundary Safety

Stack unwinding across the language boundary is undefined behavior:

- If a C++ exception unwinds into a Rust stack frame without an exception personality handler, memory corruption and process termination ensue.
- If a Rust panic unwinds into a C++ stack frame, the C++ runtime cannot execute `noexcept` checks or RAII destructors properly.

```text
Cross-Language Panic/Exception Boundary:
[ C++ Stack Frame ]  ====( C++ Exception Throw )====> [ FFI Boundary ] ====> [ Rust Stack Frame ]  -> UNDEFINED BEHAVIOR!
[ Rust Stack Frame ] ====( Rust Panic Unwinding )===> [ FFI Boundary ] ====> [ C++ Stack Frame ]   -> UNDEFINED BEHAVIOR!

Systems Solution (Zero-Unwind Invariant):
- Compile Rust with: `panic = "abort"`
- Compile C++ with:  `-fno-exceptions`
- Return errors via explicit status codes / Result enums across the boundary.
```

#### Edition 2024 Boundary Abort Guarantees

In Rust Edition 2024:

1. **`extern "C"`**: If a Rust function declared as `extern "C"` panics, the Rust runtime guarantees an immediate `std::process::abort()`. Unwinding across
   `extern "C"` is statically prevented from penetrating foreign frames.
2. **`extern "C-unwind"`**: If cross-language stack unwinding is explicitly required (e.g. interfacing with a foreign C++ runtime that catches exceptions),
   the function must be annotated with `extern "C-unwind"`. Both languages must use compatible DWARF unwinders.
3. **Repository Standard**: In this systems track and crash course, **all unwinding is forbidden**. Code is compiled with `panic = "abort"` and
   `-fno-exceptions`. Errors cross boundaries exclusively via integer codes or `Result`/`std::expected` data structs.

### 5.2 Ownership Transfer Without Leaks or Double-Frees

**Allocator Segregation Invariant**: Memory allocated by Rust's allocator (jemalloc or `std::alloc::System`) MUST be deallocated by Rust. Memory allocated by
C++ (`malloc` or `operator new`) MUST be deallocated by C++. Cross-deallocation corrupts heap metadata buckets.

#### Rust Transferring Ownership to C++

```rust
// Rust exports creation and destruction handles:
#[unsafe(no_mangle)]
pub unsafe extern "C" fn buffer_create(size: usize) -> *mut BufferHandle {
    let buf = Box::new(BufferHandle::new(size));
    // Box::into_raw relinquishes ownership without running Drop:
    Box::into_raw(buf)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn buffer_destroy(handle: *mut BufferHandle) {
    if !handle.is_null() {
        // SAFETY: The handle originated from Box::into_raw in buffer_create.
        // Box::from_raw retakes ownership, and drop() executes the destructor:
        drop(unsafe { Box::from_raw(handle) });
    }
}
```

In C++, wrap the raw pointer immediately in an RAII deleter:

```cpp
// C++ companion wrapper:
struct BufferDeleter {
    void operator()(BufferHandle* ptr) const {
        if (ptr != nullptr) {
            buffer_destroy(ptr);
        }
    }
};

using ScopedBuffer = std::unique_ptr<BufferHandle, BufferDeleter>;
```

### 5.3 Lifetimes across FFI via `PhantomData`

When passing a C-owned memory buffer into Rust as a raw pointer `*const u8` and `len: usize`, the pointer carries no lifetime information. Exposing an unbounded
lifetime (`&'a [u8]`) invites use-after-free bugs if the caller infers `'static`.

To enforce safe lifetime propagation, bind the raw pointer inside a Rust wrapper containing `PhantomData`:

```rust
use core::marker::PhantomData;

pub struct BorrowedPacketView<'arena> {
    ptr: *const u8,
    len: usize,
    _marker: PhantomData<&'arena [u8]>,
}

impl<'arena> BorrowedPacketView<'arena> {
    /// # Safety
    ///
    /// Caller guarantees `ptr` points to at least `len` valid initialized bytes
    /// that remain immutable for the duration of `'arena`.
    pub unsafe fn from_raw_parts(ptr: *const u8, len: usize) -> Self {
        Self {
            ptr,
            len,
            _marker: PhantomData,
        }
    }

    #[must_use]
    pub fn as_slice(&self) -> &'arena [u8] {
        // SAFETY: Enforced by constructor contract and tied to 'arena lifetime.
        unsafe { core::slice::from_raw_parts(self.ptr, self.len) }
    }
}
```

---

## 6. Hermetic `build.rs` Compilation Architecture

FFI micro-projects require hermetic build script configuration using `cxx_build` and `cc` to compile modern C++23 translation units.

```rust
// build.rs
fn main() {
    // 1. Build the cxx bridge stub and companion C++23 source:
    cxx_build::bridge("src/bridge.rs")
        .file("cxx_bridge/cxx_bridge.cpp")
        .include("cxx_bridge")
        .std("c++23")
        .flag_if_supported("-Wall")
        .flag_if_supported("-Wextra")
        .flag_if_supported("-Wpedantic")
        .flag_if_supported("-fno-exceptions")
        .flag_if_supported("-fno-rtti")
        .compile("cxx_bridge_interop");

    // 2. Instruct Cargo to re-run build.rs ONLY when source headers/bridges change:
    println!("cargo:rerun-if-changed=src/bridge.rs");
    println!("cargo:rerun-if-changed=cxx_bridge/cxx_bridge.cpp");
    println!("cargo:rerun-if-changed=cxx_bridge/cxx_bridge.hpp");
}
```

### 6.1 Toolchain Flags for Systems Safety

- **`-std=c++23`**: Enforces modern ISO C++23 compilation with Clang 23.
- **`-fno-exceptions`**: Disables C++ exception generation and unwind tables, matching Rust's `panic = "abort"` profile.
- **`-fno-rtti`**: Eliminates run-time type identification tables, enforcing zero-cost static polymorphism.
- **`-Wall -Wextra -Wpedantic`**: Matches the strict warnings policy of the repository's CMake toolchains.
