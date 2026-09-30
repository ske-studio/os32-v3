//! clipboard.rs — 端末のコピー / 貼り付けの純変換 (票 N4 §2 / §5、N4b)。
//!
//! `inject.rs` と同じく **`no_std` の純関数だけ**を置く。KAPI も GUI も触らない
//! ので、ホスト試験が `host_tests/src/lib.rs` から `#[path]` で取り込める。
//! 実際の `os32gui_clip_get` / `os32gui_clip_put` / `kbd_inject` の呼び出しと
//! 保留貼り付けの状態は `guest.rs`。
//!
//! ## 何をするか
//!
//! - **コピー** (`cells_to_text`): 可視画面のセル (`libos32term::model::Cell`) を
//!   UTF-8 に戻す。Wide は 1 文字、Continuation は飛ばし、行末の空白は落とし、
//!   行は `0x0A` 区切り、NUL / ESC は空白に置換、`out` に入る UTF-8 境界で切る。
//! - **貼り付け (接続モード)** (`attach_transform`): ホストのクリップボードの
//!   バイト列を子へ注ぐ形に直す。`LF (0x0A)` は `CR (0x0D)` に、`TAB` はそのまま、
//!   それ以外の制御バイト (ESC 等) は落とす — 子の暴走を防ぐ (票 §5)。
//! - **貼り付け (プロンプト)** (`first_line`): 最初の `LF` までの 1 行だけを返す
//!   (残りは呼び手が捨てる)。`prompt::Line::push` が制御バイトを落とすので、
//!   複数行はプロンプトに入れない (票 §5 新 6)。
//! - **drip の分割** (`drip_len`): 100ms タイマ 1 周で注ぐバイト数
//!   `min(残り, KBD_INJECT_RING_SIZE − pending)`。

use libos32term::model::Cell;

/// CLIP PUT / 保留貼り付けの上限 (`HOST_CLIP_MAX` と同じ 4096B、票 §2)。
pub const CLIP_MAX: usize = 4096;

/// 注入リングの容量 (`include/kbd_inject.h` の `KBD_INJECT_RING_SIZE`)。
/// ここは写し — ホスト試験から見えないといけないので自前で置く。
pub const INJECT_RING_SIZE: usize = 256;

/* ================================================================ */
/*  コピー: セル → UTF-8                                            */
/* ================================================================ */

/// NUL / ESC を空白に落とす (票 §5: `clip_put` の前に置換)。
fn sanitize(c: char) -> char {
    if c == '\0' || c == '\u{1b}' {
        ' '
    } else {
        c
    }
}

/// セルが「行末の空白」に数えられない = 中身がある。
fn is_blank(cell: Cell) -> bool {
    matches!(cell, Cell::Single(' '))
}

/// `out` に 1 文字を UTF-8 で足す。入り切らなければ **1 バイトも書かず** false
/// (UTF-8 境界を割らない、CLAUDE.md §4-27)。
fn push_char(out: &mut [u8], w: &mut usize, c: char) -> bool {
    let mut tmp = [0u8; 4];
    let s = c.encode_utf8(&mut tmp);
    let b = s.as_bytes();
    if *w + b.len() > out.len() {
        return false;
    }
    out[*w..*w + b.len()].copy_from_slice(b);
    *w += b.len();
    true
}

/// `out` に 1 バイトを足す。入り切らなければ false。
fn push_byte(out: &mut [u8], w: &mut usize, b: u8) -> bool {
    if *w + 1 > out.len() {
        return false;
    }
    out[*w] = b;
    *w += 1;
    true
}

/// 可視画面のセル (`cells`、行優先で 1 行 `cols` セル) を UTF-8 テキストに戻す。
///
/// - `Cell::Single` / `Cell::Wide` は文字を 1 つ出す (Wide も 1 文字ぶん)。
/// - `Cell::Continuation` は飛ばす (Wide の後半)。
/// - 各行の末尾の空白 (`Single(' ')`) は落とす。
/// - 行は `0x0A` で終える。**末尾の空行は出さない** (最後に中身のある行まで)。
/// - `NUL` / `ESC` は空白に置換する。
/// - `out` に入る **UTF-8 境界**で打ち切る (途中の文字は書かない)。
///
/// 戻り = `out` に書いたバイト数。
pub fn cells_to_text(cells: &[Cell], cols: usize, out: &mut [u8]) -> usize {
    let mut w = 0usize;
    if cols == 0 || out.is_empty() {
        return 0;
    }
    let rows = cells.len() / cols;
    /* 末尾の空行は出さない: 中身のある最後の行を探す。 */
    let mut last: Option<usize> = None;
    for y in 0..rows {
        let row = &cells[y * cols..(y + 1) * cols];
        if row.iter().any(|&c| !is_blank(c)) {
            last = Some(y);
        }
    }
    let Some(last) = last else {
        return 0;
    };
    for y in 0..=last {
        let row = &cells[y * cols..(y + 1) * cols];
        /* 行末の空白を落とす: 空白でない最後のセルの次まで。 */
        let mut end = 0usize;
        for (x, &c) in row.iter().enumerate() {
            if !is_blank(c) {
                end = x + 1;
            }
        }
        for &c in &row[..end] {
            match c {
                Cell::Continuation => {}
                Cell::Single(ch) | Cell::Wide(ch) => {
                    if !push_char(out, &mut w, sanitize(ch)) {
                        return w;
                    }
                }
            }
        }
        if !push_byte(out, &mut w, 0x0A) {
            return w;
        }
    }
    w
}

/* ================================================================ */
/*  貼り付け: ホストのバイト列 → 注入 / 行                          */
/* ================================================================ */

/// 接続モードの貼り付け前処理 (票 §2 / §5)。
///
/// `src` を子へ注ぐ形に直して `dst` に詰める:
/// - `LF (0x0A)` → `CR (0x0D)` (`inject::from_key` の Enter と同じ、行が確定する)。
/// - `TAB (0x09)` はそのまま。
/// - それ以外の制御バイト (`< 0x20`、`0x7F`、`CR` の生値を含む) は落とす。
/// - 印字可能バイト (`>= 0x20 && != 0x7F`) はそのまま (UTF-8 の後続バイトも含む)。
///
/// 戻り = `dst` に書いたバイト数 (`dst` が短ければそこで止める)。
pub fn attach_transform(src: &[u8], dst: &mut [u8]) -> usize {
    let mut w = 0usize;
    for &b in src {
        if w >= dst.len() {
            break;
        }
        let out = if b == 0x0A {
            0x0D
        } else if b == 0x09 {
            0x09
        } else if b < 0x20 || b == 0x7F {
            continue;
        } else {
            b
        };
        dst[w] = out;
        w += 1;
    }
    w
}

/// プロンプト貼り付け: 最初の `LF` までの 1 行 (`LF` は含めない)。
/// 残り (2 行目以降) は呼び手が捨てる (票 §5 新 6)。
pub fn first_line(src: &[u8]) -> &[u8] {
    match src.iter().position(|&b| b == 0x0A) {
        Some(i) => &src[..i],
        None => src,
    }
}

/// drip の 1 周ぶん (票 §2 / B2)。注入リングの空き `RING − pending` と残りの
/// 小さい方。`pending >= RING` なら 0 (この周は積めない)。
pub fn drip_len(remaining: usize, pending: u32) -> usize {
    let free = INJECT_RING_SIZE.saturating_sub(pending as usize);
    remaining.min(free)
}

#[cfg(test)]
mod tests {
    use super::*;
    use libos32term::model::Cell::{self, Continuation, Single, Wide};

    fn row(s: &str, cols: usize) -> [Cell; 8] {
        /* テスト用: 半角文字列を 1 行 (最大 8 セル) に置く。 */
        assert!(cols <= 8);
        let mut cells = [Single(' '); 8];
        for (i, ch) in s.chars().enumerate() {
            if i < cols {
                cells[i] = Single(ch);
            }
        }
        cells
    }

    #[test]
    fn copy_trims_trailing_spaces_and_newline_terminates_each_row() {
        let cols = 8;
        let mut cells = Vec::new();
        cells.extend_from_slice(&row("ab  ", cols)); /* 末尾空白は落とす */
        cells.extend_from_slice(&row("cd", cols));
        let mut out = [0u8; 64];
        let n = cells_to_text(&cells, cols, &mut out);
        assert_eq!(&out[..n], b"ab\ncd\n");
    }

    #[test]
    fn copy_drops_trailing_blank_rows_but_keeps_interior_blank_lines() {
        let cols = 8;
        let mut cells = Vec::new();
        cells.extend_from_slice(&row("x", cols));
        cells.extend_from_slice(&row("", cols)); /* 中の空行は残す */
        cells.extend_from_slice(&row("y", cols));
        cells.extend_from_slice(&row("", cols)); /* 末尾の空行は落とす */
        cells.extend_from_slice(&row("", cols));
        let mut out = [0u8; 64];
        let n = cells_to_text(&cells, cols, &mut out);
        assert_eq!(&out[..n], b"x\n\ny\n");
    }

    #[test]
    fn copy_all_blank_yields_nothing() {
        let cols = 8;
        let mut cells = Vec::new();
        cells.extend_from_slice(&row("", cols));
        cells.extend_from_slice(&row("   ", cols));
        let mut out = [0u8; 64];
        assert_eq!(cells_to_text(&cells, cols, &mut out), 0);
    }

    #[test]
    fn copy_wide_is_one_char_and_continuation_is_skipped() {
        let cols = 4;
        /* 「あb」: Wide('あ') + Continuation + Single('b') + 空白。 */
        let cells = [Wide('あ'), Continuation, Single('b'), Single(' ')];
        let mut out = [0u8; 32];
        let n = cells_to_text(&cells, cols, &mut out);
        assert_eq!(&out[..n], "あb\n".as_bytes());
    }

    #[test]
    fn copy_replaces_nul_and_esc_with_space() {
        let cols = 4;
        let cells = [Single('a'), Single('\0'), Single('\u{1b}'), Single('b')];
        let mut out = [0u8; 32];
        let n = cells_to_text(&cells, cols, &mut out);
        /* NUL/ESC は空白に。行末ではないので落ちない。 */
        assert_eq!(&out[..n], b"a  b\n");
    }

    #[test]
    fn copy_truncates_on_utf8_boundary_without_splitting_a_char() {
        let cols = 4;
        /* 3 バイト文字 2 つ + 改行。out=4 なら 1 文字 (3B) だけ入り、
         * 改行も 2 文字目も入らない。境界を割らない。 */
        let cells = [Wide('あ'), Continuation, Wide('い'), Continuation];
        let mut out = [0u8; 4];
        let n = cells_to_text(&cells, cols, &mut out);
        assert_eq!(&out[..n], "あ".as_bytes());
        assert_eq!(n, 3);
    }

    #[test]
    fn attach_transform_maps_lf_to_cr_keeps_tab_drops_other_controls() {
        let mut dst = [0u8; 32];
        let n = attach_transform(b"a\tb\nc\x1b\rd\x7fe", &mut dst);
        /* TAB 残る、LF→CR、ESC/CR(生)/DEL は落とす。 */
        assert_eq!(&dst[..n], b"a\tb\rcde");
    }

    #[test]
    fn attach_transform_keeps_utf8_bytes_and_stops_at_dst_end() {
        let mut dst = [0u8; 3];
        /* 「あ」= 3B はそのまま (>= 0x20 の後続バイト)、4 バイト目は入らない。 */
        let n = attach_transform("あx".as_bytes(), &mut dst);
        assert_eq!(&dst[..n], "あ".as_bytes());
        assert_eq!(n, 3);
    }

    #[test]
    fn first_line_stops_at_first_lf() {
        assert_eq!(first_line(b"echo hi\nrm -rf\nx"), b"echo hi");
        assert_eq!(first_line(b"no newline"), b"no newline");
        assert_eq!(first_line(b"\nrest"), b"");
    }

    #[test]
    fn drip_len_respects_ring_space_pending_and_remaining() {
        /* リングが空: 残り全部 (ただし 1 周は 256 まで)。 */
        assert_eq!(drip_len(1000, 0), 256);
        assert_eq!(drip_len(100, 0), 100);
        /* pending があるぶん空きが減る。 */
        assert_eq!(drip_len(1000, 200), 56);
        assert_eq!(drip_len(30, 200), 30);
        /* リング満杯 (以上) なら 0。 */
        assert_eq!(drip_len(1000, 256), 0);
        assert_eq!(drip_len(1000, 300), 0);
        /* 残り 0 なら 0。 */
        assert_eq!(drip_len(0, 0), 0);
    }
}
