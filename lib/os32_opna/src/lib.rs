#![no_std]
#![deny(clippy::float_arithmetic)]

pub mod adpcm;
pub mod channel;
pub mod operator;
pub mod psg;
mod registers;
pub mod rhythm;
pub mod state;
pub mod tables;
pub mod timer;

#[cfg(not(test))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    extern "C" {
        fn synth_panic() -> !;
    }
    // The user-space sound library supplies the non-returning C diagnostic endpoint.
    unsafe { synth_panic() }
}

// C owns the exclusive, disjoint buffers described in opna.h.
use adpcm::Ram;
use core::{mem::size_of, slice};
use state::{AudioState, OpnaState, TimerState};
const INVALID: i32 = -9;

fn valid<T>(p: *mut T) -> bool {
    !p.is_null() && p.is_aligned()
}

#[no_mangle]
unsafe extern "C" fn opna_init(
    s: *mut OpnaState,
    bytes: u32,
    ram: *mut Ram,
    ram_bytes: u32,
) -> i32 {
    if !valid(s)
        || (bytes as usize) < size_of::<OpnaState>()
        || !valid(ram)
        || ram_bytes as usize != size_of::<Ram>()
    {
        return INVALID;
    }
    s.write_bytes(0, 1);
    let s = &mut *s;
    s.audio.adpcm.ram = ram.cast();
    s.audio.psg.init(0);
    adpcm::init(&mut s.audio);
    opna_reset(s)
}

#[no_mangle]
unsafe extern "C" fn opna_reset(s: *mut OpnaState) -> i32 {
    if !valid(s) {
        return INVALID;
    }
    let s = &mut *s;
    s.audio.reset(&mut *s.audio.adpcm.ram.cast::<Ram>());
    s.timer.reset();
    0
}

#[no_mangle]
unsafe extern "C" fn opna_write(s: *mut OpnaState, reg: u32, value: u32) -> i32 {
    if !valid(s) || reg > 511 || value > 255 {
        return INVALID;
    }
    let s = &mut *s;
    s.timer.write(reg as u16, value as u8);
    s.audio.write(
        reg as u16,
        value as u8,
        &mut *s.audio.adpcm.ram.cast::<Ram>(),
    );
    0
}

#[no_mangle]
unsafe extern "C" fn opna_read(s: *mut OpnaState, reg: u32, out: *mut u32) -> i32 {
    if !valid(s) || reg > 511 || !valid(out) {
        return INVALID;
    }
    let a = &mut (*s).audio;
    *out = match reg {
        0..=15 => a.psg.registers[reg as usize].into(),
        0x108 => adpcm::read(a, &mut *a.adpcm.ram.cast::<Ram>()).into(),
        0xff => 1,
        _ => 0,
    };
    0
}

#[no_mangle]
unsafe extern "C" fn opna_status(s: *mut OpnaState, bank: u32, out: *mut u32) -> i32 {
    if !valid(s) || bank > 1 || !valid(out) {
        return INVALID;
    }
    let s = &mut *s;
    *out = if bank == 0 {
        u32::from(s.timer.status & 3)
    } else {
        adpcm::status_ex(&mut s.audio, s.timer.status)
    };
    0
}

#[no_mangle]
unsafe extern "C" fn opna_rhythm(
    s: *mut OpnaState,
    ch: u32,
    pcm: *const u8,
    frames: u32,
    rate: u32,
) -> i32 {
    if !valid(s) || !rhythm::install(&mut (*s).audio, ch as usize, pcm, frames, rate) {
        return INVALID;
    }
    0
}

#[no_mangle]
unsafe extern "C" fn opna_next_event(s: *mut OpnaState, out: *mut u32) -> i32 {
    if !valid(s) || !valid(out) {
        return INVALID;
    }
    *out = (*s).timer.next_event();
    0
}

fn advance(t: &mut TimerState, mut us: u32, mask: u8, mut on_csm: impl FnMut()) {
    while us != 0 {
        let next = t.next_event();
        if next == u32::MAX {
            break;
        }
        let step = us.min(next).min(32767);
        let events = t.count(step as u16, mask);
        if t.csm_due(events) {
            on_csm();
        }
        us -= step;
    }
}

#[no_mangle]
unsafe extern "C" fn opna_count(s: *mut OpnaState, us: u32, irq: *mut u32) -> i32 {
    if !valid(s) || !valid(irq) {
        return INVALID;
    }
    let s = &mut *s;
    advance(&mut s.timer, us, s.audio.status_mask as u8, || {
        s.audio.apply_csm()
    });
    *irq = s.timer.irq_level(&s.audio).into();
    0
}

#[no_mangle]
unsafe extern "C" fn opna_timer_write(t: *mut TimerState, reg: u32, value: u32) -> i32 {
    if !valid(t) || reg > 511 || value > 255 {
        return INVALID;
    }
    if (*t).step == 0 {
        (*t).reset();
    }
    (*t).write(reg as u16, value as u8);
    0
}

#[no_mangle]
unsafe extern "C" fn opna_timer_count(t: *mut TimerState, us: u32, event: *mut u32) -> i32 {
    if !valid(t) || !valid(event) {
        return INVALID;
    }
    let mut due = false;
    advance(&mut *t, us, 255, || due = true);
    *event = due.into();
    0
}

#[no_mangle]
unsafe extern "C" fn opna_apply_csm(a: *mut AudioState) -> i32 {
    if !valid(a) {
        return INVALID;
    }
    (*a).apply_csm();
    0
}

#[no_mangle]
unsafe extern "C" fn opna_render(s: *mut OpnaState, out: *mut u8, frames: u32) -> i32 {
    if !valid(s) || frames > 256 || (frames != 0 && out.is_null()) {
        return INVALID;
    }
    if frames == 0 {
        return 0;
    }
    let s = &mut *s;
    let samples = &mut s.stereo_scratch[..2 * frames as usize];
    samples.fill(0);
    let a = &mut s.audio;
    a.fm_mix(samples);
    a.psg.mix(samples);
    adpcm::mix(a, &*a.adpcm.ram.cast::<Ram>(), samples);
    rhythm::mix(a, samples);
    let bytes = slice::from_raw_parts_mut(out, 4 * frames as usize);
    for (sample, dest) in samples.iter().zip(bytes.chunks_exact_mut(2)) {
        dest.copy_from_slice(&((*sample).clamp(-32768, 32767) as i16).to_le_bytes());
    }
    0
}
