//! Integer port of fmgen's OPNA (type N) operator. Rates are in fmgen units.
use crate::{state::Operator, tables::*};

const ATTACKING: u32 = 1;
const DECAYING: u32 = 2;
const SUSTAIN: u32 = 3;
const RELEASE: u32 = 4;
pub const OFF: u32 = 5;
const BOTTOM: i32 = 955;

/// The prescaler register selects one set of baked phase and envelope rates.
#[derive(Clone, Copy)]
pub struct Clock {
    ratio: i32,
    multiples: &'static [u32; 64],
}
impl Clock {
    pub fn new(prescaler: u8) -> Self {
        let (ratio, multiples) = match prescaler {
            0 => (RATIO_P0[0], &MULTIPLE_P0),
            1 => (RATIO_P1[0], &MULTIPLE_P1),
            2 => (RATIO_P2[0], &MULTIPLE_P2),
            _ => unreachable!(),
        };
        Self {
            ratio: ratio as i32,
            multiples,
        }
    }
}

/// Channel PM displacement and chip AM table index for one sample.
#[derive(Clone, Copy)]
pub struct Lfo {
    pub pm: i32,
    pub am: u8,
}

impl Operator {
    pub fn reset(&mut self, clock: Clock) {
        self.total_level = 127;
        self.total_level_latch = 127;
        self.shift_phase(OFF, clock);
        self.eg_count = 0;
        self.eg_curve_count = 0;
        self.ssg_phase = 0;
        self.phase = 0;
        self.reset_feedback();
        self.dirty = 1;
    }
    pub fn reset_feedback(&mut self) {
        self.out = 0;
        self.previous_out = 0;
    }
    pub fn set_dp_bn(&mut self, delta: u32, note: u32) {
        self.delta = delta;
        self.block_note = note;
        self.dirty = 1;
    }
    pub fn set_fnum(&mut self, f: u32) {
        self.set_dp_bn(
            (f & 2047) << ((f >> 11) & 7),
            NOTE[((f >> 7) & 127) as usize] as u32,
        );
    }
    pub fn set_tl(&mut self, tl: u32, csm: bool) {
        self.total_level_latch = tl;
        if !csm {
            self.total_level = tl;
            self.dirty = 1;
        }
    }
    pub fn set_ssg(&mut self, mode: u32) {
        self.ssg_type = if mode & 8 != 0 { mode } else { 0 };
    }
    pub fn key(&mut self, on: bool, clock: Clock) {
        if on == (self.key_on != 0) {
            return;
        }
        self.key_on = on as u8;
        if !on {
            self.shift_phase(RELEASE, clock);
        } else if matches!(self.eg_phase, OFF | RELEASE) {
            self.ssg_phase = -1;
            self.shift_phase(ATTACKING, clock);
            self.update_output();
            self.feedback_input = 0;
            self.reset_feedback();
            self.phase = 0;
        }
    }
    fn ssg_shape(&mut self) {
        let threshold = if matches!(self.ssg_type, 8 | 12) {
            56
        } else {
            60
        };
        // Enabling SSG during a plain note leaves the key-on sentinel (-1).
        // fmgen asserts here. Use the initial shape until the next attack.
        let phase = self.ssg_phase.max(0) as usize;
        let index = (((self.ssg_type as usize & 7) * 2 + usize::from(self.attack >= threshold))
            * 3
            + phase)
            * 2;
        self.ssg_offset = SSG_ENVELOPE[index] * 512;
        self.ssg_vector = SSG_ENVELOPE[index + 1];
    }
    fn rate(&mut self, rate: u32, clock: Clock) {
        self.eg_rate = rate as i32;
        self.eg_count_delta = DECAY_RATE[(rate / 4) as usize] * clock.ratio;
    }
    fn phase_rate(&mut self, clock: Clock) {
        let base = match self.eg_phase {
            ATTACKING => self.attack,
            DECAYING => self.decay,
            SUSTAIN => self.sustain_rate,
            RELEASE => self.release,
            _ => {
                self.rate(0, clock);
                return;
            }
        };
        let scaled = if base != 0 || self.eg_phase == RELEASE {
            (base + self.key_scale_rate).min(63)
        } else {
            0
        };
        self.rate(scaled, clock);
    }
    pub fn prepare(&mut self, clock: Clock) {
        if self.dirty == 0 {
            return;
        }
        self.dirty = 0;
        let detune = DETUNE[(self.detune + self.block_note) as usize] as u32;
        self.phase_delta = self
            .delta
            .wrapping_add(detune)
            .wrapping_mul(clock.multiples[(self.detune2 * 16 + self.multiple) as usize]);
        self.phase_delta_lfo = (self.phase_delta >> 11) as i32;
        self.key_scale_rate = self.block_note >> (3 - self.key_scale);
        self.tl_out = if self.mute != 0 {
            1023
        } else {
            self.total_level as i32 * 8
        };
        if self.eg_phase != OFF {
            self.phase_rate(clock);
        }
        if self.eg_phase == DECAYING {
            self.eg_next_level = self.sustain_level as i32 * 8;
        }
        if self.ssg_type != 0 && self.eg_phase != RELEASE {
            self.ssg_shape();
        }
        self.update_output();
    }
    fn shift_phase(&mut self, mut phase: u32, clock: Clock) {
        if phase == ATTACKING {
            self.total_level = self.total_level_latch;
            if self.ssg_type != 0 {
                self.ssg_phase += 1;
                if self.ssg_phase > 2 {
                    self.ssg_phase = 1;
                }
                self.ssg_shape();
            }
            if self.attack + self.key_scale_rate >= 62 {
                phase = DECAYING;
            }
        }
        let sl = self.sustain_level as i32 * 8;
        if phase == DECAYING && sl == 0 {
            phase = SUSTAIN;
        }
        match phase {
            ATTACKING => {}
            DECAYING => {
                self.eg_level = 0;
                self.eg_next_level = if self.ssg_type != 0 { sl.min(512) } else { sl };
            }
            SUSTAIN => {
                self.eg_level = sl;
                self.eg_next_level = if self.ssg_type != 0 { 512 } else { 1024 };
            }
            RELEASE => {
                if self.ssg_type != 0 {
                    self.eg_level = self.eg_level * self.ssg_vector + self.ssg_offset;
                    self.ssg_vector = 1;
                    self.ssg_offset = 0;
                }
                if self.eg_phase == ATTACKING || self.eg_level < BOTTOM {
                    self.eg_next_level = 1024;
                } else {
                    phase = OFF;
                }
            }
            _ => phase = OFF,
        }
        if phase == OFF {
            self.eg_level = BOTTOM;
            self.eg_next_level = BOTTOM;
            self.update_output();
        }
        self.eg_phase = phase;
        self.phase_rate(clock);
    }
    fn update_output(&mut self) {
        let level = if self.ssg_type != 0 {
            self.eg_level * self.ssg_vector + self.ssg_offset
        } else {
            self.eg_level
        };
        self.eg_out = (self.tl_out + level).min(1023) << 3;
    }
    fn step_envelope(&mut self, clock: Clock) {
        self.eg_count -= self.eg_count_delta;
        if self.eg_count > 0 {
            return;
        }
        self.eg_count = (2047 * 3) << 7;
        let index = (self.eg_rate * 8 + (self.eg_curve_count & 7)) as usize;
        if self.eg_phase == ATTACKING {
            let shift = ATTACK[index];
            if shift >= 0 {
                self.eg_level -= 1 + (self.eg_level >> shift);
                if self.eg_level <= 0 {
                    self.shift_phase(DECAYING, clock);
                }
            }
            self.update_output();
        } else if self.ssg_type == 0 {
            self.eg_level += DECAY[index] as i32;
            if self.eg_level >= self.eg_next_level {
                self.shift_phase(self.eg_phase + 1, clock);
            }
            self.update_output();
        } else {
            self.eg_level += 4 * DECAY[index] as i32;
            if self.eg_level >= self.eg_next_level {
                self.update_output();
                match self.eg_phase {
                    DECAYING => self.shift_phase(SUSTAIN, clock),
                    SUSTAIN => self.shift_phase(ATTACKING, clock),
                    RELEASE => self.shift_phase(OFF, clock),
                    _ => {}
                }
            }
        }
        self.eg_curve_count = self.eg_curve_count.wrapping_add(1);
    }
    fn tone(&mut self, input: i32, lfo: Option<Lfo>) -> i32 {
        let angle = ((self.phase >> 19) as i32).wrapping_add(input);
        let mut attenuation = (self.eg_out as u32).wrapping_add(SINE[(angle & 1023) as usize]);
        let mut delta = self.phase_delta;
        if let Some(lfo) = lfo {
            delta = delta.wrapping_add((self.phase_delta_lfo.wrapping_mul(lfo.pm) >> 5) as u32);
            let depth = if self.amplitude_modulation != 0 {
                (self.sensitivity >> 4) & 3
            } else {
                0
            };
            attenuation = attenuation.wrapping_add(FM_AM[depth as usize * 256 + lfo.am as usize]);
        }
        self.phase = self.phase.wrapping_add(delta);
        self.out = LOG_TO_LINEAR
            .get(attenuation as usize)
            .copied()
            .unwrap_or(0);
        self.out
    }
    pub fn calc(&mut self, input: i32, clock: Clock, lfo: Option<Lfo>) -> i32 {
        self.step_envelope(clock);
        if lfo.is_none() {
            self.previous_out = self.out;
        }
        self.tone(input >> 1, lfo)
    }
    pub fn feedback(&mut self, shift: u32, clock: Clock, lfo: Option<Lfo>) -> i32 {
        self.step_envelope(clock);
        let sum = self.out + self.previous_out;
        self.previous_out = self.out;
        let input = if shift < 31 {
            ((sum << 17) >> shift) >> 19
        } else {
            0
        };
        self.tone(input, lfo);
        if lfo.is_some() {
            self.out
        } else {
            self.previous_out
        }
    }
    pub fn noise(&mut self, noise: u32, clock: Clock) -> i32 {
        self.step_envelope(clock);
        let level = (1023 - self.tl_out - self.eg_level).max(0) * 2;
        self.out = if noise & 1 != 0 { level } else { -level };
        self.out
    }
}
impl Operator {
    pub fn set_dt(&mut self, value: u32) {
        self.detune = value * 32;
        self.dirty = 1;
    }
    pub fn set_dt2(&mut self, value: u32) {
        self.detune2 = value & 3;
        self.dirty = 1;
    }
    pub fn set_multiple(&mut self, value: u32) {
        self.multiple = value;
        self.dirty = 1;
    }
    pub fn set_ar(&mut self, value: u32) {
        self.attack = value;
        self.dirty = 1;
    }
    pub fn set_dr(&mut self, value: u32) {
        self.decay = value;
        self.dirty = 1;
    }
    pub fn set_sr(&mut self, value: u32) {
        self.sustain_rate = value;
        self.dirty = 1;
    }
    pub fn set_sl(&mut self, value: u32) {
        self.sustain_level = value;
        self.dirty = 1;
    }
    pub fn set_rr(&mut self, value: u32) {
        self.release = value;
        self.dirty = 1;
    }
    pub fn set_ks(&mut self, value: u32) {
        self.key_scale = value;
        self.dirty = 1;
    }
    pub fn set_ms(&mut self, value: u32) {
        self.sensitivity = value;
        self.dirty = 1;
    }
    pub fn set_amon(&mut self, value: bool) {
        self.amplitude_modulation = value as u8;
        self.dirty = 1;
    }
    pub fn set_mute(&mut self, value: bool) {
        self.mute = value as u8;
        self.dirty = 1;
    }
}
