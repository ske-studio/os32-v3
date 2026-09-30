//! edit_gui の本文 (`doc.rs`) をホストで踏む (票 受入 E8)。
//!
//! **実物のソースをそのまま**取り込む。ゲストもエミュレータも要らない —
//! 挿入・削除・改行・折り返しの桁・見える範囲の切り出しが壊れていたら、
//! GUI で触っても意味がないので、ここが先に落ちる。
//!
//! 保存は KAPI の `sys_open` / `write` / `close` を差し替えて踏む
//! (filer の `host/model_tests.rs` と同じ手)。**書けなかったのに成功と
//! 答えない**ことがここの中心 (受入 E7 / [V4])。
use crate::doc::{self, Doc, VRow};
use std::sync::Mutex;

/* ================================================================ */
/*  贋 KAPI (保存の経路だけ)                                         */
/* ================================================================ */

pub static WRITTEN: Mutex<Vec<u8>> = Mutex::new(Vec::new());
pub static CLOSED: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// 次の `sys_open` が返す値 (負ならエラー)。
pub static OPEN_RET: Mutex<i32> = Mutex::new(7);
/// `sys_write` の振る舞い。
/// 0 = そのまま / 1 = **本文の行だけ** 1 バイト少なく返す / 2 = -1 /
/// 3 = **改行だけ** 0 バイトで返す。
///
/// 1 と 3 を分けているのは、行の短い write と改行の短い write が
/// **別々の判定**で、片方を外したのを他方が隠してしまうため
/// (実際に変異 `short_write_reported_as_ok` がそれで GREEN のままだった)。
pub static WRITE_MODE: Mutex<i32> = Mutex::new(0);
/// `sys_read` が返す本文。
pub static READ_DATA: Mutex<Vec<u8>> = Mutex::new(Vec::new());
pub static READ_POS: Mutex<usize> = Mutex::new(0);

fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

unsafe extern "C" fn m_open(_path: *const u8, _mode: i32) -> i32 {
    *lock(&OPEN_RET)
}
unsafe extern "C" fn m_close(fd: i32) {
    lock(&CLOSED).push(fd);
}
unsafe extern "C" fn m_write(_fd: i32, buf: *const u8, size: u32) -> i32 {
    let mode = *lock(&WRITE_MODE);
    if mode == 2 {
        return -1;
    }
    let n = if mode == 1 && size > 1 {
        size - 1
    } else if mode == 3 && size == 1 {
        0
    } else {
        size
    };
    let mut w = lock(&WRITTEN);
    for i in 0..n {
        w.push(unsafe { *buf.add(i as usize) });
    }
    n as i32
}
unsafe extern "C" fn m_read(_fd: i32, buf: *mut u8, size: u32) -> i32 {
    let data = lock(&READ_DATA);
    let mut pos = lock(&READ_POS);
    let left = data.len() - *pos;
    let n = if (size as usize) < left { size as usize } else { left };
    for i in 0..n {
        unsafe { *buf.add(i) = data[*pos + i] };
    }
    *pos += n;
    n as i32
}

fn setup() {
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| {
        os32api::os32_init(Box::into_raw(Box::new(os32api::mock_api())));
    });
    lock(&WRITTEN).clear();
    lock(&CLOSED).clear();
    *lock(&OPEN_RET) = 7;
    *lock(&WRITE_MODE) = 0;
    lock(&READ_DATA).clear();
    *lock(&READ_POS) = 0;
    unsafe {
        let a = &mut *os32api::api_ptr();
        a.sys_open = m_open;
        a.sys_close = m_close;
        a.sys_write = m_write;
        a.sys_read = m_read;
    }
}

fn newdoc() -> Box<Doc> {
    let mut d = Box::new(Doc::NEW);
    d.reset();
    d
}

fn typed(d: &mut Doc, s: &str) {
    assert!(d.insert(s.as_bytes()), "入らなかった: {:?}", s);
}

/* ================================================================ */
/*  読み込み                                                         */
/* ================================================================ */

#[test]
fn load_splits_on_lf_and_crlf() {
    let mut d = newdoc();
    d.load(b"one\ntwo\r\nthree");
    assert_eq!(d.nlines, 3);
    assert_eq!(d.line(0), b"one");
    assert_eq!(d.line(1), b"two");
    assert_eq!(d.line(2), b"three");
    assert_eq!(d.truncated, 0);
}

#[test]
fn load_keeps_kanji_whole() {
    let mut d = newdoc();
    d.load("あい\nう".as_bytes());
    assert_eq!(d.line(0), "あい".as_bytes());
    assert_eq!(d.line(1), "う".as_bytes());
}

#[test]
fn load_says_when_it_dropped_something() {
    /* [V4]: 捨てたなら黙らない。 */
    let mut d = newdoc();
    let mut big = Vec::new();
    for _ in 0..(doc::MAX_LINES + 20) {
        big.extend_from_slice(b"x\n");
    }
    d.load(&big);
    assert!(d.truncated & doc::TRUNC_LINES != 0, "行数の打ち切りを言わない");
    assert_eq!(d.nlines, doc::MAX_LINES);

    let mut d = newdoc();
    let long = vec![b'y'; doc::LINE_CAP + 50];
    d.load(&long);
    assert!(d.truncated & doc::TRUNC_COLS != 0, "行の長さの打ち切りを言わない");
    assert_eq!(d.line(0).len(), doc::LINE_CAP);
}

/* ================================================================ */
/*  編集                                                            */
/* ================================================================ */

#[test]
fn insert_then_backspace_handles_kanji_as_one_char() {
    let mut d = newdoc();
    typed(&mut d, "aあ");
    assert_eq!(d.line(0), "aあ".as_bytes());
    assert_eq!(d.cx, 4);
    assert!(d.backspace());
    assert_eq!(d.line(0), b"a", "3 バイトまとめて消える");
    assert_eq!(d.cx, 1);
}

#[test]
fn newline_splits_and_backspace_joins() {
    let mut d = newdoc();
    typed(&mut d, "abcd");
    d.cx = 2;
    assert!(d.newline());
    assert_eq!(d.nlines, 2);
    assert_eq!(d.line(0), b"ab");
    assert_eq!(d.line(1), b"cd");
    assert_eq!((d.cy, d.cx), (1, 0));
    assert!(d.backspace(), "行頭の BS は前の行と繋ぐ");
    assert_eq!(d.nlines, 1);
    assert_eq!(d.line(0), b"abcd");
    assert_eq!((d.cy, d.cx), (0, 2));
}

#[test]
fn delete_at_end_of_line_joins_the_next() {
    let mut d = newdoc();
    d.load(b"ab\ncd");
    d.cy = 0;
    d.move_end();
    assert!(d.delete());
    assert_eq!(d.nlines, 1);
    assert_eq!(d.line(0), b"abcd");
}

#[test]
fn insert_of_a_newline_byte_splits_the_line() {
    /* 貼り付け (クリップボード) は改行を含む。 */
    let mut d = newdoc();
    typed(&mut d, "ab\ncd");
    assert_eq!(d.nlines, 2);
    assert_eq!(d.line(0), b"ab");
    assert_eq!(d.line(1), b"cd");
}

#[test]
fn caret_stays_on_a_utf8_boundary_when_moving_between_lines() {
    let mut d = newdoc();
    d.load("あいう\nx".as_bytes());
    d.cy = 0;
    d.move_end();
    assert_eq!(d.cx, 9);
    d.move_down();
    assert_eq!((d.cy, d.cx), (1, 1), "短い行では末尾に収まる");
    d.move_up();
    /* 行を戻ってもキャレットは継続バイトの上に乗らない。 */
    let line = d.line(d.cy).to_vec();
    assert!(
        d.cx == line.len() || (line[d.cx] & 0xC0) != 0x80,
        "継続バイトの上にキャレットが乗った (cx={})",
        d.cx
    );
}

#[test]
fn edits_mark_the_document_dirty() {
    let mut d = newdoc();
    assert!(!d.dirty);
    typed(&mut d, "a");
    assert!(d.dirty, "打ったのに dirty が立たない (E6 が黙って捨てる)");
}

/* ================================================================ */
/*  折り返しと「見える範囲」 (票 §2 の中心 / 受入 E2・E3)             */
/* ================================================================ */

fn rows_of(d: &Doc, cols: usize, top: usize, n: usize) -> Vec<VRow> {
    let mut out = vec![VRow::EMPTY; n];
    let k = d.fill_view(cols, top, &mut out);
    out.truncate(k);
    out
}

#[test]
fn a_logical_line_is_covered_exactly_once_by_its_visual_rows() {
    let mut d = newdoc();
    d.load("aあbいc漢字de\nshort\n\nあいうえおかきくけこ".as_bytes());
    for cols in 1..=12usize {
        let total = d.vrow_total(cols);
        let rows = rows_of(&d, cols, 0, total + 4);
        assert_eq!(rows.len(), total, "cols={} で本数が合わない", cols);
        let mut li = 0usize;
        let mut at = 0usize;
        for r in rows.iter() {
            if r.line != li {
                assert_eq!(r.line, li + 1, "cols={} で論理行が飛んだ", cols);
                assert_eq!(at, d.line(li).len(), "cols={} で行末まで出していない", cols);
                li = r.line;
                at = 0;
            }
            assert_eq!(r.start, at, "cols={} で行内に隙間 / 重なり", cols);
            let s = d.line(r.line);
            assert!(
                r.start == s.len() || (s[r.start] & 0xC0) != 0x80,
                "cols={} で UTF-8 の途中から始まった",
                cols
            );
            assert!(
                r.end == s.len() || (s[r.end] & 0xC0) != 0x80,
                "cols={} で UTF-8 の途中で切った",
                cols
            );
            at = r.end;
        }
        assert_eq!(li, d.nlines - 1, "cols={} で最後の行まで届かない", cols);
        assert_eq!(at, d.line(li).len());
    }
}

#[test]
fn fill_view_from_top_matches_vrow_at() {
    let mut d = newdoc();
    d.load("aあbいc漢字de\nshort\n\nあいうえおかきくけこ".as_bytes());
    let cols = 5usize;
    let total = d.vrow_total(cols);
    for top in 0..total {
        let rows = rows_of(&d, cols, top, 4);
        for (k, r) in rows.iter().enumerate() {
            assert_eq!(
                Some(*r),
                d.vrow_at(cols, top + k),
                "top={} k={} で見える範囲がずれた",
                top,
                k
            );
        }
    }
}

#[test]
fn fill_view_does_not_write_more_than_the_widget_can_hold() {
    let mut d = newdoc();
    let mut big = String::new();
    for i in 0..100 {
        big.push_str(&format!("line {}\n", i));
    }
    d.load(big.as_bytes());
    let mut out = [VRow::EMPTY; 8];
    let n = d.fill_view(40, 0, &mut out);
    assert_eq!(n, 8, "1 画面に入らない本文でも渡すのは 8 本まで");
}

#[test]
fn caret_row_is_inside_the_view_after_scrolling() {
    let mut d = newdoc();
    let mut big = String::new();
    for i in 0..80 {
        big.push_str(&format!("l{}\n", i));
    }
    d.load(big.as_bytes());
    let (cols, height) = (40usize, 10usize);
    for cy in [0usize, 5, 40, 79] {
        d.cy = cy;
        d.cx = 0;
        let top = d.scroll_to_caret(cols, height, 0);
        let c = d.caret_vrow(cols);
        assert!(c >= top && c < top + height, "cy={} が見える範囲の外", cy);
        let rows = rows_of(&d, cols, top, height);
        assert_eq!(rows[c - top].line, cy, "キャレット行が別の論理行を指した");
    }
}

#[test]
fn caret_vrow_follows_the_wrap() {
    let mut d = newdoc();
    d.load("あいうえお".as_bytes()); /* 10 桁 */
    let cols = 4usize; /* 2 文字 / 行 → 3 本 */
    assert_eq!(d.vrow_total(cols), 3);
    d.cy = 0;
    d.cx = 0;
    assert_eq!(d.caret_vrow(cols), 0);
    d.cx = 6; /* う の手前 = 2 本目の頭 */
    assert_eq!(d.caret_vrow(cols), 1);
    d.move_end();
    assert_eq!(d.caret_vrow(cols), 2);
}

/* ================================================================ */
/*  保存 — **書けなかったのに成功と答えない** (受入 E7 / [V4])        */
/* ================================================================ */

#[test]
fn save_writes_every_line_with_a_newline() {
    setup();
    let mut d = newdoc();
    d.load("ab\nあ\n".as_bytes());
    let r = doc::save_file(&d, b"/tmp/x\0");
    assert!(r > 0, "成功しているのに負を返した: {}", r);
    /* 末尾の改行は最後の行の終端。空行を 1 本足さない (穴 H16)。 */
    assert_eq!(&*lock(&WRITTEN), "ab\nあ\n".as_bytes());
    assert_eq!(r as usize, lock(&WRITTEN).len());
    assert_eq!(&*lock(&CLOSED), &[7], "fd を閉じていない");
}

/// 穴 H16 (2026-09-18 にゲストで実測): **開いて保存するだけでファイルが伸びた。**
///
/// `load` が末尾の改行を空行として数え、`save_file` がその空行にも改行を書くため、
/// 1 往復ごとに 1 バイト増えていた (275 → 270 → 271…)。**編集していなくても増える。**
/// 人が使う経路 (開く → 保存する) を通して初めて出た穴で、
/// 行の内容だけを見ていた `load_file_round_trips` では捕まらなかった。
#[test]
fn load_then_save_is_byte_identical() {
    for src in ["ab\nあ\n", "a\n\n", "x\n", "一\n二\n三\n"] {
        setup();
        let mut d = newdoc();
        d.load(src.as_bytes());
        let r = doc::save_file(&d, b"/tmp/x\0");
        assert!(r > 0, "{:?}: 成功しているのに負を返した: {}", src, r);
        assert_eq!(
            &*lock(&WRITTEN),
            src.as_bytes(),
            "{:?}: 開いて保存しただけで中身が変わった (穴 H16)",
            src
        );
    }
}

/// 何度往復しても伸びないこと (H16 の本体: 単調増加だった)。
#[test]
fn saving_over_and_over_does_not_grow_the_file() {
    let mut cur = "ab\nあ\n".as_bytes().to_vec();
    for round in 0..4 {
        setup();
        let mut d = newdoc();
        d.load(&cur);
        let r = doc::save_file(&d, b"/tmp/x\0");
        assert!(r > 0);
        let out = lock(&WRITTEN).clone();
        assert_eq!(
            out.len(),
            cur.len(),
            "{} 回目の往復で長さが変わった (穴 H16)",
            round + 1
        );
        cur = out;
    }
}

/// 末尾に改行の無いファイルは 1 本足して正規化する (これは仕様)。
#[test]
fn a_file_without_a_trailing_newline_gets_one() {
    setup();
    let mut d = newdoc();
    d.load(b"ab");
    let r = doc::save_file(&d, b"/tmp/x\0");
    assert!(r > 0);
    assert_eq!(&*lock(&WRITTEN), b"ab\n");
}

/// 末尾の空行が**本物**なら残る (H16 の修正が行を食っていないこと)。
#[test]
fn a_real_blank_last_line_survives() {
    setup();
    let mut d = newdoc();
    d.load(b"a\n\n");
    assert_eq!(d.nlines, 2, "本物の空行まで消した");
    assert_eq!(d.line(1), b"");
}

#[test]
fn save_reports_the_error_when_open_fails() {
    setup();
    *lock(&OPEN_RET) = -2;
    let mut d = newdoc();
    d.load(b"ab");
    let r = doc::save_file(&d, b"/ro/x\0");
    assert_eq!(r, -2, "開けなかったのに成功と答えた");
    assert!(lock(&WRITTEN).is_empty());
}

#[test]
fn save_fails_on_a_short_line_write() {
    setup();
    *lock(&WRITE_MODE) = 1;
    let mut d = newdoc();
    d.load(b"abcd");
    let r = doc::save_file(&d, b"/full/x\0");
    assert!(r < 0, "**行の短い write を成功と答えた** (E7 / [V4])");
    assert_eq!(&*lock(&CLOSED), &[7], "失敗しても fd は閉じる");
}

#[test]
fn save_fails_on_a_short_newline_write() {
    setup();
    *lock(&WRITE_MODE) = 3;
    let mut d = newdoc();
    d.load(b"abcd");
    let r = doc::save_file(&d, b"/full/x\0");
    assert!(r < 0, "**改行の短い write を成功と答えた** (E7 / [V4])");
    assert_eq!(&*lock(&CLOSED), &[7], "失敗しても fd は閉じる");
}

#[test]
fn save_fails_when_write_returns_an_error() {
    setup();
    *lock(&WRITE_MODE) = 2;
    let mut d = newdoc();
    d.load(b"abcd");
    let r = doc::save_file(&d, b"/ro/x\0");
    assert!(r < 0, "write が -1 なのに成功と答えた");
}

#[test]
fn load_file_reports_an_open_failure() {
    setup();
    *lock(&OPEN_RET) = -2;
    let mut d = newdoc();
    let mut scratch = vec![0u8; 256];
    let r = doc::load_file(&mut d, b"/nope\0", &mut scratch);
    assert_eq!(r, -2, "開けなかったのに 0 バイト読めたと答えた");
}

#[test]
fn load_file_round_trips() {
    setup();
    *lock(&READ_DATA) = "hello\nあ\n".as_bytes().to_vec();
    let mut d = newdoc();
    let mut scratch = vec![0u8; 256];
    let r = doc::load_file(&mut d, b"/x\0", &mut scratch);
    assert_eq!(r, 10);
    assert_eq!(d.line(0), b"hello");
    assert_eq!(d.line(1), "あ".as_bytes());
    assert!(!d.dirty, "読んだ直後に dirty を立てない");
}
