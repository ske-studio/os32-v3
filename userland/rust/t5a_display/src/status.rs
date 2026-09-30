use crate::state::{Display, Fixture, Stop};
use core::fmt::{self, Write};
pub const LINE_COUNT: usize = 5;
pub struct Line {
    bytes: [u8; 64],
    len: usize,
}
impl Line {
    fn new() -> Self {
        Self {
            bytes: [0; 64],
            len: 0,
        }
    }
    pub fn bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }
}
impl Write for Line {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let end = self.len.checked_add(s.len()).ok_or(fmt::Error)?;
        let dst = self.bytes.get_mut(self.len..end).ok_or(fmt::Error)?;
        dst.copy_from_slice(s.as_bytes());
        self.len = end;
        Ok(())
    }
}
/// con_sink を吸っている側の勘定 (票 K6C-A §2-2: 欠けを黙らせない)。
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SinkStatus {
    /// con_sink_read が返した総バイト数。
    pub bytes: u64,
    /// 適用したレコード数。
    pub records: u64,
    /// カーネルがあふれで捨てたレコード数 (con_sink_stat)。
    pub dropped: u32,
    /// まだリングに残っているバイト数 (con_sink_stat)。
    pub ring: u32,
    /// 形式違反で捨てたバイト数 (パーサの Stop)。
    pub malformed: u32,
    /// 画面を畳んだ回数と、それでも置けなかった文字数。
    pub wraps: u32,
    pub lost: u32,
    /// con_sink_read の最後の負の戻り値。
    pub error: Option<i32>,
    /// これ以上読まない (直らない失敗)。
    pub stopped: bool,
    /// `kbd_inject` の最後の負の戻り値 (票 §6 K7-A:「戻り値が負なら状態行に出す」)。
    pub inject_error: Option<i32>,
    /// 注入リングがあふれて積めなかったバイト数 (`rc < len` の差)。
    /// 打鍵が消えたことを黙らせない (票 K6C-A §2-2 と同じ理由)。
    pub inject_short: u32,
    /// クリップボード (コピー / 貼り付け) の最後の失敗 (票 N4b)。`os32gui_clip_*`
    /// の負の戻り値 (`HOST_E*`)。成功で `None` に戻す。黙らせない ([V4])。
    pub clip_error: Option<i32>,
}

/// Live (con_sink) 用の状態行。fixture 用とは別物なので分けてある。
fn live_lines(s: &Display<'_>, runs: u64, paint_error: bool, k: &SinkStatus) -> [Line; LINE_COUNT] {
    let mut out: [Line; LINE_COUNT] = core::array::from_fn(|_| Line::new());
    if paint_error {
        write!(out[0], "PAINT ERROR").unwrap();
    } else if let Some(rc) = k.error {
        /* 読めていない理由を必ず出す。stopped なら以後試さない。 */
        let why = if rc == crate::sink::ERR_EXIST {
            "busy"
        } else {
            "err"
        };
        write!(out[0], "LIVE {} rc={}", why, rc).unwrap();
        if k.stopped {
            write!(out[0], " stopped").unwrap();
        }
    } else {
        write!(out[0], "LIVE reading").unwrap();
    }
    /* 注入側の失敗は読み出し側の行に足す (行数を増やすと本文が 1 行減る)。
     * 入り切らなければ黙って落とす — 状態行の失敗で panic させない。 */
    if let Some(rc) = k.inject_error {
        let _ = write!(out[0], " inject rc={}", rc);
    }
    if k.inject_short != 0 {
        let _ = write!(out[0], " injdrop={}", k.inject_short);
    }
    if let Some(rc) = k.clip_error {
        let _ = write!(out[0], " clip rc={}", rc);
    }
    write!(out[1], "in={}B rec={}", k.bytes, k.records).unwrap();
    write!(out[2], "dropped={} ring={}B", k.dropped, k.ring).unwrap();
    write!(
        out[3],
        "bad={}B wrap={} lost={}",
        k.malformed, k.wraps, k.lost
    )
    .unwrap();
    write!(
        out[4],
        "top={} rows={} prev_runs={}",
        s.top,
        s.terminal.model().state().retained_rows.end,
        runs
    )
    .unwrap();
    out
}

pub fn lines(
    s: &Display<'_>,
    runs: u64,
    paint_error: bool,
    sink: &SinkStatus,
) -> [Line; LINE_COUNT] {
    if s.fixture == Fixture::Live {
        return live_lines(s, runs, paint_error, sink);
    }
    let mut out: [Line; LINE_COUNT] = core::array::from_fn(|_| Line::new());
    let fixture = match s.fixture {
        /* live_lines へ分岐済み。panic を増やさないため到達不能でも値を置く。 */
        Fixture::Live => "LIVE",
        Fixture::Normal => "1 NORMAL",
        Fixture::Exact => "2 EXACT",
        Fixture::Full => "3 FULL",
        Fixture::TooWide => "4 TOO WIDE",
        Fixture::Finish => "5 FINISH",
    };
    if paint_error {
        write!(out[0], "PAINT ERROR").unwrap();
    } else {
        write!(out[0], "{} ", fixture).unwrap();
        match s.stop {
            Stop::Complete => write!(out[0], "complete"),
            Stop::Feed(e) => write!(out[0], "feed:{:?}", e),
            Stop::Finish(e) => write!(out[0], "finish:{:?}", e),
        }
        .unwrap();
    }
    write!(out[1], "consumed={} bytes", s.consumed).unwrap();
    write!(out[2], "unconsumed={} bytes", s.total - s.consumed).unwrap();
    write!(out[3], "pending=").unwrap();
    for (i, pending) in s.terminal.pending().iter().enumerate() {
        if i != 0 {
            write!(out[3], ",").unwrap();
        }
        match pending {
            Some(c) => write!(out[3], "{:04X}", *c as u32),
            None => write!(out[3], "----"),
        }
        .unwrap();
    }
    write!(
        out[4],
        "top={} rows={} prev_runs={}",
        s.top,
        s.terminal.model().state().retained_rows.end,
        runs
    )
    .unwrap();
    out
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::CAPACITY;
    use libos32term::model::BLANK;
    #[test]
    fn stopped_status_distinguishes_bytes_pending_and_finish() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Full).unwrap();
        let k = SinkStatus::default();
        let l = lines(&s, 42, false, &k);
        assert_eq!(l[0].bytes(), b"3 FULL feed:Full");
        assert_eq!(l[1].bytes(), b"consumed=2561 bytes");
        assert_eq!(l[2].bytes(), b"unconsumed=1 bytes");
        assert_eq!(l[3].bytes(), b"pending=0058,----");
        assert_eq!(l[4].bytes(), b"top=0 rows=64 prev_runs=42");
        drop(s);
        let s = Display::load(&mut cells, Fixture::Finish).unwrap();
        assert_eq!(lines(&s, 0, false, &k)[0].bytes(), b"5 FINISH finish:Full");
        assert_eq!(lines(&s, 0, true, &k)[0].bytes(), b"PAINT ERROR");
    }

    #[test]
    fn live_status_reports_drops_and_reader_rejection() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Live).unwrap();
        let mut k = SinkStatus {
            bytes: 4096,
            records: 37,
            dropped: 5,
            ring: 128,
            malformed: 2,
            wraps: 1,
            lost: 3,
            error: None,
            stopped: false,
            inject_error: None,
            inject_short: 0,
            clip_error: None,
        };
        let l = lines(&s, 9, false, &k);
        assert_eq!(l[0].bytes(), b"LIVE reading");
        assert_eq!(l[1].bytes(), b"in=4096B rec=37");
        assert_eq!(l[2].bytes(), b"dropped=5 ring=128B");
        assert_eq!(l[3].bytes(), b"bad=2B wrap=1 lost=3");
        assert_eq!(l[4].bytes(), b"top=0 rows=1 prev_runs=9");

        /* 読み手拒否は「busy」で出し、まだ諦めていないことも分かる。 */
        k.error = Some(crate::sink::ERR_EXIST);
        assert_eq!(lines(&s, 0, false, &k)[0].bytes(), b"LIVE busy rc=-5");
        /* 直らない失敗は stopped まで出す。 */
        k.error = Some(crate::sink::ERR_INVAL);
        k.stopped = true;
        assert_eq!(
            lines(&s, 0, false, &k)[0].bytes(),
            b"LIVE err rc=-9 stopped"
        );
        /* Paint の失敗はすべてに優先する。 */
        assert_eq!(lines(&s, 0, true, &k)[0].bytes(), b"PAINT ERROR");
    }

    #[test]
    fn live_status_reports_injection_failures() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Live).unwrap();
        let mut k = SinkStatus::default();
        /* 何も起きていなければ読み出し側の行はそのまま。 */
        assert_eq!(lines(&s, 0, false, &k)[0].bytes(), b"LIVE reading");
        /* 負の戻り値は必ず出す (票 §6 K7-A)。 */
        k.inject_error = Some(crate::sink::ERR_EXIST);
        assert_eq!(
            lines(&s, 0, false, &k)[0].bytes(),
            b"LIVE reading inject rc=-5"
        );
        /* あふれで消えた打鍵も出す。 */
        k.inject_short = 12;
        assert_eq!(
            lines(&s, 0, false, &k)[0].bytes(),
            b"LIVE reading inject rc=-5 injdrop=12"
        );
        /* 読めていない理由と同居できる。 */
        k.error = Some(crate::sink::ERR_EXIST);
        assert_eq!(
            lines(&s, 0, false, &k)[0].bytes(),
            b"LIVE busy rc=-5 inject rc=-5 injdrop=12"
        );
        /* クリップボードの失敗も同じ行に足す (票 N4b、[V4])。 */
        k.clip_error = Some(-103);
        assert_eq!(
            lines(&s, 0, false, &k)[0].bytes(),
            b"LIVE busy rc=-5 inject rc=-5 injdrop=12 clip rc=-103"
        );
    }
}
