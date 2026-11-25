//! Four-operator routing from fmgen Channel4.
use crate::{
    operator::{Clock, Lfo, OFF},
    state::Channel,
    tables::{FEEDBACK, FM_PM, KEY_FRACTION},
};

impl Channel {
    pub fn reset(&mut self, clock: Clock) {
        for op in &mut self.operators {
            op.reset(clock);
        }
    }
    pub fn prepare(&mut self, clock: Clock) -> u32 {
        for op in &mut self.operators {
            op.prepare(clock);
        }
        let active = self.operators.iter().any(|op| op.eg_phase != OFF);
        let mask = if self.operators.iter().any(|op| op.amplitude_modulation != 0) {
            0x37
        } else {
            7
        };
        u32::from(active) | (u32::from(self.operators[0].sensitivity & mask != 0) << 1)
    }
    pub fn set_fnum(&mut self, f: u32) {
        for op in &mut self.operators {
            op.set_fnum(f);
        }
    }
    pub fn set_kckf(&mut self, kc: u32, kf: u32) {
        const KC: [u32; 16] = [
            5197, 5506, 5833, 6180, 6180, 6547, 6937, 7349, 7349, 7786, 8249, 8740, 8740, 9259,
            9810, 10394,
        ];
        let coarse = (KC[(kc & 15) as usize] + 2) & !3;
        let dp = (coarse * KEY_FRACTION[(kf & 63) as usize] as u32) & !0x7ffff;
        for op in &mut self.operators {
            op.set_dp_bn(dp >> (19 - ((kc >> 4) & 7)), (kc >> 2) & 31);
        }
    }
    pub fn key_control(&mut self, mask: u32, clock: Clock) {
        for (i, op) in self.operators.iter_mut().enumerate() {
            op.key(mask & (1 << i) != 0, clock);
        }
    }
    pub fn set_algorithm(&mut self, algo: u32) {
        self.algorithm = algo;
        self.operators[0].reset_feedback();
    }
    pub fn set_feedback(&mut self, fb: u32) {
        self.feedback = FEEDBACK[fb as usize] as u32;
    }
    pub fn set_ms(&mut self, ms: u32) {
        for op in &mut self.operators {
            op.set_ms(ms);
        }
    }
    pub fn mute(&mut self, mute: bool) {
        for op in &mut self.operators {
            op.set_mute(mute);
        }
    }
    pub fn refresh(&mut self) {
        for op in &mut self.operators {
            op.dirty = 1;
        }
    }
    pub fn lfo(&self, pm: u8, am: u8) -> Lfo {
        Lfo {
            pm: FM_PM[(self.operators[0].sensitivity as usize & 7) * 256 + pm as usize],
            am,
        }
    }
    pub fn calc(&mut self, clock: Clock, lfo: Option<Lfo>) -> i32 {
        let [a, b, c, d] = &mut self.operators;
        let algo = self.algorithm;
        // Operator 2 reads the previous sample of operator 1.
        let c_in = match algo {
            0 | 2 => b.out,
            1 => a.out + b.out,
            5 => a.out,
            _ => 0,
        };
        let c_out = c.calc(c_in, clock, lfo);
        let b_in = if matches!(algo, 0 | 3 | 4 | 5 | 6) {
            a.out
        } else {
            0
        };
        let b_out = b.calc(b_in, clock, lfo);
        let d_in = match algo {
            0 | 1 | 4 => c_out,
            2 => a.out + c_out,
            3 => b_out + c_out,
            5 => a.out,
            _ => 0,
        };
        let d_out = d.calc(d_in, clock, lfo);
        let feedback = a.feedback(self.feedback, clock, lfo);
        match algo {
            4 => b_out + d_out,
            5 | 6 => c_out + b_out + d_out,
            7 => c_out + b_out + d_out + feedback,
            _ => d_out,
        }
    }
    pub fn calc_noise(&mut self, noise: u32, clock: Clock, lfo: Option<Lfo>) -> i32 {
        const ROUTE: [[usize; 5]; 8] = [
            [0, 1, 1, 2, 3],
            [1, 0, 0, 1, 2],
            [1, 1, 1, 0, 2],
            [0, 1, 2, 1, 2],
            [0, 1, 2, 2, 1],
            [0, 1, 0, 1, 1],
            [0, 1, 2, 1, 1],
            [1, 0, 1, 0, 0],
        ];
        let [bi, bo, ci, co, out] = ROUTE[self.algorithm as usize];
        let [a, b, c, d] = &mut self.operators;
        self.buffer = [a.out, 0, 0, 0];
        a.feedback(self.feedback, clock, lfo);
        self.buffer[bo] += b.calc(self.buffer[bi], clock, lfo);
        self.buffer[co] += c.calc(self.buffer[ci], clock, lfo);
        let delayed = d.out;
        d.noise(noise, clock);
        self.buffer[out] + delayed
    }
}

impl crate::state::AudioState {
    /// Add FM to the interleaved mix accumulator for this render block.
    pub fn fm_mix(&mut self, stereo_scratch: &mut [i32]) {
        use crate::tables::{FM_GAIN, OPNA_AM, OPNA_PM};
        let frames = stereo_scratch.len() / 2;
        if frames == 0 {
            return;
        }
        let clock = Clock::new(self.prescaler);
        if self.timer_control_shadow & 0xc0 == 0 {
            self.channels[2].set_fnum(self.fnum[2]);
        } else {
            let [f0, f1, f2] = self.channel3_fnum;
            for (op, f) in self.channels[2]
                .operators
                .iter_mut()
                .zip([f1, f2, f0, self.fnum[2]])
            {
                op.set_fnum(f);
            }
        }
        let count = if self.reg29 & 0x80 != 0 { 6 } else { 3 };
        let mut active = 0;
        for (i, ch) in self.channels[..count].iter_mut().enumerate() {
            active |= ch.prepare(clock) << (2 * i);
        }
        if active & 0x555 == 0 {
            return;
        }
        let use_lfo = self.reg22 & 8 != 0 && active & 0xaaa != 0;
        for stereo in stereo_scratch.chunks_exact_mut(2) {
            if use_lfo {
                let index = ((self.lfo_count >> 15) & 255) as usize;
                self.pm_level = OPNA_PM[index] as u32;
                self.am_level = OPNA_AM[index] as u32;
                self.lfo_count = self.lfo_count.wrapping_add(self.lfo_step);
            }
            let mut bus = [0; 4];
            for (i, ch) in self.channels[..count].iter_mut().enumerate() {
                if active & (1 << (i * 2)) == 0 {
                    continue;
                }
                let lfo = use_lfo.then(|| ch.lfo(self.pm_level as u8, self.am_level as u8));
                if let Some(value) = lfo {
                    self.pm_value = value.pm;
                }
                bus[self.pan[i] as usize] += ch.calc(clock, lfo);
            }
            for (sample, sum) in stereo.iter_mut().zip([bus[2] + bus[3], bus[1] + bus[3]]) {
                *sample += (sum.clamp(-32768, 32767) * FM_GAIN[0]) >> 14;
            }
        }
    }
}
