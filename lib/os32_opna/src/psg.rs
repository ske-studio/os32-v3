// Port of fmgen_psg.cpp (Copyright (C) cisc 1997, 1999).
use crate::{state::Psg, tables::*};

const OVERSAMPLING: u32 = 2;
const TONE_SHIFT: u32 = 24;
const ENV_SHIFT: u32 = 22;
const NOISE_SHIFT: u32 = 14;

impl Psg {
    /// Initialize zeroed chip storage. Subsequent hardware resets preserve tone/noise phase.
    pub fn init(&mut self, prescaler: u8) {
        self.mask = 0x3f;
        self.tone_count = [0; 3];
        self.noise_count = 0;
        self.reset(prescaler);
    }

    pub fn reset(&mut self, prescaler: u8) {
        for reg in 0..16 {
            self.set_reg(
                reg,
                if matches!(reg, 7 | 14 | 15) { 255 } else { 0 },
                prescaler,
            );
        }
    }

    pub fn set_channel_mask(&mut self, muted: i32) {
        self.mask = !muted;
        for ch in 0..3 {
            self.update_level(ch);
        }
    }

    fn update_level(&mut self, ch: usize) {
        self.output_level[ch] = if self.mask & (1 << ch) != 0 {
            PSG_VOLUME[usize::from(self.registers[8 + ch] & 15) * 2 + 1] as u32
        } else {
            0
        };
    }

    pub fn set_clock(&mut self, prescaler: u8) {
        for reg in [0, 2, 4, 6, 11] {
            self.set_reg(reg, self.registers[reg], prescaler);
        }
        // SetClock halves the noise increment; SetReg(6) uses the full increment.
        self.noise_period /= 2;
    }

    pub fn set_reg(&mut self, reg: usize, data: u8, prescaler: u8) {
        if reg >= self.registers.len() {
            return;
        }
        self.registers[reg] = data;
        let p = usize::from(prescaler);
        match reg {
            0..=5 => {
                let ch = reg / 2;
                let period = self.word(ch * 2) & 0xfff;
                self.tone_period[ch] =
                    [TONE_BASE_P0[0], TONE_BASE_P1[0], TONE_BASE_P2[0]][p] / period.max(1);
            }
            6 => {
                self.noise_period = [NOISE_BASE_P0[0], NOISE_BASE_P1[0], NOISE_BASE_P2[0]][p]
                    / u32::from(data & 31).max(1)
            }
            8..=10 => self.update_level(reg - 8),
            11 | 12 => {
                let base = [ENV_BASE_P0[0], ENV_BASE_P1[0], ENV_BASE_P2[0]][p];
                let period = self.word(11);
                self.envelope_period = base.checked_div(period).unwrap_or(base * 2);
            }
            13 => self.envelope_count = 0,
            _ => {}
        }
    }

    fn word(&self, low: usize) -> u32 {
        u32::from(u16::from_le_bytes([
            self.registers[low],
            self.registers[low + 1],
        ]))
    }

    fn fold_envelope(&self, mut phase: u32, limit: u32) -> u32 {
        if phase >= limit {
            if self.registers[13] & 0x0b != 0x0a {
                phase |= limit / 2;
            }
            phase &= limit - 1;
        }
        phase
    }

    /// Add PSG output to interleaved i32 stereo frames.
    pub fn mix(&mut self, stereo: &mut [i32]) {
        let enabled = !self.registers[7];
        if enabled & 63 == 0
            && (self.registers[8] | self.registers[9] | self.registers[10]) & 31 == 0
        {
            return;
        }
        let envelope = core::array::from_fn::<_, 3, _>(|ch| {
            self.mask & (1 << ch) != 0 && self.registers[8 + ch] & 16 != 0
        });
        let use_env = envelope.contains(&true);
        let shape = usize::from(self.registers[13] & 15);
        let frames = (stereo.len() / 2) as u32;
        const ENV_LIMIT: u32 = 1 << (ENV_SHIFT + OVERSAMPLING + 6);
        for frame in stereo.chunks_exact_mut(2) {
            let mut sum = 0;
            for _ in 0..1 << OVERSAMPLING {
                let mut env = 0;
                if use_env {
                    env = PSG_ENVELOPE
                        [shape * 64 + (self.envelope_count >> (ENV_SHIFT + OVERSAMPLING)) as usize];
                    self.envelope_count = self.fold_envelope(
                        self.envelope_count.wrapping_add(self.envelope_period),
                        ENV_LIMIT,
                    );
                }
                let mut noise = 0;
                if use_env || enabled & 0x38 != 0 {
                    let bit = self.noise_count >> (NOISE_SHIFT + OVERSAMPLING + 1);
                    noise = (PSG_NOISE[((bit >> 5) & 2047) as usize] >> (bit & 31)) & 1;
                    self.noise_count = self.noise_count.wrapping_add(self.noise_period);
                }
                for (ch, &from_env) in envelope.iter().enumerate() {
                    let tone_high = enabled & (1 << ch) != 0
                        && self.tone_period[ch] <= 1 << TONE_SHIFT
                        && (self.tone_count[ch] >> (TONE_SHIFT + OVERSAMPLING)) & 1 != 0;
                    let noise_high = enabled & (8 << ch) != 0 && noise != 0;
                    let level = if from_env { env } else { self.output_level[ch] } as i32;
                    sum += if tone_high || noise_high {
                        level
                    } else {
                        -level
                    };
                    self.tone_count[ch] = self.tone_count[ch].wrapping_add(self.tone_period[ch]);
                }
            }
            let sample = sum / (1 << OVERSAMPLING);
            for out in frame {
                *out = out.wrapping_add(sample);
            }
        }
        if !use_env {
            // fmgen advances the idle envelope once per Mix call at reduced precision.
            let delta = (self.envelope_period >> (8 - OVERSAMPLING)).wrapping_mul(frames);
            let phase = (self.envelope_count >> 8).wrapping_add(delta);
            self.envelope_count = self.fold_envelope(phase, ENV_LIMIT >> 8) << 8;
        }
    }
}
