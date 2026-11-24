use crate::{state::TimerState, tables::*};

impl TimerState {
    pub fn reset(&mut self) {
        self.step = TIMER_STEP_P0[0];
        self.status = 0;
        self.control = 0;
        self.count_a = 0;
        self.count_b = 0;
        for reg in 0x24..=0x26 {
            self.write(reg, 0);
        }
    }

    pub fn write(&mut self, reg: u16, value: u8) {
        match reg {
            0x24 | 0x25 => {
                self.reg_a[(reg & 1) as usize] = value;
                let a = i32::from(self.reg_a[0]) * 4 + i32::from(self.reg_a[1] & 3);
                self.period_a = (1024 - a) * self.step;
            }
            0x26 => self.period_b = (256 - i32::from(value)) * self.step,
            0x27 => {
                let changed = self.control ^ value;
                self.control = value;
                self.status &= !((value >> 4) & 3);
                if changed & 1 != 0 {
                    self.count_a = if value & 1 != 0 { self.period_a } else { 0 };
                }
                if changed & 2 != 0 {
                    self.count_b = if value & 2 != 0 { self.period_b } else { 0 };
                }
            }
            0x2d => self.step = TIMER_STEP_P0[0],
            0x2e => self.step = TIMER_STEP_P1[0],
            0x2f => self.step = TIMER_STEP_P2[0],
            0x110 if value & 0x80 != 0 => self.status = 0,
            _ => {}
        }
    }

    pub fn csm_due(&self, events: u8) -> bool {
        events & 1 != 0 && self.control & 0x80 != 0
    }

    /// Current combined level for the standalone count wrapper.
    pub fn irq_level(&self, audio: &crate::state::AudioState) -> bool {
        (u32::from(self.status) | audio.status) & audio.status_mask & u32::from(audio.reg29) != 0
    }

    pub fn next_event(&self) -> u32 {
        fn deadline(count: i32, shift: u32) -> u32 {
            if count == 0 {
                u32::MAX
            } else {
                (count as u32).div_ceil(1 << shift)
            }
        }
        deadline(self.count_a, 16).min(deadline(self.count_b, 12))
    }

    /// Caller splits elapsed time at the next deadline and at 32767 us.
    /// Return expiration bits (A=1, B=2), independently of status enables.
    pub fn count(&mut self, us: u16, status_mask: u8) -> u8 {
        fn advance(count: &mut i32, period: i32, elapsed: i32) -> bool {
            if *count == 0 {
                return false;
            }
            *count -= elapsed;
            if *count > 0 {
                return false;
            }
            *count = period - (-*count % period);
            true
        }
        let a = advance(&mut self.count_a, self.period_a, i32::from(us) << 16);
        let b = advance(&mut self.count_b, self.period_b, i32::from(us) << 12);
        let events = u8::from(a) | (u8::from(b) << 1);
        self.status |= events & (self.control >> 2) & status_mask;
        events
    }
}
