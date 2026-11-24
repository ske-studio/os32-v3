//! Banked YM2608 register dispatch, ported from fmgen OPNA and OPNBase.
use crate::{adpcm, operator::Clock, rhythm, state::AudioState, tables::*};

impl AudioState {
    pub fn apply_csm(&mut self) {
        let clock = Clock::new(self.prescaler);
        self.channels[2].key_control(0, clock);
        self.channels[2].key_control(15, clock);
    }

    pub fn write(&mut self, reg: u16, data: u8, ram: &mut adpcm::Ram) {
        let slot = usize::from(reg & 3);
        let ch = slot + if reg & 0x100 != 0 { 3 } else { 0 };
        let d = u32::from(data);
        match reg {
            0..=15 => self.psg.set_reg(reg as usize, data, self.prescaler),
            0x10..=0x1f => rhythm::write(self, reg, data),
            0x100..=0x110 => adpcm::write(self, ram, reg, data),
            0x22 => {
                if (self.reg22 ^ data) & 8 != 0 {
                    self.lfo_count = 0;
                }
                self.reg22 = data;
                let steps = [&LFO_STEP_P0, &LFO_STEP_P1, &LFO_STEP_P2];
                self.lfo_step = if data & 8 != 0 {
                    steps[self.prescaler as usize][(data & 7) as usize]
                } else {
                    0
                };
            }
            0x27 => self.timer_control_shadow = data,
            0x28 if data & 3 != 3 => {
                let index = (data & 3) + if data & 4 != 0 { 3 } else { 0 };
                self.channels[index as usize].key_control(d >> 4, Clock::new(self.prescaler));
            }
            0x29 => self.reg29 = data,
            0x2d..=0x2f if self.prescaler != (reg - 0x2d) as u8 => {
                self.prescaler = (reg - 0x2d) as u8;
                self.psg.set_clock(self.prescaler);
            }
            0xa4..=0xa6 | 0x1a4..=0x1a6 => self.fnum_latch[ch] = data,
            0xa0..=0xa2 | 0x1a0..=0x1a2 => {
                self.fnum[ch] = d | (u32::from(self.fnum_latch[ch]) << 8);
                self.channels[ch].set_fnum(self.fnum[ch]);
            }
            0xac..=0xae => self.fnum_latch[slot + 6] = data,
            0xa8..=0xaa => {
                self.channel3_fnum[slot] = d | (u32::from(self.fnum_latch[slot + 6]) << 8)
            }
            0xb0..=0xb2 | 0x1b0..=0x1b2 => {
                self.channels[ch].set_feedback((d >> 3) & 7);
                self.channels[ch].set_algorithm(d & 7);
            }
            0xb4..=0xb6 | 0x1b4..=0x1b6 => {
                self.pan[ch] = data >> 6;
                self.channels[ch].set_ms(d);
            }
            0x30..=0x9f | 0x130..=0x19f if slot < 3 => {
                let order = [0, 2, 1, 3];
                let op = &mut self.channels[ch].operators[order[((reg >> 2) & 3) as usize]];
                match (reg >> 4) & 15 {
                    3 => {
                        op.set_dt((d >> 4) & 7);
                        op.set_multiple(d & 15);
                    }
                    4 => op.set_tl(d & 127, ch == 2 && self.timer_control_shadow & 128 != 0),
                    5 => {
                        op.set_ks(d >> 6);
                        op.set_ar((d & 31) * 2);
                    }
                    6 => {
                        op.set_dr((d & 31) * 2);
                        op.set_amon(d & 128 != 0);
                    }
                    7 => op.set_sr((d & 31) * 2),
                    8 => {
                        op.set_sl(if d >> 4 == 15 { 124 } else { (d >> 4) * 4 });
                        op.set_rr((d & 15) * 4 + 2);
                    }
                    _ => op.set_ssg(d & 15),
                }
            }
            _ => {}
        }
    }

    pub fn reset(&mut self, ram: &mut adpcm::Ram) {
        self.reg29 = 31;
        self.write(0x2d, 0, ram);
        self.psg.reset(0);
        // OPNABase::Reset calls its nonvirtual base SetReg. Rhythm levels
        // and ADPCM register latches survive reset.
        for reg in (0x20..0x28).chain(0x30..0xc0).chain(0x130..0x1c0) {
            self.write(reg, 0, ram);
        }
        self.pan.fill(3);
        for ch in &mut self.channels {
            ch.reset(Clock::new(0));
        }
        adpcm::reset(self);
        rhythm::reset(self);
        self.lfo_count = 0;
    }
}
