use crate::{state::AudioState, tables::TOTAL_LEVEL};

pub fn reset(s: &mut AudioState) {
    s.rhythm_key = 0;
}

/// Install a stopped voice. The caller keeps 2*frames readable bytes alive
/// until replacement, including across chip resets.
pub fn install(s: &mut AudioState, ch: usize, samples: *const u8, frames: u32, rate: u32) -> bool {
    if ch >= 6 || samples.is_null() || frames == 0 || frames > u32::MAX / 1024 || rate == 0 {
        return false;
    }
    let v = &mut s.rhythm[ch];
    v.samples_s16le = samples;
    v.frames = frames;
    v.source_rate = rate;
    v.step = rate.wrapping_mul(1024) / 44100;
    v.position = frames * 1024;
    s.rhythm_ready = u8::from(s.rhythm.iter().all(|v| !v.samples_s16le.is_null()));
    true
}

pub fn write(s: &mut AudioState, reg: u16, data: u8) {
    match reg {
        0x10 if data & 0x80 != 0 => s.rhythm_key &= !data,
        0x10 => {
            s.rhythm_key |= data & 63;
            for (i, v) in s.rhythm.iter_mut().enumerate() {
                if data & (1 << i) != 0 {
                    v.position = 0;
                }
            }
        }
        0x11 => s.rhythm_level = (!data & 63) as i8,
        0x18..=0x1d => {
            let v = &mut s.rhythm[(reg & 7) as usize];
            v.pan = data >> 6;
            v.level = (!data & 31) as i8;
        }
        _ => {}
    }
}

pub fn volume(db: i32) -> i32 {
    (-(i64::from(db.min(20)) * 2) / 3) as i32
}

/// # Safety
/// All installed PCM buffers must satisfy the lifetime and size contract of install.
pub unsafe fn mix(s: &mut AudioState, dest: &mut [i32]) {
    if s.rhythm_ready == 0 || s.rhythm_volume >= 128 {
        return;
    }
    for (i, v) in s.rhythm.iter_mut().enumerate() {
        if s.rhythm_key & (1 << i) == 0 {
            continue;
        }
        let db = (i64::from(s.rhythm_level)
            + i64::from(s.rhythm_volume)
            + i64::from(v.level)
            + i64::from(v.volume))
        .clamp(-31, 127);
        let gain = TOTAL_LEVEL[(32 + db) as usize] >> 4;
        let pan = if s.rhythm_mask & (1 << i) != 0 {
            0
        } else {
            v.pan
        };
        for frame in dest.chunks_exact_mut(2) {
            if v.position / 1024 >= v.frames {
                break;
            }
            let ptr = v.samples_s16le.add((v.position / 1024) as usize * 2);
            let pcm = i16::from_le_bytes([*ptr, *ptr.add(1)]);
            let sample = i32::from(pcm).wrapping_mul(gain) >> 12;
            v.position = v.position.wrapping_add(v.step);
            if pan & 2 != 0 {
                frame[0] = frame[0].wrapping_add(sample);
            }
            if pan & 1 != 0 {
                frame[1] = frame[1].wrapping_add(sample);
            }
        }
    }
}
