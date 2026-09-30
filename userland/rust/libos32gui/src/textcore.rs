//! textcore.rs — 「3 バイトで 2 桁」を扱う下請け (票 TASK_EDIT_GUI §2 決裁 A1)。
//!
//! `utf8core.rs` と同じ立ち位置で、**os32api にもクレートの他の部分にも依存しない**
//! (core だけ)。理由は 3 つ:
//!
//! 1. `WK_TEXTBOX` (1 行) と `WK_TEXTAREA` (複数行) の重複をここへ寄せる。
//!    決裁 A1 の「重複は共通の下請けに寄せてよい」がこれ。
//! 2. **エディタ (別クレート) も同じ実体を使う**。`libos32gui_stub` が
//!    `#[path]` でこのファイルを取り込むので、桁の数え方と折り返しの位置の
//!    実装は木全体で 1 本しかない。写しを作るとずれる。
//! 3. ホスト TDD (`tools/tests/test_edit_doc.py`) が `#[path]` で直に取り込む。
//!    ここが壊れたら `WK_TEXTBOX` の振る舞いも RED になる (受入 E10)。
//!
//! # 桁の規約 (`draw.rs` の `decode_glyph` と**同じ規則**)
//!
//! - `b < 0x80` … 半角 1 桁 (ANK 8px)
//! - 多バイトの先頭 (`0xC0`/`0xE0`/`0xF0` 系) … 全角 2 桁 (16px)
//! - 継続バイト / 不正バイト … 1 個読み飛ばして半角 1 桁
//!
//! ここを `draw.rs` とずらすと、折り返した桁と実際に描かれる幅が食い違う
//! (日本語で必ず露見する。`docs/POLICY_DEBUG.md` §4-27)。
#![allow(dead_code)]

/// 半角 1 文字の桁数。
pub const COLS_ANK: usize = 1;
/// 全角 1 文字の桁数。
pub const COLS_KANJI: usize = 2;

/// UTF-8 の先頭バイトから符号単位のバイト長を得る (不正バイトは 1)。
///
/// `utf8core::utf8_seq_len` と同じ。あちらは `client`/`hostsvc` 用で
/// **依存を増やさない**ことが目的なので、ここでも独立に持つ (2 行)。
#[inline]
pub fn seq_len(b: u8) -> usize {
    if b < 0x80 {
        1
    } else if b & 0xE0 == 0xC0 {
        2
    } else if b & 0xF0 == 0xE0 {
        3
    } else if b & 0xF8 == 0xF0 {
        4
    } else {
        1
    }
}

/// 先頭バイトから桁数 (`draw.rs` の `decode_glyph` と同じ規則)。
#[inline]
pub fn cols_of(b: u8) -> usize {
    if b < 0x80 {
        COLS_ANK
    } else if b & 0xC0 == 0x80 {
        /* 継続バイト単独 = 不正。描画側も 1 個スキップして半角扱い。 */
        COLS_ANK
    } else {
        COLS_KANJI
    }
}

/// `at` の位置から 1 文字**進んだ** UTF-8 境界 (末尾を越えない)。
pub fn next_boundary(s: &[u8], at: usize) -> usize {
    let n = s.len();
    if at >= n {
        return n;
    }
    let q = at + seq_len(s[at]);
    if q > n {
        n
    } else {
        q
    }
}

/// `at` の位置から 1 文字**戻った** UTF-8 境界 (0 を下回らない)。
pub fn prev_boundary(s: &[u8], at: usize) -> usize {
    let mut p = if at > s.len() { s.len() } else { at };
    while p > 0 {
        p -= 1;
        if (s[p] & 0xC0) != 0x80 {
            break;
        }
    }
    p
}

/// `s` の表示桁数 (半角 1 / 全角 2)。
pub fn columns(s: &[u8]) -> usize {
    let mut c = 0usize;
    let mut i = 0usize;
    while i < s.len() {
        c += cols_of(s[i]);
        i += seq_len(s[i]).max(1);
    }
    c
}

/// `s` の先頭から `cols` 桁に収まる**最大の UTF-8 境界**を返す。
///
/// **これがこの票の中心**。桁で切るときに符号単位の途中で切ると壊れる
/// (`docs/POLICY_DEBUG.md` §4-27)。全角は 2 桁なので、残り 1 桁のところへは
/// 入れず、その文字はまるごと次の行へ送る。
///
/// `cols == 0` は 0 を返す (無限ループを作らないのは呼び手の責任だが、
/// [`wrap_rows`] は自分で 1 桁以上を保証する)。
pub fn wrap_end(s: &[u8], cols: usize) -> usize {
    let mut used = 0usize;
    let mut i = 0usize;
    while i < s.len() {
        let w = cols_of(s[i]);
        if used + w > cols {
            break;
        }
        let step = seq_len(s[i]);
        let q = i + step;
        if q > s.len() {
            break; /* 末尾の不完全シーケンス: 境界で止める */
        }
        used += w;
        i = q;
    }
    i
}

/// 論理行 1 本を `cols` 桁で折り返したときの**区切り位置の列**。
///
/// `out[k]` に k 番目の見える行の [開始, 終了) を書き、書いた本数を返す。
/// 空行も 1 本 (0,0) を返す — 画面から行が消えると行番号がずれて見える。
///
/// `cols` が 0 なら 1 桁として扱う (前に進まない折り返しは作らない)。
pub fn wrap_rows(s: &[u8], cols: usize, out: &mut [(usize, usize)]) -> usize {
    let cols = if cols == 0 { 1 } else { cols };
    let mut n = 0usize;
    let mut at = 0usize;
    loop {
        if n >= out.len() {
            return n;
        }
        let end = at + wrap_end(&s[at..], cols);
        /* 1 文字も入らない (幅より広い文字) ときは 1 文字だけ載せて必ず進む。 */
        let end = if end == at && at < s.len() {
            next_boundary(s, at)
        } else {
            end
        };
        out[n] = (at, end);
        n += 1;
        at = end;
        if at >= s.len() {
            return n;
        }
    }
}

/// `cols` 桁で折り返したときの見える行数 (`wrap_rows` と同じ規則、数えるだけ)。
pub fn wrap_count(s: &[u8], cols: usize) -> usize {
    let cols = if cols == 0 { 1 } else { cols };
    if s.is_empty() {
        return 1;
    }
    let mut n = 0usize;
    let mut at = 0usize;
    while at < s.len() {
        let e = at + wrap_end(&s[at..], cols);
        let e = if e == at { next_boundary(s, at) } else { e };
        at = e;
        n += 1;
    }
    n
}

/* ================================================================ */
/*  バイト列の編集 (WK_TEXTBOX と エディタの本文で共用)               */
/* ================================================================ */

/// `buf[..len]` の `at` に `seq` を差し込む。入らなければ `None` (buf は無変更)。
pub fn insert(buf: &mut [u8], len: usize, at: usize, seq: &[u8]) -> Option<usize> {
    if len > buf.len() || at > len {
        return None;
    }
    let k = seq.len();
    if len + k > buf.len() {
        return None;
    }
    let mut i = len;
    while i > at {
        buf[i + k - 1] = buf[i - 1];
        i -= 1;
    }
    let mut j = 0usize;
    while j < k {
        buf[at + j] = seq[j];
        j += 1;
    }
    Some(len + k)
}

/// `buf[..len]` の `at` から `n` バイト消す。戻りは新しい長さ。
pub fn remove(buf: &mut [u8], len: usize, at: usize, n: usize) -> usize {
    if at >= len || n == 0 || len > buf.len() {
        return len;
    }
    let end = if at + n > len { len } else { at + n };
    let k = end - at;
    let mut i = at;
    while i + k < len {
        buf[i] = buf[i + k];
        i += 1;
    }
    len - k
}
