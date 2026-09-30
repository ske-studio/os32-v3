//! Owns the *external* backing borrow, never a buffer inside this struct.
use crate::sink::Record;
use crate::state::{Display, Fixture, Movement};
use core::{marker::PhantomData, ptr::NonNull};
use libos32term::model::{Cell, Error};

/// 1 画面ぶんを畳んでもなお入らないときの打ち切り。`Error::TooWide` のように
/// 畳んでも直らない失敗で無限に回らないための保険 (通常は 0 か 1 回)。
const WRAP_LIMIT: u32 = 4;

/// `Session::apply` が 1 本のレコードで起こしたこと。
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Applied {
    /// 画面が埋まって畳んだ回数 (履歴が 1 画面ぶん消えた)。
    pub wraps: u32,
    /// 畳んでも置けずに捨てた文字数。
    pub lost: u32,
    /// モデルの外を指していて無視した CURSOR の数。
    pub ignored_cursor: u32,
}
pub struct Session<'a> {
    display: Option<Display<'a>>,
    backing: NonNull<[Cell]>,
    borrow: PhantomData<&'a mut [Cell]>,
}
impl<'a> Session<'a> {
    pub fn new(cells: &'a mut [Cell], fixture: Fixture) -> Result<Self, Error> {
        if cells.len() < crate::state::CAPACITY {
            return Err(Error::InsufficientStorage);
        }
        let backing = NonNull::from(cells);
        let mut session = Self {
            display: None,
            backing,
            borrow: PhantomData,
        };
        session.select(fixture)?;
        Ok(session)
    }
    // No mutable Display reference or owned Terminal can escape this wrapper.
    pub fn display(&self) -> &Display<'_> {
        self.display.as_ref().unwrap()
    }
    pub fn move_top(&mut self, movement: Movement, rows: usize) {
        self.display.as_mut().unwrap().move_top(movement, rows);
    }
    /// con_sink のレコード 1 本を画面へ適用する (票 K6C-A §2-1)。
    ///
    /// T4 モデルは固定 64 行でスクロールしない。端末として使う以上、画面が
    /// 埋まったところで黙って止まるわけにはいかないので、**埋まったら畳んで
    /// 続きを入れる** (CLEAR と同じ経路)。畳んだ回数は呼び側へ返して状態行に
    /// 出す — 消えた履歴を黙って無かったことにはしない。
    pub fn apply(&mut self, record: Record<'_>) -> Result<Applied, Error> {
        let mut out = Applied::default();
        match record {
            Record::Clear => self.select(Fixture::Live)?,
            /* 票 T7 E1 の EXIT は画面を持たない。モード (接続 → プロンプト)
             * は guest.rs が決めるので、ここでは何もしない — 出力領域に
             * 勝手な行を足さない。 */
            Record::Exit(_) => {}
            Record::Cursor { x, y } => {
                if !self.live_mut().place_cursor(x as usize, y as usize) {
                    out.ignored_cursor += 1;
                }
            }
            Record::Print { bytes, .. } => {
                /* 色は T4 モデルが属性を持たないので落とす (票 §2-1: 写せる
                 * 範囲で、無ければ無視)。 */
                let mut rest = bytes;
                loop {
                    let feed = self.live_mut().feed_live(rest);
                    if feed.error.is_none() {
                        break;
                    }
                    if out.wraps >= WRAP_LIMIT {
                        /* 畳んでも直らない失敗。残りは捨てて前へ進む。 */
                        out.lost +=
                            feed.rest.len() as u32 + feed.pending.iter().flatten().count() as u32;
                        break;
                    }
                    out.wraps += 1;
                    let pending = feed.pending;
                    rest = feed.rest;
                    self.select(Fixture::Live)?;
                    /* 復号は済んで書けなかった文字を先に戻す。ここを生バイトへ
                     * 巻き戻すと多バイト文字を割ってしまう。 */
                    for ch in pending.iter().flatten() {
                        if self.live_mut().place(*ch).is_err() {
                            out.lost += 1;
                        }
                    }
                    if rest.is_empty() {
                        break;
                    }
                }
            }
        }
        Ok(out)
    }

    fn live_mut(&mut self) -> &mut Display<'a> {
        // The &mut Display never leaves this module (see select()'s contract).
        self.display.as_mut().unwrap()
    }

    pub fn select(&mut self, fixture: Fixture) -> Result<(), Error> {
        self.display = None;
        // SAFETY: backing came from the sole &'a mut external slice in new().
        // The original reference is never reused. All Display access is shared
        // and tied to &self, so &mut self excludes outstanding display readers.
        // No Terminal / mutable Display escapes; the previous one was dropped
        // above. The slice stays alive for 'a via the exclusive PhantomData
        // borrow. This is a reborrow of external storage, not lifetime extension
        // of a field in a movable/self-referential struct.
        let cells = unsafe { &mut *self.backing.as_ptr() };
        self.display = Some(Display::load(cells, fixture)?);
        Ok(())
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::{Stop, CAPACITY};
    use libos32term::model::BLANK;
    #[test]
    fn switching_drops_pending_limit_top_and_storage_contents() {
        let mut cells = [BLANK; CAPACITY];
        let mut session = Session::new(&mut cells, Fixture::Full).unwrap();
        session.move_top(Movement::Last, 10);
        session.select(Fixture::Normal).unwrap();
        assert_eq!(session.display().fixture, Fixture::Normal);
        assert_eq!(session.display().top, 0);
        assert_eq!(session.display().stop, Stop::Complete);
        assert_eq!(session.display().terminal.pending(), [None; 2]);
        assert_eq!(session.display().terminal.model().state().limit, None);
        assert_eq!(
            session.display().terminal.model().cells()[0],
            Cell::Single('A')
        );
        for _ in 0..20 {
            for f in [Fixture::Finish, Fixture::TooWide, Fixture::Normal] {
                session.select(f).unwrap();
                assert_eq!(session.display().fixture, f);
            }
        }
        assert_eq!(session.display().terminal.pending(), [None; 2]);
        assert_eq!(session.display().consumed, 229);
    }

    fn print(bytes: &[u8]) -> Record<'_> {
        Record::Print { color: 0xE1, bytes }
    }
    fn row(session: &Session<'_>, y: usize) -> Vec<Cell> {
        let cells = session.display().terminal.model().cells();
        cells[y * crate::state::COLS..(y + 1) * crate::state::COLS].to_vec()
    }

    #[test]
    fn live_applies_print_clear_and_cursor() {
        let mut cells = [BLANK; CAPACITY];
        let mut session = Session::new(&mut cells, Fixture::Live).unwrap();
        /* 空で始まる。 */
        assert_eq!(session.display().terminal.model().cells()[0], BLANK);
        assert_eq!(
            session.display().terminal.model().state().retained_rows.end,
            1
        );

        /* PRINT: 改行も 3 バイト文字もモデル任せで通る。 */
        assert_eq!(
            session.apply(print("ab\n".as_bytes())).unwrap(),
            Applied::default()
        );
        assert_eq!(
            session.apply(print("日x".as_bytes())).unwrap(),
            Applied::default()
        );
        assert_eq!(
            row(&session, 0)[0..2],
            [Cell::Single('a'), Cell::Single('b')]
        );
        assert_eq!(
            row(&session, 1)[0..3],
            [Cell::Wide('日'), Cell::Continuation, Cell::Single('x')]
        );

        /* CURSOR: 既に描いた行の中なら効く。 */
        assert_eq!(
            session.apply(Record::Cursor { x: 1, y: 0 }).unwrap(),
            Applied::default()
        );
        session.apply(print(b"Z")).unwrap();
        assert_eq!(
            row(&session, 0)[0..2],
            [Cell::Single('a'), Cell::Single('Z')]
        );

        /* CLEAR: 画面も履歴もカーソルも戻る。 */
        assert_eq!(session.apply(Record::Clear).unwrap(), Applied::default());
        assert!(session
            .display()
            .terminal
            .model()
            .cells()
            .iter()
            .all(|c| *c == BLANK));
        assert_eq!(
            session.display().terminal.model().state().retained_rows.end,
            1
        );
        assert_eq!(session.display().terminal.model().state().cursor, (0, 0));
    }

    #[test]
    fn cursor_outside_the_model_is_ignored_not_clamped() {
        let mut cells = [BLANK; CAPACITY];
        let mut session = Session::new(&mut cells, Fixture::Live).unwrap();
        session.apply(print(b"ab\ncd")).unwrap();
        let before = session.display().terminal.model().state().cursor;
        for bad in [
            Record::Cursor { x: 0, y: 200 }, /* まだ無い行 */
            Record::Cursor { x: 79, y: 0 },  /* 80 桁の端末座標 */
        ] {
            let a = session.apply(bad).unwrap();
            assert_eq!(a.ignored_cursor, 1);
        }
        assert_eq!(session.display().terminal.model().state().cursor, before);
        /* 無視しても以後の出力は続く。 */
        session.apply(print(b"!")).unwrap();
        assert_eq!(row(&session, 1)[2], Cell::Single('!'));
    }

    #[test]
    fn a_full_screen_wraps_instead_of_freezing() {
        let mut cells = [BLANK; CAPACITY];
        let mut session = Session::new(&mut cells, Fixture::Live).unwrap();
        /* ROWS 本の改行でちょうど底を突く。畳んで続きを入れること。 */
        let feed: Vec<u8> = core::iter::repeat(b'\n').take(crate::state::ROWS).collect();
        let a = session.apply(print(&feed)).unwrap();
        assert_eq!(a.wraps, 1);
        assert_eq!(a.lost, 0);
        /* 畳んだあと、書けなかった改行が戻っているので 2 行目に居る。 */
        assert_eq!(session.display().terminal.model().state().cursor, (0, 1));
        assert_eq!(
            session.display().terminal.model().state().retained_rows.end,
            2
        );
        session.apply(print(b"AFTER")).unwrap();
        assert_eq!(row(&session, 1)[0], Cell::Single('A'));
        /* 止まっていない: さらに流し込んでも進み続ける。 */
        let mut wraps = 0;
        for _ in 0..4 {
            wraps += session.apply(print(&feed)).unwrap().wraps;
        }
        assert_eq!(wraps, 4);
        let y = session.display().terminal.model().state().cursor.1;
        session.apply(print(b"LAST")).unwrap();
        assert_eq!(row(&session, y)[0], Cell::Single('L'));
    }

    #[test]
    fn a_wedged_fixture_model_recovers_into_live_on_the_first_record() {
        /* TooWide fixture は 1 桁のモデルで全角に詰まったまま (pending 保持)。
         * sink のレコードが来たら畳んで Live のモデルへ移り、詰まっていた
         * 文字ごと描き直すこと — 端末が「開いた瞬間から死んでいる」を防ぐ。 */
        let mut cells = [BLANK; CAPACITY];
        let mut session = Session::new(&mut cells, Fixture::TooWide).unwrap();
        assert_eq!(session.display().terminal.pending(), [Some('日'), None]);
        let a = session.apply(print("あい".as_bytes())).unwrap();
        assert_eq!(a.wraps, 1);
        assert_eq!(a.lost, 0);
        assert_eq!(session.display().fixture, Fixture::Live);
        assert_eq!(
            row(&session, 0)[0..6],
            [
                Cell::Wide('日'),
                Cell::Continuation,
                Cell::Wide('あ'),
                Cell::Continuation,
                Cell::Wide('い'),
                Cell::Continuation,
            ]
        );
        /* WRAP_LIMIT は「畳んでも直らない」ときの保険。1 本のレコードで
         * それを超えて回ることはない。 */
        assert!(a.wraps < WRAP_LIMIT);
    }
}
