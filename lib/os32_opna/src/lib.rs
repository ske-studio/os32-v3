#![no_std]
#![deny(clippy::float_arithmetic)]

pub mod state;
pub mod tables;

#[cfg(not(test))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    extern "C" {
        fn synth_panic() -> !;
    }
    // The final module supplies the non-returning C diagnostic endpoint.
    unsafe { synth_panic() }
}
