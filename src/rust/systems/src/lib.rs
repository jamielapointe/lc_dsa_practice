#![allow(unsafe_code)]

//! Low-level, systems, and embedded-style components in idiomatic Rust.

pub mod bounded_fifo;
pub mod platform;
pub mod shm_pubsub;

pub use bounded_fifo::{BoundedFifo, PopError, PopTimeoutError, PushError, PushTimeoutError};
pub use shm_pubsub::{
    ShmChannel, ShmConfig, ShmConsumer, ShmProducer, ShmPubSubConfig, ShmPubSubError, ShmPublisher, ShmReadMode,
    ShmSample, ShmSubscriber,
};
