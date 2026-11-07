//! Low-level, systems, and embedded-style components in idiomatic Rust.

pub mod bounded_fifo;

pub use bounded_fifo::{BoundedFifo, PopError, PopTimeoutError, PushError, PushTimeoutError};
