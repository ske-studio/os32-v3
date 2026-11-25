//! OPNA ADPCM-B. The register dispatcher supplies the caller-owned RAM.
use crate::{
    state::AudioState,
    tables::{ADPCM_BASE, ADPCM_GAIN},
};
pub type Ram = [u8; 0x40000];

/// Called once on zeroed storage.
pub fn init(s: &mut AudioState) {
    s.adpcm.delta_n = 256;
    reset(s);
}

pub fn reset(s: &mut AudioState) {
    let a = &mut s.adpcm;
    a.limit = 0x3ffff;
    a.address = 0;
    a.playing = 0;
    a.cpu_stream = 0;
    a.sample = 0;
    a.step = 127;
    a.phase = 0;
    a.phase_delta = 256;
    reset_fifo(a);
    s.status_mask = !0x1c;
    s.status = 0;
    s.status_next = 0;
}

use crate::state::Adpcm;
fn reset_fifo(a: &mut Adpcm) {
    a.fifo_read = 0;
    a.fifo_write = 0;
    a.fifo_count = 0;
    a.fifo_current = 0;
    a.fifo_low = 0;
}

fn byte(a: &Adpcm, ram: &Ram) -> u8 {
    if a.control2 & 2 == 0 {
        return ram[(a.address as usize >> 4) & 0x3ffff];
    }
    let base = (a.address as usize >> 4) & 0x7fff;
    let bank = (a.address >> 1) & 7;
    let mut data = 0;
    for plane in 0..8 {
        data |= ((ram[base + plane * 0x8000] >> bank) & 1) << plane;
    }
    data
}

fn transfer(s: &mut AudioState, ram: &mut Ram, write: Option<u8>) -> u8 {
    let a = &mut s.adpcm;
    let mut result = 0;
    if let Some(data) = write {
        if a.control2 & 2 == 0 {
            ram[(a.address as usize >> 4) & 0x3ffff] = data;
        } else {
            let base = (a.address as usize >> 4) & 0x7fff;
            let bank = (a.address >> 1) & 7;
            for plane in 0..8 {
                let cell = &mut ram[base + plane * 0x8000];
                *cell = (*cell & !(1 << bank)) | (((data >> plane) & 1) << bank);
            }
        }
    } else {
        result = byte(a, ram);
    }
    a.address = a
        .address
        .wrapping_add(if a.control2 & 2 == 0 { 16 } else { 2 });
    if a.address == a.stop {
        s.status |= 4 & s.status_mask;
        s.status_next = 4;
        a.address &= 0x3fffff;
    }
    if a.address == a.limit {
        a.address = 0;
    }
    if write.is_some() || a.address < a.stop {
        s.status |= 8 & s.status_mask;
    }
    result
}

/// Two reads of pipeline latency at register 108h.
pub fn read(s: &mut AudioState, ram: &mut Ram) -> u8 {
    let value = s.adpcm.read_buffer as u8;
    s.adpcm.read_buffer >>= 8;
    if s.adpcm.control1 & 0x60 == 0x20 {
        s.adpcm.read_buffer |= u32::from(transfer(s, ram, None)) << 8;
    }
    value
}

pub fn status_ex(s: &mut AudioState, timer_status: u8) -> u32 {
    let a = &s.adpcm;
    let ready = if a.cpu_stream == 0 || a.fifo_count < 65536 - 16 {
        8
    } else {
        0
    };
    let value = ((s.status | u32::from(timer_status) | ready) & s.status_mask)
        | (u32::from(a.playing) << 5);
    s.status |= s.status_next;
    s.status_next = 0;
    value
}

/// Full banked register index.
pub fn write(s: &mut AudioState, ram: &mut Ram, reg: u16, data: u8) {
    let a = &mut s.adpcm;
    match reg {
        0x100 => {
            if data & 0x80 != 0 && a.playing == 0 {
                a.playing = 1;
                a.cpu_stream = u8::from(data & 0xe0 == 0x80);
                if a.cpu_stream != 0 {
                    reset_fifo(a);
                } else {
                    a.address = a.start;
                }
                a.sample = 0;
                a.step = 127;
                a.output = 0;
                a.previous_mix = 0;
                a.current_mix = 0;
                a.phase = 0;
            }
            if data & 1 != 0 {
                a.playing = 0;
                a.cpu_stream = 0;
                reset_fifo(a);
            }
            a.control1 = data;
        }
        0x101 => {
            a.control2 = data;
            a.granularity = if data & 2 != 0 { 1 } else { 4 };
        }
        0x102..=0x105 | 0x109..=0x10a | 0x10c..=0x10d => {
            let i = match reg {
                0x102..=0x105 => reg - 0x102,
                0x109..=0x10a => reg - 0x109 + 4,
                _ => reg - 0x10c + 6,
            } as usize;
            a.registers[i] = data;
            let pair = i & !1;
            let word = u32::from(u16::from_le_bytes([
                a.registers[pair],
                a.registers[pair + 1],
            ]));
            match pair {
                0 => {
                    a.start = word << 6;
                    a.address = a.start;
                }
                2 => a.stop = (word + 1) << 6,
                4 => {
                    a.delta_n = word.max(256);
                    a.phase_delta = ((a.delta_n * ADPCM_BASE[0]) >> 16) as i32;
                }
                _ => a.limit = (word + 1) << 6,
            }
        }
        0x108 if a.control1 & 0x60 == 0x60 => {
            transfer(s, ram, Some(data));
        }
        0x108 if a.control1 & 0xe0 == 0x80 && a.fifo_count < 65536 => {
            a.fifo[(a.fifo_write & 0xffff) as usize] = data;
            a.fifo_write = a.fifo_write.wrapping_add(1);
            a.fifo_count += 1;
        }
        0x10b => {
            a.level = i32::from(data);
            a.volume = (ADPCM_GAIN[0] * a.level) >> 12;
        }
        0x110 if data & 0x80 != 0 => s.status = 0,
        0x110 => s.status_mask = !u32::from(data & 31),
        _ => {}
    }
}

fn decode(a: &mut Adpcm, nibble: u8) -> i32 {
    let magnitude = (2 * i32::from(nibble & 7) + 1) * a.step / 8;
    a.sample = (a.sample
        + if nibble & 8 == 0 {
            magnitude
        } else {
            -magnitude
        })
    .clamp(-32768, 32767);
    const SCALE: [i32; 8] = [57, 57, 57, 57, 77, 102, 128, 153];
    a.step = (a.step * SCALE[(nibble & 7) as usize] / 64).clamp(127, 24576);
    a.sample
}

fn next_sample(s: &mut AudioState, ram: &Ram) -> i32 {
    let a = &mut s.adpcm;
    if a.cpu_stream != 0 {
        if a.fifo_low == 0 {
            if a.fifo_count == 0 {
                return a.sample;
            }
            a.fifo_current = a.fifo[(a.fifo_read & 0xffff) as usize];
            a.fifo_read = a.fifo_read.wrapping_add(1);
            a.fifo_count -= 1;
            a.fifo_low = 1;
            return decode(a, a.fifo_current >> 4);
        }
        a.fifo_low = 0;
        return decode(a, a.fifo_current & 15);
    }
    let (data, half) = if a.granularity == 0 {
        (ram[(a.address as usize >> 1) & 0x3ffff], 1)
    } else {
        (byte(a, ram), 1 << (a.granularity - 1))
    };
    a.address = a.address.wrapping_add(half);
    if a.address & half != 0 {
        return decode(a, data >> 4);
    }
    let result = decode(a, data & 15);
    if a.address == a.stop {
        if a.control1 & 0x10 != 0 {
            a.address = a.start;
            a.sample = 0;
            a.step = 127;
            return result;
        }
        a.address &= 0x3ffff;
        a.playing = 0;
        s.status |= 4 & s.status_mask;
    }
    if a.address == a.limit {
        a.address = 0;
    }
    result
}

fn advance(s: &mut AudioState, ram: &Ram) {
    let n = (next_sample(s, ram) * s.adpcm.volume) >> 13;
    let a = &mut s.adpcm;
    a.previous_mix = a.current_mix;
    a.current_mix = a.output + n;
    a.output = n;
}

pub fn mix(s: &mut AudioState, ram: &Ram, dest: &mut [i32]) {
    let delta = s.adpcm.phase_delta;
    let pan = if s.adpcm.muted == 0 {
        s.adpcm.control2
    } else {
        0
    };
    if s.adpcm.playing != 0 {
        for frame in dest.chunks_exact_mut(2) {
            let sample = if delta <= 8192 {
                if s.adpcm.playing != 0 && s.adpcm.phase < 0 {
                    s.adpcm.phase += 8192;
                    advance(s, ram);
                }
                let a = &mut s.adpcm;
                let n = if a.playing != 0 {
                    (a.phase * a.previous_mix + (8192 - a.phase) * a.current_mix) >> 13
                } else {
                    if a.previous_mix == 0 {
                        break;
                    }
                    if a.phase < 0 {
                        a.previous_mix = a.current_mix;
                        a.current_mix = 0;
                        a.phase += 8192;
                    }
                    (a.phase * a.current_mix) >> 13
                };
                a.phase -= delta;
                n
            } else {
                let tick = -(8192 * 8192) / delta;
                let mut sum = s.adpcm.previous_mix * (8192 + s.adpcm.phase);
                while s.adpcm.phase < 0 {
                    advance(s, ram);
                    if s.adpcm.playing == 0 {
                        break;
                    }
                    sum -= s.adpcm.previous_mix * s.adpcm.phase.max(tick);
                    s.adpcm.phase -= tick;
                }
                if s.adpcm.playing == 0 {
                    break;
                }
                s.adpcm.phase -= 8192;
                sum >> 13
            };
            if pan & 0x80 != 0 {
                frame[0] = frame[0].wrapping_add(sample);
            }
            if pan & 0x40 != 0 {
                frame[1] = frame[1].wrapping_add(sample);
            }
        }
    }
    if s.adpcm.playing == 0 {
        let a = &mut s.adpcm;
        a.previous_mix = 0;
        a.current_mix = 0;
        a.output = 0;
        a.phase = 0;
    }
}
