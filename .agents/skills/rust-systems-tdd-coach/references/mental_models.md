# Mental Models & Rust Edition 2024 Systems Delta Guide

An architectural guide for senior systems architects, kernel engineers, and C/C++ veterans transitioning to modern Rust. This document bypasses introductory
programming concepts and maps Rust's memory management, type system, and concurrency primitives directly to C++20/23, ISO C23, Python 3.14 internals, LLVM IR,
and hardware execution models.

---

## 1. High-Level Systems Comparison Matrix

| Mechanic | Rust 1.99.1 & Edition 2024 | C++20/23 Mental Model | Python 3.14 Mental Model | Low-Level Systems & Hardware Reality |
| :--- | :--- | :--- | :--- | :--- |
| **Move Semantics** | Bitwise `memcpy` destructive move. Source statically dead in compiler CFG. | Non-destructive `std::move`. Source left in valid-but-unspecified state; destructor `~T()` still runs. | Name rebinding (`y = x`). Increments `ob_refcnt`. No byte moving. | Register copy or stack `memcpy(sizeof(T))`. Caller skips destructor invocation for source binding. |
| **Aliasing & Borrows** | Compile-time aliasing XOR mutability: `(&mut T)` XOR `(&T, &T, ...)`. | Unrestricted pointer aliasing. Requires manual `__restrict__` qualifiers. | Pervasive reference aliasing. All mutable bindings alias identical heap `PyObject`. | `&T` emits LLVM `readonly`. `&mut T` emits LLVM `noalias`, allowing CPU register caching across calls. |
| **Lifetimes & Regions** | Generic region parameters in compiler CFG. Erased before code generation. | Unchecked reference lifetimes. Dangling references produce undefined behavior. | Dynamic garbage collection (reference counting + cyclic garbage collector). | Zero runtime overhead, zero memory footprint. Statically validated scope lifetimes. |
| **Dynamic Dispatch** | Trait object (`&dyn Trait`, `Box<dyn Trait>`). Fat pointer (16 bytes). | Virtual table dispatch. Fat object (`_vptr` stored inside object instance). | Dynamic method resolution on `PyTypeObject` via dictionary/MRO lookup. | Trait object contains 8B data ptr + 8B vtable ptr. Object payload has zero layout overhead. |
| **Address Invariance** | `Pin<P>` wrapping pointer to address-sensitive or `!Unpin` type. | Address identity via `this`. Types disable moves via `= delete` on move constructor. | Immutable object memory address (`id(obj)` invariant throughout lifetime). | Guarantees pointee memory will not be relocated via `memcpy` or reclaimed before drop. |
| **Error Handling** | Algebraic enum `Result<T, E>` with `?` propagation. Zero heap allocation. | `std::expected<T, E>` in C++23. Legacy code relies on Itanium C++ ABI exceptions. | Runtime exceptions (`try...except`). Stack frame allocation and traceback unwinding. | Discriminant niche optimization allows register returns without heap overhead or DWARF tables. |

---

## 2. Destructive Moves vs. C++ Move Semantics vs. Python Rebinding

### 2.1 Mechanics of Destructive Moves

In C++20/23, moving an object is non-destructive: `std::move(src)` is an explicit cast to an rvalue reference `T&&`, invoking a user-defined or
compiler-generated move constructor `T(T&&)`. The moved-from object remains alive in the current scope, and its destructor `~T()` is guaranteed to execute when
exiting the scope. Consequently, C++ class authors must defensively leave the moved-from instance in a "valid but unspecified" state (e.g. setting internal
pointers to `nullptr`).

```text
C++20/23 Move Construction:
[Source Object (ptr=0xDEAD)] ----(Move Constructor)----> [Dest Object (ptr=0xDEAD)]
      |                                                        |
      +--> Sets source ptr = nullptr                           +--> ~T() executes at scope exit
      +--> ~T() STILL RUNS at scope exit! (checks if nullptr)

Rust Destructive Move (Bitwise memcpy):
[Source Binding (ptr=0xDEAD)] ---(Bitwise memcpy of T)---> [Dest Binding (ptr=0xDEAD)]
      |
      +--> Statically marked DEAD in compiler CFG.
      +--> NO Drop runs for source binding!
      +--> Only Dest Binding runs Drop at scope exit.
```

In Rust 1.99.1, moves are **destructive by default**:

1. **Bitwise Bit Copy**: The compiler emits a bitwise copy (`memcpy` of `size_of::<T>()` bytes) from the source memory slot to the destination memory slot.
2. **Static Invalidation**: The borrow checker statically marks the source identifier as uninitialized/dead in the control-flow graph (CFG). Any subsequent
   access to the source produces compile error `E0382: use of moved value`.
3. **Drop Elimination**: Because the source is statically dead, the compiler omits the drop glue for the source variable entirely. `Drop::drop` runs exactly
   once—when the new owner goes out of scope.
4. **No Custom Move Constructors**: Rust does not support custom move constructors or move assignment operators. Custom cleanup logic is strictly confined to
   the `Drop` trait.

### 2.2 Stack Drop Flags & Conditional Moves

When an ownership transfer occurs inside a dynamic branch (e.g. an `if` condition), the compiler cannot determine at compile time whether the source variable
was moved:

```rust
pub fn process_packet(condition: bool, packet: HardwarePacket) {
    if condition {
        consume_packet(packet); // Destructive move occurs here conditionally
    }
    // Did packet get dropped?
}
```

In this scenario, the Rust compiler allocates a hidden 1-byte **drop flag** on the local stack frame:

- The drop flag is initialized to `1` (active).
- Upon taking the conditional branch that executes `consume_packet(packet)`, the compiler inserts code to set the drop flag to `0`.
- At function epilogue, the compiler generates **drop glue** that inspects the drop flag: if the flag is still `1`, `Drop::drop(&mut packet)` is invoked; if
  `0`, drop execution is skipped.
- When moves are unconditional, drop flags are fully optimized away by LLVM dead-code elimination.

### 2.3 Contrast with Python Rebinding

Python does not feature moves. An assignment `y = x` merely binds a second name in the local namespace dictionary to the identical heap-allocated `PyObject*`,
incrementing its reference counter `ob_refcnt`. Destruction occurs unpredictably when `ob_refcnt` drops to zero or during cyclic garbage collection sweeps.

---

## 3. Borrow Checker, Lifetimes & Variance

### 3.1 Aliasing XOR Mutability (LLVM `noalias` Optimization)

Rust enforces the core systems invariant: **Aliasing XOR Mutability**. At any point in execution, a resource may have either:

- Any number of shared references (`&T`), OR
- Exactly one exclusive reference (`&mut T`).

This is not merely a memory safety guarantee—it is an aggressive optimization contract for the compiler backend:

- In C/C++, the compiler must conservatively assume that two pointers `int* a` and `int* b` may alias the same memory address, preventing register caching of
  `*a` across writes to `*b` unless annotated with `__restrict__`.
- In Rust, `&mut T` is lowered directly to LLVM IR with the `noalias` parameter attribute. This guarantees to LLVM that no other pointer within the function
  scope accesses the same memory, permitting aggressive loop vectorization, dead-store elimination, and register-allocated caching.
- `&T` references are decorated with LLVM `readonly` (and `noalias` when dereferenced without interior mutability), allowing the CPU memory controller to
  exploit read-only cache lines.

### 3.2 Lifetimes as Control-Flow Regions

Lifetimes are not runtime timestamps; they are compile-time generic region labels in the compiler's intermediate representation. The compiler constructs a
directed graph of borrow constraints and solves them via Non-Lexical Lifetimes (NLL). Once validated, lifetime annotations are completely erased: they occupy
zero bytes in binary memory and produce zero runtime instructions.

### 3.3 Formal Variance Rules

Variance describes how the subtyping relationship between lifetimes translates to compound types. In Rust, lifetime subtyping states that `'a: 'b` ("`'a`
outlives `'b`") implies `'a` is a subtype of `'b` (a reference valid for longer can be safely substituted where a shorter reference is expected).

```text
Lifetime Subtyping Hierarchy:
If 'a: 'b ('a outlives 'b):
  - &'a T       is COVARIANT over 'a and T       (&'a T can be passed where &'b T is required)
  - &'a mut T   is COVARIANT over 'a, but INVARIANT over T
  - fn(T) -> U  is CONTRAVARIANT over T, and COVARIANT over U
```

| Type Constructor | Variance over `'a` | Variance over `T` | Systems Consequence |
| :--- | :--- | :--- | :--- |
| `&'a T` | Covariant | Covariant | Subtype lifetimes can be freely shortened. A long-lived read-only buffer can satisfy a shorter consumer scope. |
| `&'a mut T` | Covariant | **Invariant** | **CRITICAL**: If `&mut T` were covariant over `T`, you could write a short-lived reference into a long-lived storage location, causing a use-after-free! |
| `fn(T) -> R` | Contravariant over `T` | Covariant over `R` | A function accepting general arguments can replace a function accepting specific arguments. |
| `UnsafeCell<T>` | N/A | **Invariant** | Interior mutability primitives disable compiler variance optimizations to prevent aliasing violations. |
| `*const T` | N/A | Covariant | Raw pointers mirror reference covariance without borrow checker validation. |
| `*mut T` | N/A | **Invariant** | Raw mutable pointers cannot be substituted covariantly because they permit arbitrary writes. |

#### Why `&mut T` Must Be Invariant Over `T`

Consider the systems disaster that would occur if `&mut T` were covariant over `T`:

```rust
// Hypothetical unsound Rust if &mut T were covariant:
fn overwrite_ref<'a>(target: &mut &'a str, temporary: &'a str) {
    *target = temporary;
}

fn trigger_use_after_free() {
    let mut static_str: &'static str = "immortal";
    {
        let local_string = String::from("ephemeral");
        // If &mut &'a str were covariant over &str, &'static str could be narrowed to &'local str:
        // overwrite_ref(&mut static_str, &local_string);
    }
    // static_str now points to reclaimed stack memory!
}
```

Because `&mut T` is invariant over `T`, Rust strictly forbids lifetime coercion of the underlying referent type `T`, preventing dangling pointers at compile
time.

---

## 4. Trait Objects (`dyn Trait`) vs. C++ Virtual Dispatch

### 4.1 Fat Pointer Memory Representation

In C++, dynamic dispatch relies on **fat objects**: every class instance with virtual functions embeds a virtual table pointer (`_vptr`, 8 bytes on 64-bit
architectures) at offset 0 of its struct layout.

In Rust, dynamic dispatch relies on **fat pointers** (`&dyn Trait` or `Box<dyn Trait>`):

- The struct itself remains a thin, zero-overhead layout containing only its data fields (zero extra bytes for vtables).
- The pointer to the trait object is 16 bytes: an 8-byte data pointer and an 8-byte vtable pointer.

```text
Rust Trait Object Fat Pointer (&dyn Device):
+------------------------------------+------------------------------------+
| Data Pointer (*const DeviceStruct) | Vtable Pointer (*const DeviceVtbl) |
| (8 bytes)                          | (8 bytes)                          |
+------------------------------------+------------------------------------+
                                                        |
                                                        v
                                      +-----------------------------------+
                                      | Drop glue function pointer        | (8 bytes)
                                      | Size of concrete struct (usize)   | (8 bytes)
                                      | Alignment of struct (usize)       | (8 bytes)
                                      | Method 1 function pointer         | (8 bytes)
                                      | Method 2 function pointer         | (8 bytes)
                                      +-----------------------------------+

C++ Virtual Class Instance:
+------------------------------------+
| _vptr -> *const CppVtbl (8 bytes)  |  <-- Stored INSIDE every object instance!
+------------------------------------+
| Member Field 1                     |
| Member Field 2                     |
+------------------------------------+
```

### 4.2 Static vs. Dynamic Dispatch Trade-offs

1. **Static Dispatch (Monomorphization)**:
   - Written as `fn process<T: Device>(dev: &T)` or `fn process(dev: &impl Device)`.
   - Exactly matches C++ templates: the compiler generates specialized machine code for each concrete type.
   - Enables aggressive compiler inlining, loop unrolling, and bounds-check elimination at the cost of binary size.
2. **Dynamic Dispatch (`dyn Trait`)**:
   - Written as `fn process(dev: &dyn Device)`.
   - Exactly one copy of the function is generated in machine code.
   - Incurs an indirect call through the vtable pointer (`call *%rax`), preventing inlining across the boundary.

### 4.3 Object Safety Rules

A trait can be instantiated as a trait object (`dyn Trait`) only if it is **object-safe**:

- All associated functions must have a `self` receiver (`&self`, `&mut self`, `self: Box<Self>`, etc.). Associated constructor functions like
  `fn new() -> Self` make a trait non-object-safe.
- Methods must not return `Self` by value, because the compiler cannot determine the concrete return size when calling through a dynamic vtable.
- Methods must not have generic type parameters, as that would require an infinite number of vtable entries for every possible instantiation.

---

## 5. `Pin<&mut T>` for Address-Sensitive Types

### 5.1 The Memory Relocation Problem

Because Rust moves are bitwise `memcpy` operations, moving a struct shifts its physical memory address. This poses a fatal hazard for:

1. **Self-Referential Structs**: Structs that store pointers pointing to their own fields (such as generated state machines in async futures). A bitwise
   `memcpy` moves the struct to a new address while the internal pointer still targets the old address.
2. **C++ Classes across FFI**: C++ objects maintain address identity (`this`). If a C++ class registers itself in a callback table or maintains internal
   cross-pointers, moving the Rust wrapper via `memcpy` corrupts the C++ instance.

```text
Before Move:
[ Address 0x1000: Struct A ]
  |-- field: [ 42 ]
  +-- self_ptr: 0x1000 ----+
         ^                 |
         +-----------------+

After Bitwise memcpy Move to Address 0x2000 (WITHOUT PINNING):
[ Address 0x2000: Struct A' ]
  |-- field: [ 42 ]
  +-- self_ptr: 0x1000 ----> [ Address 0x1000: INVALID / RECLAIMED STACK ] (DANGLING POINTER!)
```

### 5.2 The Pinning Invariant

`Pin<P>` is a wrapper around a pointer type `P` (such as `&mut T` or `Box<T>`) that enforces an invariant:
**The pointee `T` will never be moved to a new memory address, nor will its storage be deallocated or repurposed, until `Drop::drop` has completed.**

- **The `Unpin` Marker Trait**: Types that do not care about address stability automatically implement the compiler auto trait `Unpin` (almost all standard
  types: primitives, `Vec`, `String`). A `Pin<&mut T>` where `T: Unpin` can be freely dereferenced to `&mut T` via `Pin::get_mut`.
- **`!Unpin` Types**: Address-sensitive types explicitly opt out of `Unpin` using `PhantomPinned`. For `!Unpin` types, safe Rust code cannot obtain an `&mut T`
  without calling `unsafe Pin::get_unchecked_mut`, demanding an explicit `// SAFETY:` proof that the object will not be moved out of its pinned location.
- **Application in `cxx` Interoperability**: Non-trivially relocatable C++ classes are represented in Rust as `!Unpin`. Mutating methods across the FFI bridge
  strictly take `self: Pin<&mut CppClass>`, preventing Rust code from executing `std::mem::swap` on foreign C++ class instances.

---

## 6. Error Handling Architecture

### 6.1 `Result<T, E>` & Niche Optimization

In systems programming, exceptions (Itanium C++ ABI) are banned due to non-deterministic latency spikes during stack unwinding and substantial binary bloat from
DWARF unwind tables.

Rust utilizes algebraic enums `Result<T, E>` returned directly through CPU registers. Rust's compiler applies **discriminant niche optimizations**:

- An enum discriminant normally requires an integer tag byte.
- However, for types with invalid bit patterns (niches)—such as references `&T`, `NonNull<T>`, or `core::num::NonZeroU64` that cannot be zero (0x0)—the
  compiler reuses the zero bit pattern to represent the `Err` variant.
- `Result<&T, ()>` occupies exactly 8 bytes (the size of a single pointer). Zero runtime space overhead.

### 6.2 Error Propagation (`?` Operator)

The `?` operator is syntactic sugar for an explicit early return:

```rust
// Rust:
let frame = socket.read_frame()?;

// Expands to:
let frame = match socket.read_frame() {
    Ok(val) => val,
    Err(err) => return Err(From::from(err)),
};
```

This mirrors C++23 `std::expected<T, E>`, but provides seamless type conversion via the standard `From` trait and ergonomic early-exit syntax.

### 6.3 Domain vs. Application Errors (`thiserror` vs. `anyhow`)

1. **Domain / Library Errors (`thiserror`)**:
   - Used in low-level drivers, protocols, and library crates.
   - Strongly-typed, zero-allocation enums.
   - Implements `std::error::Error` and `Display` via declarative derive macros:

     ```rust
     #[derive(Debug, thiserror::Error)]
     pub enum DmaError {
         #[error("DMA allocation failed: requested {requested_bytes}B, available {available_bytes}B")]
         OutOfMemory { requested_bytes: usize, available_bytes: usize },
         #[error("Hardware buffer misalignment at physical address {0:#x}")]
         UnalignedAddress(usize),
     }
     ```

2. **Application / Orchestration Errors (`anyhow`)**:
   - Used at application boundaries and high-level test harnesses.
   - Dynamically boxed error container (`anyhow::Error` wrapping `Box<dyn Error>`).
   - Supports arbitrary context attachment: `file.read_to_end(&mut buf).context("failed reading telemetry log")?`.

---

## 7. Rust 1.99.1 & Edition 2024 Compiler Invariants

To maintain strict alignment with this repository's toolchain (`rustc 1.99.1`, `edition = "2024"`, `resolver = "3"`), all code must adhere to the following
syntactic and semantic invariants:

### 7.1 `unsafe extern "C"` Blocks

In Edition 2024, declaring foreign function interfaces requires explicit `unsafe` qualification on the extern block:

```rust
// CORRECT (Edition 2024):
unsafe extern "C" {
    pub fn mmap(
        addr: *mut libc::c_void,
        len: libc::size_t,
        prot: libc::c_int,
        flags: libc::c_int,
        fd: libc::c_int,
        offset: libc::off_t,
    ) -> *mut libc::c_void;

    pub fn munmap(addr: *mut libc::c_void, len: libc::size_t) -> libc::c_int;
}
```

### 7.2 `#[unsafe(no_mangle)]` Attributes

Exporting symbols to the foreign linker without name mangling is an unsafe operation that requires the `#[unsafe(...)]` attribute wrapper:

```rust
// CORRECT (Edition 2024):
#[unsafe(no_mangle)]
pub unsafe extern "C" fn packet_engine_status(engine: *const HardwareEngine) -> i32 {
    if engine.is_null() {
        return -1;
    }
    // SAFETY: Verified non-null above and caller contract guarantees valid engine pointer.
    unsafe { (*engine).status_flags }
}
```

### 7.3 `#![deny(unsafe_op_in_unsafe_fn)]`

Inside an `unsafe fn`, performing an unsafe operation now strictly requires an inner `unsafe {}` block and a detailed `// SAFETY:` rationale:

```rust
#![deny(unsafe_op_in_unsafe_fn)]

/// Reads a 64-bit hardware register.
///
/// # Safety
///
/// `reg_addr` must be 8-byte aligned and map to valid MMIO address space.
pub unsafe fn read_mmio_reg(reg_addr: *const u64) -> u64 {
    // Inside an unsafe function, calling an unsafe operation STILL requires unsafe {}!
    // SAFETY: Caller guarantees reg_addr is 8-byte aligned and within MMIO space.
    unsafe { core::ptr::read_volatile(reg_addr) }
}
```

### 7.4 Precise Capturing in `impl Trait` (`use<..>`)

Edition 2024 introduces the `use<..>` lifetime capture syntax for return-position opaque types. This resolves lifetime over-capturing, allowing callers to
drop or borrow parameters that are not captured by the returned iterator or future:

```rust
/// Returns an iterator borrowing strictly from `packet_buffer`.
/// The lifetime `'ctx` is intentionally omitted from `use<'buf>`, allowing `_ctx` to be dropped immediately.
pub fn parse_packets<'buf, 'ctx>(
    packet_buffer: &'buf [u8],
    _ctx: &'ctx ExecutionContext,
) -> impl Iterator<Item = &'buf [u8]> + use<'buf> {
    packet_buffer.chunks(64)
}
```

### 7.5 Standard Concurrency Primitives (`std::sync::LazyLock`)

Replace external dependencies (`lazy_static`, `once_cell`) with standard library `LazyLock`:

```rust
use std::sync::LazyLock;

pub static HARDWARE_TOPOLOGY: LazyLock<CpuTopology> = LazyLock::new(|| {
    CpuTopology::query_sysfs().unwrap_or_default()
});
```

### 7.6 Raw Address-of Syntax (`&raw const` and `&raw mut`)

In place of `core::ptr::addr_of!` and `addr_of_mut!`, Edition 2024 provides native language syntax to inspect the address of a field without creating an
intermediate reference (which would trigger undefined behavior if the field is unaligned or uninitialized):

```rust
#[repr(C, packed)]
pub struct PackedNetworkHeader {
    pub flags: u8,
    pub timestamp_ns: u64, // Unaligned!
}

pub fn get_timestamp_unaligned(header: &PackedNetworkHeader) -> u64 {
    // &header.timestamp_ns would be INSTANT UNDEFINED BEHAVIOR because it is unaligned!
    let raw_addr: *const u64 = &raw const header.timestamp_ns;
    // SAFETY: read_unaligned safely reads byte-by-byte into a CPU register.
    unsafe { raw_addr.read_unaligned() }
}
```

### 7.7 Native Async Closures (`async ||` and `AsyncFn`)

Rust Edition 2024 stabilizes native async closures (`async || { ... }` and `async move || { ... }`) alongside the `AsyncFn`, `AsyncFnMut`, and
`AsyncFnOnce` trait hierarchy in `core::ops`. This eliminates a longstanding limitation in prior editions where closures returning async blocks could
not borrow from closure arguments across `.await` points without resorting to heap-allocated `Box<dyn Future>` or impossible lifetime contortions.

#### The Argument-Borrowing Limitation of Legacy `|| async { ... }`

In prior editions, attempting to create an "async closure" required writing a synchronous closure that returned an `impl Future`:

```rust
// Legacy pattern (pre-Edition 2024):
fn process_packets_legacy<F>(packets: &[Packet], mut handler: F)
where
    F: FnMut(&Packet) -> Pin<Box<dyn Future<Output = ()> + '_>>, // Boxing and lifetime contortions required!
{
    // ...
}
```

The underlying mechanical constraint stems from the synchronous `Fn` trait signature: `fn call(&self, args: Args) -> Self::Output`.
Because `Output` is an associated type on `Fn`, its lifetime cannot be parameterized by the ephemeral lifetime of `args` passed into an individual invocation of
`call()`. If the returned async block attempts to borrow from a parameter across an `.await` point (e.g. `|pkt: &Packet| async { pkt.handle().await }`), the
compiler rejects the code with `lifetime may not live long enough` because the returned future cannot borrow from the parameter beyond the synchronous call
stack frame.

#### Native Async Closures in Edition 2024

Native async closures (`async |arg| { ... }`) are first-class compiler primitives whose returned futures are higher-ranked over the argument lifetimes. The
future can hold borrows of closure parameters across `.await` suspension points:

```rust
use core::ops::AsyncFnMut;

/// Higher-order async stream processor accepting a native async closure.
pub async fn dispatch_stream<F>(batch: &[Packet], mut callback: F)
where
    F: AsyncFnMut(&Packet) -> bool, // Native async closure trait bound!
{
    for packet in batch {
        // Native async closure: the future borrows `packet` across the .await suspension point!
        let continue_processing: bool = callback(packet).await;
        if !continue_processing {
            break;
        }
    }
}

pub async fn run_hardware_pipeline(packets: &[Packet]) {
    let mut processed_count: usize = 0;

    // The native async closure captures `processed_count` mutably from the environment
    // AND borrows ephemeral parameter `pkt` across .await points with zero heap allocation!
    let handler = async |pkt: &Packet| -> bool {
        pkt.dispatch_dma_async().await;
        processed_count += 1;
        true
    };

    dispatch_stream(packets, handler).await;
}
```

#### The `core::ops` Trait Hierarchy

Edition 2024 defines three dedicated traits in `core::ops` mirroring the synchronous closure hierarchy:

1. **`AsyncFnOnce<Args>`**: An async callable that can be invoked at most once, consuming its captured environment (`async_call_once`).
2. **`AsyncFnMut<Args>: AsyncFnOnce<Args>`**: An async callable that can be invoked repeatedly, mutably modifying its captured environment (`async_call_mut`).
3. **`AsyncFn<Args>: AsyncFnMut<Args>`**: An async callable that can be invoked concurrently from multiple threads, immutably borrowing its environment
   (`async_call`).

Standard synchronous functions and closures returning futures (`Fn(Args) -> impl Future`) implement `AsyncFn` automatically, ensuring backward compatibility.

#### Architectural Comparison: Systems Mental Models

| Dimension | Rust Edition 2024 (`async \|\|`) | C++20 Coroutine Lambda | Python 3.14 Async Callable |
| :--- | :--- | :--- | :--- |
| **Syntax** | `async \|arg: &T\| { ... }` | `[capture](const T& arg) -> Task<R> { ... }` | `async def handler(arg): ...` (no `async lambda`) |
| **Argument Lifetime Across Await** | **Statically Verified**: Borrow checker guarantees `arg` outlives the future's completion before caller drops it. | **Unchecked Dangling Hazard**: If lambda takes `const T&`, reference dangles if caller awaits after temporary is destroyed. | **Garbage Collected**: `PyObject*` refcount incremented; no dangling pointer hazard, but GC overhead. |
| **State Machine Allocation** | **Zero-Heap**: Unboxed, stack-allocated state machine of compile-time known `sizeof` (zero allocation). | **Heap Allocated**: Coroutine frame allocated via `operator new` unless elided by HALO/ELID compiler optimizations. | **Heap Allocated**: Every coroutine is a heap-allocated `PyGenObject` on the Python heap. |
| **Thread-Safety Proof** | Enforced at compile time via `Send` / `Sync` bounds on the closure and its returned future. | Developer-audited; data races and use-after-free across resumption points trigger undefined behavior. | GIL-dependent (free-threaded Python 3.14t requires explicit locks for shared mutable state). |
