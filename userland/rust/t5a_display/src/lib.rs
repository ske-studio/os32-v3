#![no_std]
#![no_main]

mod boundary;
mod clipboard;
mod guest;
mod inject;
mod input;
mod launch;
mod paint;
mod prompt;
mod session;
mod sink;
mod state;
mod status;
mod storage;
mod view;

use os32api::KernelAPI;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    guest::run(api)
}
