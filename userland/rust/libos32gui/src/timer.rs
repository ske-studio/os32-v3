//! timer.rs — タイマの所有型 (契約 U5)。票 C2 作業 7。
//!
//! `set_timer(id: u8, interval_ticks: u16, repeat: bool)` / `kill_timer(id)`、
//! `Timer` イベントで届く。PIT 10ms 粒度、8 本 / アプリ。単発 (repeat=false) は
//! WM が 1 回発火して消す (共有 ABI が契約どおりになった 2026-09-06 以降)。

use crate::client::{self, GuiResult};
use crate::window::Window;

/// アプリのタイマ 1 本。`Drop` で `kill_timer`。
pub struct Timer {
    window: u32,
    id: u8,
}

impl Timer {
    /// 反復タイマ (契約 U5 の `repeat = true`)。`interval_ticks` は 10ms 単位。
    pub fn repeating(window: &Window, id: u8, interval_ticks: u16) -> GuiResult<Timer> {
        client::timer_set(window.id(), id, interval_ticks, true)?;
        Ok(Timer { window: window.id(), id })
    }

    /// 単発タイマ (`repeat = false`)。1 回の `Timer` イベントで WM 側が止まる。
    pub fn once(window: &Window, id: u8, delay_ticks: u16) -> GuiResult<Timer> {
        client::timer_set(window.id(), id, delay_ticks, false)?;
        Ok(Timer { window: window.id(), id })
    }

    #[inline]
    pub fn id(&self) -> u8 {
        self.id
    }

    #[inline]
    pub fn window(&self) -> u32 {
        self.window
    }

    /// 間隔を張り替える (反復として)。
    pub fn restart(&self, interval_ticks: u16) -> GuiResult<()> {
        client::timer_set(self.window, self.id, interval_ticks, true)
    }
}

impl Drop for Timer {
    fn drop(&mut self) {
        let _ = client::timer_kill(self.window, self.id);
    }
}
