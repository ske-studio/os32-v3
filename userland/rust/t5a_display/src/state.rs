use libos32term::{
    model::{Cell, Error, Model},
    stream::Terminal,
};
use libos32term_render::width;

pub const COLS: usize = 40;
pub const ROWS: usize = 64;
pub const CAPACITY: usize = COLS * ROWS;
pub const HISTORY_LINES: usize = 40;
const _: () = assert!(COLS > 0 && ROWS > 0 && CAPACITY / COLS == ROWS);
pub const NORMAL: &[u8] =
    "ASCII 0123\n日本語 あいう\nＡＢ１２！\nｱｲｳ ¥\nNUL:\0 ESC:\u{1b}\n".as_bytes();
pub const CONTROLS: &[u8] = b"invalid:\xff\nTAB:a\tb\nBS:ab\x08Z\nCR:old\rNEW\n";
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Fixture {
    /// con_sink から吸った出力だけが入る空の画面 (票 K6C-A §2-4)。
    /// ゲストはこれで始まり、固定 fixture はホスト試験の中だけで使う。
    Live,
    Normal,
    Exact,
    Full,
    TooWide,
    Finish,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Stop {
    Complete,
    Feed(Error),
    Finish(Error),
}
#[derive(Clone, Copy)]
pub enum Movement {
    First,
    Up,
    Down,
    Last,
}
/// `Display::feed_live` の結果。`error` が立っているときだけ `rest` /
/// `pending` に中身がある。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LiveFeed<'b> {
    /// まだ入っていない生バイト。
    pub rest: &'b [u8],
    /// 復号は済んだが書けなかった文字 (先頭から詰まっている)。
    pub pending: [Option<char>; 2],
    pub error: Option<Error>,
}

pub struct Display<'a> {
    pub terminal: Terminal<'a>,
    pub fixture: Fixture,
    pub top: usize,
    pub consumed: usize,
    pub total: usize,
    pub stop: Stop,
}
impl<'a> Display<'a> {
    pub fn load(cells: &'a mut [Cell], fixture: Fixture) -> Result<Self, Error> {
        let (cols, rows) = if fixture == Fixture::TooWide {
            (1, 1)
        } else {
            (COLS, ROWS)
        };
        let mut state = Self {
            terminal: Terminal::new(Model::new(cells, cols, rows, width)?),
            fixture,
            top: 0,
            consumed: 0,
            total: 0,
            stop: Stop::Complete,
        };
        if fixture == Fixture::Live {
            /* 空で始める。finish() も呼ばない — 以後 feed_live() が続きを
             * 流すので、ここで復号器を締めてはいけない。 */
            return Ok(state);
        }
        if fixture == Fixture::Normal {
            state.feed(NORMAL);
            state.feed(CONTROLS);
            for _ in 0..HISTORY_LINES {
                state.feed(b".\r\n");
            }
        } else if fixture == Fixture::TooWide {
            state.feed("日!".as_bytes());
        } else {
            for _ in 0..CAPACITY {
                state.feed(b"A");
            }
            if fixture == Fixture::Full {
                state.feed(b"XY");
            }
            if fixture == Fixture::Finish {
                state.feed(b"\xe3\x81");
            }
        }
        state.finish();
        Ok(state)
    }
    pub fn move_top(&mut self, movement: Movement, visible: usize) {
        let last = self
            .terminal
            .model()
            .state()
            .retained_rows
            .end
            .saturating_sub(visible.max(1));
        self.top = match movement {
            Movement::First => 0,
            Movement::Last => last,
            Movement::Up => self.top.saturating_sub(1).min(last),
            Movement::Down => self.top.saturating_add(1).min(last),
        };
    }
    /// con_sink から来た PRINT 本体を流す (Live 用)。
    ///
    /// 固定 fixture 用の `feed` と違い、**一度止まっても諦めない**:
    /// 画面が埋まった (`Error::Full`) ときは未消費の残りと未描画の文字を
    /// 返し、呼び側 (`Session::apply`) が画面を畳んでから続きを入れる。
    pub fn feed_live<'b>(&mut self, bytes: &'b [u8]) -> LiveFeed<'b> {
        self.total += bytes.len();
        let report = self.terminal.feed(bytes);
        self.consumed += report.consumed;
        match report.error {
            None => {
                self.stop = Stop::Complete;
                LiveFeed {
                    rest: &[],
                    pending: [None; 2],
                    error: None,
                }
            }
            Some(error) => {
                self.stop = Stop::Feed(error);
                LiveFeed {
                    /* consumed は「受け取った」バイト数。最後の 1 文字は
                     * 復号できても書けずに pending に残っているので、
                     * バイトではなく文字として持ち帰る (二重復号を避ける)。 */
                    rest: &bytes[report.consumed.min(bytes.len())..],
                    pending: self.terminal.pending(),
                    error: Some(error),
                }
            }
        }
    }

    /// 復号を経由せずに 1 文字だけ置く (畳んだ直後の pending の戻し用)。
    pub fn place(&mut self, ch: char) -> Result<(), Error> {
        self.terminal.model_mut().write_char(ch)
    }

    /// CURSOR レコードの適用。モデルの外 (未到達の行など) なら何もしない。
    /// 端末の見た目より、すでに描いた内容を壊さないことを優先する。
    pub fn place_cursor(&mut self, x: usize, y: usize) -> bool {
        self.terminal.model_mut().set_cursor(x, y).is_ok()
    }

    fn feed(&mut self, bytes: &[u8]) {
        self.total += bytes.len();
        if self.stop != Stop::Complete {
            return;
        }
        let report = self.terminal.feed(bytes);
        self.consumed += report.consumed;
        if let Some(error) = report.error {
            self.stop = Stop::Feed(error);
        }
    }
    fn finish(&mut self) {
        if self.stop == Stop::Complete {
            if let Some(error) = self.terminal.finish().error {
                self.stop = Stop::Finish(error);
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use libos32term::model::BLANK;
    #[test]
    fn normal_fixture_and_controls() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Normal).unwrap();
        assert_eq!(s.consumed, NORMAL.len() + CONTROLS.len() + 40 * 3);
        assert_eq!(s.total, s.consumed);
        assert_eq!(s.stop, Stop::Complete);
        let m = s.terminal.model();
        assert_eq!(m.cells()[0], Cell::Single('A'));
        assert_eq!(m.cells()[COLS], Cell::Wide('日'));
        assert_eq!(m.cells()[2 * COLS], Cell::Wide('Ａ'));
        assert_eq!(m.cells()[3 * COLS], Cell::Single('ｱ'));
        assert_eq!(m.cells()[4 * COLS + 4], Cell::Single('\0'));
        assert_eq!(m.cells()[4 * COLS + 10], Cell::Single('\u{1b}'));
        assert_eq!(m.cells()[5 * COLS + 8], Cell::Wide('\u{fffd}'));
        assert_eq!(m.cells()[6 * COLS + 8], Cell::Single('b'));
        assert_eq!(m.cells()[7 * COLS + 4], Cell::Single('Z'));
        assert_eq!(m.cells()[8 * COLS], Cell::Single('N'));
        assert_eq!(m.state().retained_rows.end, 50);
    }
    #[test]
    fn exact_capacity() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Exact).unwrap();
        assert_eq!(s.consumed, CAPACITY);
        assert_eq!(s.terminal.model().state().cursor, (COLS, ROWS - 1));
        assert_eq!(s.stop, Stop::Complete);
    }
    #[test]
    fn full_stops_with_pending_and_unconsumed_tail() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Full).unwrap();
        assert_eq!(s.stop, Stop::Feed(Error::Full));
        assert_eq!(s.consumed, CAPACITY + 1);
        assert_eq!(s.total - s.consumed, 1);
        assert_eq!(s.terminal.pending(), [Some('X'), None]);
    }
    #[test]
    fn one_column_too_wide() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::TooWide).unwrap();
        assert_eq!(s.stop, Stop::Feed(Error::TooWide));
        assert_eq!(s.consumed, 3);
        assert_eq!(s.total, 4);
        assert_eq!(s.terminal.pending(), [Some('日'), None]);
        assert_eq!(s.terminal.model().cells(), &[BLANK]);
    }
    #[test]
    fn incomplete_final_utf8_fails_only_at_finish() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Finish).unwrap();
        assert_eq!(s.stop, Stop::Finish(Error::Full));
        assert_eq!(s.consumed, CAPACITY + 2);
        assert_eq!(s.total, s.consumed);
        assert_eq!(s.terminal.pending(), [Some('\u{fffd}'), None]);
    }

    #[test]
    fn top_round_trip() {
        let mut cells = [BLANK; CAPACITY];
        let mut s = Display::load(&mut cells, Fixture::Normal).unwrap();
        let before = s.terminal.model().cells().to_vec();
        s.move_top(Movement::Down, 10);
        assert_eq!(s.top, 1);
        s.move_top(Movement::Last, 10);
        assert_eq!(s.top, 40);
        s.move_top(Movement::Down, 10);
        assert_eq!(s.top, 40);
        s.move_top(Movement::Up, 10);
        assert_eq!(s.top, 39);
        s.move_top(Movement::First, 10);
        assert_eq!(s.top, 0);
        s.move_top(Movement::Up, 10);
        assert_eq!(s.top, 0);
        s.move_top(Movement::Last, usize::MAX);
        assert_eq!(s.top, 0);
        assert_eq!(s.terminal.model().cells(), before);
    }
    #[test]
    fn reinitialize_same_storage_regression() {
        let mut cells = [BLANK; CAPACITY];
        for fixture in [Fixture::Full, Fixture::Finish, Fixture::TooWide] {
            {
                let _old = Display::load(&mut cells, fixture).unwrap();
            }
            let s = Display::load(&mut cells, Fixture::Normal).unwrap();
            assert_eq!(s.top, 0);
            assert_eq!(s.fixture, Fixture::Normal);
            assert_eq!(s.stop, Stop::Complete);
            assert_eq!(s.terminal.pending(), [None; 2]);
            assert_eq!(s.terminal.model().state().limit, None);
            assert_eq!(s.terminal.model().cells()[0], Cell::Single('A'));
            assert!(s.terminal.model().cells()[50 * COLS..]
                .iter()
                .all(|c| *c == BLANK));
        }
    }
}
