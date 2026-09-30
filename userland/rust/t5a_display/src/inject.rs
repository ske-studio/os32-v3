//! inject.rs — 打鍵 → 注入バイト列の純変換 (票 K7 §5 R2 / §6 K7-A)。
//!
//! 票 K6C-A §2-3 の `sink.rs` と同じ流儀で、ここは `no_std` の純関数だけ。
//! KAPI も GUI も触らないので、ホスト試験が `host_tests/src/lib.rs` から
//! そのまま取り込める。実際の `kbd_inject` 呼び出しは `guest.rs`。
//!
//! ## 何を注ぐか
//!
//! 注ぐバイトは「GUI モードでなければ IRQ1 が cooked リングへ積んだはずの
//! バイト」と同じにする。正典は `drivers/kbd.c` の `scancode_to_ascii[]`
//! (gshell の `input.rs::SC2A` はその写し):
//!
//! | キー | scan | 注入 | 根拠 |
//! |---|---|---|---|
//! | Enter | 0x1C | **0x0D** (`\r`) | `drivers/kbd.c:108` が 0x0D |
//! | BS    | 0x0E | 0x08 | 同 `:84` (gshell SC2A) |
//! | TAB   | 0x0F | 0x09 | 同上 |
//! | ESC   | 0x00 | 注入しない | 端末アプリ自身の終了に使う (票 §6) |
//! | 矢印・ファンクション | — | 注入しない | 票 §6「範囲外」。`kbd_getkey` の
//!   スキャンコードは GUI 中 0 なので (票 §1 D7)、注いでも意味を持たない |
//!
//! **Enter を `\n` にしない理由**: 常駐シェルの行入力 (`userland/shell/ui.c:497`、
//! `ime_getkey()` 経由) は `0x0D` **だけ**を行末として見る。`\n` を注ぐと
//! 行が確定しない。0x0D なら両対応の呼び手 (`shell/cmd_script.c:295` の `read`、
//! `cmds/less.c:402`、`lib/filer/filer_core.c:289`) も同じように通る。
//!
//! **印字可能 ASCII を `GUI_EV_KEY` から注がない理由**: gshell は 0x20..0x7E の
//! キーに対して `GUI_EV_KEY` と `GUI_EV_TEXT` を**両方**積む
//! (`userland/gshell/src/input.rs:436-450`)。KEY 側でも注ぐと 1 打鍵が
//! 2 バイトになる。FEP の確定文字は `GUI_EV_TEXT` にしか来ない
//! (`userland/gshell/src/fep.rs:483`)。

/// `GuiEvtText.utf8` の長さ。1 回の注入はこれを超えない。
pub const MAX: usize = 8;

/* --- 制御キーが積むバイト (drivers/kbd.c の keymap と同じ値) --- */
pub const BYTE_ENTER: u8 = 0x0D;
pub const BYTE_BS: u8 = 0x08;
pub const BYTE_TAB: u8 = 0x09;

/* --- スキャンコード (libos32gui::widget::SCAN_* と同じ値。ここは
 *     ホスト試験から見えないといけないので自前で置く) --- */
pub const SCAN_ESC: u8 = 0x00;
pub const SCAN_BS: u8 = 0x0E;
pub const SCAN_TAB: u8 = 0x0F;
pub const SCAN_RETURN: u8 = 0x1C;

/// 1 回分の注入バイト列。`kbd_inject(ptr, len)` へそのまま渡せる。
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Bytes {
    buf: [u8; MAX],
    len: usize,
}

impl Bytes {
    pub const EMPTY: Bytes = Bytes {
        buf: [0; MAX],
        len: 0,
    };

    const fn one(b: u8) -> Bytes {
        let mut out = Bytes::EMPTY;
        out.buf[0] = b;
        out.len = 1;
        out
    }

    pub fn as_slice(&self) -> &[u8] {
        &self.buf[..self.len]
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

/// `GUI_EV_TEXT` の payload から注入バイト列を作る。
///
/// `sub` の下位 7 bit が長さ、bit7 は「まだ続きがある」印 (FEP の確定文字列を
/// 8 バイトずつ切って送るときに立つ、`gshell/src/fep.rs:483`)。長さは payload の
/// 長さで頭打ちにする — libos32gui の配送 (`app.rs:258-261`) と同じ規則で、
/// 壊れた `sub` でも境界の外を読まない。
///
/// 中身は検査も変換もしない (票 §1 D4「カーネルで再変換しない」の裏返し):
/// FEP が確定した UTF-8 バイト列をそのまま CUI プログラムへ渡す。
pub fn from_text(sub: u8, payload: &[u8; MAX]) -> Bytes {
    let n = ((sub & 0x7F) as usize).min(MAX);
    let mut out = Bytes::EMPTY;
    out.buf[..n].copy_from_slice(&payload[..n]);
    out.len = n;
    out
}

/// `GUI_EV_KEY` から注入バイト列を作る。押下のときの制御キーだけが中身を持つ。
///
/// `ch` は見ない。gshell の `translate()` は同じ keymap を引くので値は一致する
/// が、判断の根拠を 1 つ (スキャンコード) に絞ったほうが取り違えが起きない。
pub fn from_key(scan: u8, down: bool) -> Bytes {
    if !down {
        return Bytes::EMPTY;
    }
    match scan & 0x7F {
        SCAN_RETURN => Bytes::one(BYTE_ENTER),
        SCAN_BS => Bytes::one(BYTE_BS),
        SCAN_TAB => Bytes::one(BYTE_TAB),
        /* ESC は端末アプリ自身の終了に使う (票 §6)。 */
        SCAN_ESC => Bytes::EMPTY,
        /* 矢印・ファンクションは範囲外。印字可能キーは GUI_EV_TEXT 側で来る。 */
        _ => Bytes::EMPTY,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn text_takes_low_seven_bits_of_sub_and_clamps_to_payload() {
        /* 空の TEXT は 1 バイトも注がない (kbd_inject は len==0 で 0 を返すが、
         * 呼ばずに済むほうが良い)。 */
        assert!(from_text(0, &[0; MAX]).is_empty());
        assert_eq!(from_text(0, &[0; MAX]).as_slice(), b"");

        /* 印字可能 ASCII 1 文字 (gshell input.rs:445 の形)。 */
        let mut p = [0u8; MAX];
        p[0] = b'a';
        assert_eq!(from_text(1, &p).as_slice(), b"a");

        /* 多バイト: 「あ」= E3 81 82 の 3 バイトがそのまま出る。 */
        let mut p = [0u8; MAX];
        p[..3].copy_from_slice("あ".as_bytes());
        assert_eq!(from_text(3, &p).as_slice(), "あ".as_bytes());

        /* more (bit7) が立っていても長さは下位 7 bit だけ。 */
        assert_eq!(from_text(0x83, &p).as_slice(), "あ".as_bytes());

        /* 8 バイトちょうど = 「あいうえ」(12B) の頭 8B。境界で切れた UTF-8 も
         * そのまま渡す — 続きは次の TEXT (more 付き) で来る。 */
        let mut p = [0u8; MAX];
        p.copy_from_slice(&"あいうえ".as_bytes()[..MAX]);
        assert_eq!(from_text(8, &p).len(), MAX);
        assert_eq!(from_text(8, &p).as_slice(), &"あいうえ".as_bytes()[..MAX]);

        /* 壊れた sub (9 以上) でも payload の外を読まない。 */
        assert_eq!(from_text(0x7F, &p).len(), MAX);
        assert_eq!(from_text(0xFF, &p).as_slice(), &p[..]);
    }

    #[test]
    fn key_injects_only_control_keys_on_press() {
        /* Enter は 0x0D。userland/shell/ui.c:497 が 0x0D だけを行末に見る。 */
        assert_eq!(from_key(SCAN_RETURN, true).as_slice(), &[0x0D]);
        assert_eq!(from_key(SCAN_BS, true).as_slice(), &[0x08]);
        assert_eq!(from_key(SCAN_TAB, true).as_slice(), &[0x09]);

        /* 離しは注がない (1 打鍵が 2 バイトになる)。 */
        for scan in [SCAN_RETURN, SCAN_BS, SCAN_TAB] {
            assert!(from_key(scan, false).is_empty());
        }

        /* ESC は自分の終了に使うので注がない。 */
        assert!(from_key(SCAN_ESC, true).is_empty());

        /* 未対応キー: 矢印 (0x3A/0x3B/0x3C/0x3D)、HOME、ROLLUP/DOWN、
         * 印字可能キー ('a' = 0x1D、'1' = 0x01)、表の外 (0x50..)。 */
        for scan in [
            0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x36, 0x37, 0x39, 0x1D, 0x01, 0x34, 0x50, 0x7F,
        ] {
            assert!(from_key(scan, true).is_empty(), "scan {:#04x}", scan);
        }

        /* 離しビット (0x80) が混じった scan でも同じ判断になる。 */
        assert_eq!(from_key(SCAN_RETURN | 0x80, true).as_slice(), &[0x0D]);
    }
}
