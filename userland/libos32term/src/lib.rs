#![no_std]
#![forbid(unsafe_code)]

//! OS-independent terminal display state with caller-owned fixed storage.
//! See the crate README for replacement, capacity and clipping contracts.

pub mod clip;
pub mod model;
pub mod stream;
pub mod utf8;
