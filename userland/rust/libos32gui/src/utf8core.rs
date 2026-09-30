//! utf8core.rs — UTF-8 の境界計算だけを集めた小モジュール (契約 U9)。
//!
//! `client.rs` と `hostsvc.rs` が共用する。**os32api にもクレートの他の部分にも
//! 依存しない** (core だけ) — ホスト TDD (`host_tests`) が `#[path]` で直に
//! 取り込み、`hostsvc.rs` の clip 切り詰めを実 os32api 無しで固定できるように。
//!
//! 元は `client.rs` にあった 2 関数を、票 N4 §5 (clip_get の UTF-8 境界を
//! host_tests から踏むため) でここへ出した。挙動は変えていない。
#![allow(dead_code)]

/// UTF-8 の先頭バイトから符号単位のバイト長を得る (不正バイトは 1)。
#[inline]
pub fn utf8_seq_len(b: u8) -> usize {
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

/// `max` バイト以内に収まる UTF-8 の切れ目を返す (NUL 終端も尊重)。
/// 途中の符号単位で切らない (CLAUDE.md の注意事項 / 契約 U9)。
pub fn utf8_truncate(s: &[u8], max: usize) -> usize {
    let lim = if s.len() < max { s.len() } else { max };
    let mut i = 0usize;
    while i < lim {
        if s[i] == 0 {
            break;
        }
        let need = utf8_seq_len(s[i]);
        if i + need > lim {
            break;
        }
        i += need;
    }
    i
}
