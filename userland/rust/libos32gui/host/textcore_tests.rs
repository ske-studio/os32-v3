//! textcore をホストで踏む。**実物のソースをそのまま**取り込む (写さない)。
//!
//! ここが守っているのは 2 つ:
//!
//! 1. **桁で切るときに UTF-8 の途中で切らない** (`docs/POLICY_DEBUG.md` §4-27)。
//!    日本語は 3 バイトで 2 桁。エディタの折り返しはここを通る。
//! 2. **`WK_TEXTBOX` の振る舞いが変わっていない** (票 受入 E10)。
//!    決裁 A1 で「重複は共通の下請けに寄せてよい」とした結果、textbox の
//!    バイト操作は `textcore::insert` / `remove` / `*_boundary` に乗った。
//!    だからここを壊すと textbox も壊れる — その変異が RED になることで
//!    E10 を押さえる ([`tb_model`] が textbox と同じ手順を踏む)。
use crate::textcore as tc;

/* 実物の `widget.rs` の textbox が持てるバイト数 (uistate::TEXT_CAP)。 */
const TEXT_CAP: usize = 64;

const A: &[u8] = "あ".as_bytes(); /* E3 81 82 — 3B / 2 桁 */
const I: &[u8] = "い".as_bytes();

fn is_boundary(s: &[u8], at: usize) -> bool {
    at == 0 || at == s.len() || (s[at] & 0xC0) != 0x80
}

/* ================================================================ */
/*  桁の数え方                                                       */
/* ================================================================ */

#[test]
fn ank_is_one_column_kanji_is_two() {
    assert_eq!(tc::cols_of(b'a'), 1);
    assert_eq!(tc::cols_of(A[0]), 2, "全角の先頭バイトは 2 桁");
    /* 継続バイト単独は draw.rs の decode_glyph と同じく半角 1 桁扱い。 */
    assert_eq!(tc::cols_of(A[1]), 1);
    assert_eq!(tc::columns(b"abc"), 3);
    assert_eq!(tc::columns("あい".as_bytes()), 4);
    assert_eq!(tc::columns("aあ".as_bytes()), 3);
}

#[test]
fn seq_len_matches_utf8() {
    assert_eq!(tc::seq_len(b'a'), 1);
    assert_eq!(tc::seq_len(A[0]), 3);
    assert_eq!(tc::seq_len("é".as_bytes()[0]), 2);
    assert_eq!(tc::seq_len("𠮷".as_bytes()[0]), 4);
}

/* ================================================================ */
/*  折り返し — **UTF-8 の途中で切らない** (票 受入 E3)                */
/* ================================================================ */

#[test]
fn wrap_end_never_splits_a_sequence() {
    let s = "aあbいc漢字dえ".as_bytes();
    for cols in 0..=24usize {
        let e = tc::wrap_end(s, cols);
        assert!(
            is_boundary(s, e),
            "cols={} で UTF-8 の途中 (offset {}) を返した",
            cols,
            e
        );
        assert!(tc::columns(&s[..e]) <= cols, "cols={} を超えて詰めた", cols);
    }
}

#[test]
fn wrap_end_does_not_squeeze_a_wide_char_into_one_column() {
    /* 残り 1 桁のところへ全角は入らない。半端に入れると描画で 1 桁はみ出る。 */
    assert_eq!(tc::wrap_end(A, 1), 0);
    assert_eq!(tc::wrap_end(A, 2), 3);
    let s = "aあ".as_bytes();
    assert_eq!(tc::wrap_end(s, 2), 1, "a だけ入って あ は次の行へ");
    assert_eq!(tc::wrap_end(s, 3), 4);
}

#[test]
fn wrap_rows_covers_the_line_exactly_once() {
    let cases: [&str; 5] = ["", "abc", "あいうえお", "aあiい漢", "aaaaaaaaaaaa"];
    for text in cases.iter() {
        let s = text.as_bytes();
        for cols in 1..=10usize {
            let mut out = [(0usize, 0usize); 64];
            let n = tc::wrap_rows(s, cols, &mut out);
            assert!(n >= 1, "空行でも 1 本返す ({:?}, cols={})", text, cols);
            assert_eq!(out[0].0, 0, "先頭から始まる");
            assert_eq!(out[n - 1].1, s.len(), "末尾まで届く ({:?})", text);
            let mut k = 0;
            while k < n {
                let (a, b) = out[k];
                assert!(is_boundary(s, a) && is_boundary(s, b), "境界で切る");
                /* 桁を超えてよいのは「幅より広い 1 文字」だけ。それ以外で
                 * 超えたら折り返しが桁を見ていない。1 文字も載せずに進まない
                 * 行を作ると無限ループになるので、この 1 件だけは許す。 */
                if tc::columns(&s[a..b]) > cols {
                    assert_eq!(
                        b,
                        tc::next_boundary(s, a),
                        "{:?} cols={} で 2 文字以上を桁の外へ押し込んだ",
                        text,
                        cols
                    );
                }
                if k + 1 < n {
                    /* 隙間も重なりも作らない (**行が飛ぶ / 重なるの検出**)。 */
                    assert_eq!(b, out[k + 1].0, "{:?} cols={} で行が飛んだ", text, cols);
                    assert!(b > a, "進まない行を作った (無限ループの種)");
                }
                k += 1;
            }
            assert_eq!(tc::wrap_count(s, cols), n, "数えるだけの版とずれた");
        }
    }
}

#[test]
fn wrap_count_of_empty_line_is_one() {
    assert_eq!(tc::wrap_count(b"", 10), 1);
    assert_eq!(tc::wrap_count(b"", 0), 1);
}

/* ================================================================ */
/*  バイト操作                                                       */
/* ================================================================ */

#[test]
fn insert_refuses_to_overflow() {
    let mut buf = [0u8; 8];
    let len = tc::insert(&mut buf, 0, 0, b"abcdef").unwrap();
    assert_eq!(len, 6);
    assert!(
        tc::insert(&mut buf, len, 6, A).is_none(),
        "8B の箱に 6B + 3B は入らない — **入らないなら入れない**"
    );
    assert_eq!(&buf[..6], b"abcdef", "断ったのに書き換えた");
}

#[test]
fn insert_and_remove_are_inverses() {
    let mut buf = [0u8; 32];
    let len = tc::insert(&mut buf, 0, 0, b"abc").unwrap();
    let len = tc::insert(&mut buf, len, 1, A).unwrap();
    assert_eq!(&buf[..len], "aあbc".as_bytes());
    let len2 = tc::remove(&mut buf, len, 1, A.len());
    assert_eq!(&buf[..len2], b"abc");
}

#[test]
fn boundaries_step_whole_characters() {
    let s = "aあb".as_bytes(); /* 0:a 1..4:あ 4:b */
    assert_eq!(tc::next_boundary(s, 0), 1);
    assert_eq!(tc::next_boundary(s, 1), 4, "全角は 3B 進む");
    assert_eq!(tc::next_boundary(s, 4), 5);
    assert_eq!(tc::next_boundary(s, 5), 5);
    assert_eq!(tc::prev_boundary(s, 5), 4);
    assert_eq!(tc::prev_boundary(s, 4), 1, "全角は 3B 戻る");
    assert_eq!(tc::prev_boundary(s, 1), 0);
    assert_eq!(tc::prev_boundary(s, 0), 0);
}

/* ================================================================ */
/*  受入 E10 — WK_TEXTBOX (1 行) の振る舞いが変わっていないこと       */
/*                                                                  */
/*  `widget.rs` の `tb_insert` / `tb_remove` / `tb_backspace` /       */
/*  `tb_delete` / `key_textbox` と**同じ手順**をここで踏む。実物は     */
/*  KAPI の描画に依存してホストで動かないので、下請け (textcore) の    */
/*  上に載った振る舞いを固定する。実物がこの下請けを通っていることは   */
/*  試験ドライバ (`tools/tests/test_edit_doc.py`) が静的に見る。       */
/* ================================================================ */

struct TbModel {
    text: [u8; TEXT_CAP],
    len: usize,
    caret: usize,
}

impl TbModel {
    fn new() -> TbModel {
        TbModel { text: [0; TEXT_CAP], len: 0, caret: 0 }
    }
    fn s(&self) -> &[u8] {
        &self.text[..self.len]
    }
    /// `tb_insert` と同じ。
    fn insert(&mut self, seq: &[u8]) -> bool {
        let c = if self.caret > self.len { self.len } else { self.caret };
        match tc::insert(&mut self.text, self.len, c, seq) {
            Some(n) => {
                self.len = n;
                self.caret = c + seq.len();
                true
            }
            None => false,
        }
    }
    /// `tb_backspace` と同じ。
    fn backspace(&mut self) -> bool {
        if self.caret == 0 {
            return false;
        }
        let p = tc::prev_boundary(&self.text[..self.len], self.caret);
        self.len = tc::remove(&mut self.text, self.len, p, self.caret - p);
        self.caret = p;
        true
    }
    /// `tb_delete` と同じ。
    fn delete(&mut self) -> bool {
        if self.caret >= self.len {
            return false;
        }
        let q = tc::next_boundary(&self.text[..self.len], self.caret);
        self.len = tc::remove(&mut self.text, self.len, self.caret, q - self.caret);
        true
    }
    fn left(&mut self) {
        if self.caret > 0 {
            self.caret = tc::prev_boundary(&self.text[..self.len], self.caret);
        }
    }
    fn right(&mut self) {
        if self.caret < self.len {
            self.caret = tc::next_boundary(&self.text[..self.len], self.caret);
        }
    }
}

#[test]
fn textbox_typing_is_unchanged() {
    let mut t = TbModel::new();
    assert!(t.insert(b"a"));
    assert!(t.insert(A));
    assert!(t.insert(b"b"));
    assert_eq!(t.s(), "aあb".as_bytes());
    assert_eq!(t.caret, 5);
}

#[test]
fn textbox_backspace_removes_a_whole_kanji() {
    let mut t = TbModel::new();
    t.insert(b"a");
    t.insert(A);
    assert!(t.backspace());
    assert_eq!(t.s(), b"a", "全角 1 文字 = 3 バイトまとめて消える");
    assert_eq!(t.caret, 1);
    assert!(t.backspace());
    assert_eq!(t.s(), b"");
    assert!(!t.backspace(), "行頭では何も起きない");
}

#[test]
fn textbox_delete_removes_a_whole_kanji() {
    let mut t = TbModel::new();
    t.insert(A);
    t.insert(I);
    t.caret = 0;
    assert!(t.delete());
    assert_eq!(t.s(), I);
    assert!(t.delete());
    assert_eq!(t.s(), b"");
    assert!(!t.delete());
}

#[test]
fn textbox_arrows_land_on_boundaries() {
    let mut t = TbModel::new();
    t.insert("aあb".as_bytes());
    t.caret = 0;
    t.right();
    assert_eq!(t.caret, 1);
    t.right();
    assert_eq!(t.caret, 4, "全角をまたぐ");
    t.left();
    assert_eq!(t.caret, 1);
}

#[test]
fn textbox_stops_at_its_capacity() {
    let mut t = TbModel::new();
    let mut n = 0;
    while t.insert(b"x") {
        n += 1;
        assert!(n <= TEXT_CAP, "上限を超えて入った");
    }
    assert_eq!(t.len, TEXT_CAP, "ちょうど 64B まで入る");
    assert!(!t.insert(A), "**入らないものを入れない** (箱の外へ書かない)");
    assert_eq!(t.len, TEXT_CAP);
}
