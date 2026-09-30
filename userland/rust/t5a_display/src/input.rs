use crate::state::{Fixture, Movement};
#[derive(Clone, Copy)]
pub enum Action {
    None,
    Quit,
    /// fixture の切り替え。ゲスト (live) は捨て、ホスト試験だけが中身を読む
    /// — 票 §2-4「ゲストは sink だけ」。
    #[allow(dead_code)]
    Select(Fixture),
    Move(Movement),
}
/* drivers/kbd.c のスキャンコード (libos32gui::widget::SCAN_* と同じ値)。
 * ここに並べるのは `inject::from_key` が**注がない**キーだけ — 注ぐキーに
 * 表示操作を重ねると、CUI プログラムへ渡した打鍵で画面が動いてしまう
 * (票 K7 §6「矢印・ファンクションは範囲外」)。 */
pub const SCAN_ROLLUP: u8 = 0x36;
pub const SCAN_ROLLDOWN: u8 = 0x37;
pub const SCAN_UP: u8 = 0x3A;
pub const SCAN_DOWN: u8 = 0x3D;
pub const SCAN_HOME: u8 = 0x3E;

/// スキャンコードによる表示操作。注入が生きているあいだはこちらだけを使う
/// (ASCII の割り当ては CUI プログラムのものになるため)。
pub fn nav(scan: u8, down: bool) -> Action {
    if !down {
        return Action::None;
    }
    match scan & 0x7F {
        SCAN_UP => Action::Move(Movement::Up),
        SCAN_DOWN => Action::Move(Movement::Down),
        SCAN_ROLLUP | SCAN_HOME => Action::Move(Movement::First),
        SCAN_ROLLDOWN => Action::Move(Movement::Last),
        _ => Action::None,
    }
}

pub fn key(ch: u8, down: bool) -> Action {
    if !down {
        return Action::None;
    }
    match ch {
        b'1' => Action::Select(Fixture::Normal),
        b'2' => Action::Select(Fixture::Exact),
        b'3' => Action::Select(Fixture::Full),
        b'4' => Action::Select(Fixture::TooWide),
        b'5' => Action::Select(Fixture::Finish),
        b'j' => Action::Move(Movement::Down),
        b'k' => Action::Move(Movement::Up),
        b'g' => Action::Move(Movement::First),
        b'e' => Action::Move(Movement::Last),
        b'q' => Action::Quit,
        _ => Action::None,
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fixed_keys_and_keyup_ignored() {
        assert!(matches!(key(b'1', true), Action::Select(Fixture::Normal)));
        assert!(matches!(key(b'2', true), Action::Select(Fixture::Exact)));
        assert!(matches!(key(b'3', true), Action::Select(Fixture::Full)));
        assert!(matches!(key(b'4', true), Action::Select(Fixture::TooWide)));
        assert!(matches!(key(b'5', true), Action::Select(Fixture::Finish)));
        assert!(matches!(key(b'j', true), Action::Move(Movement::Down)));
        assert!(matches!(key(b'k', true), Action::Move(Movement::Up)));
        assert!(matches!(key(b'g', true), Action::Move(Movement::First)));
        assert!(matches!(key(b'e', true), Action::Move(Movement::Last)));
        assert!(matches!(key(b'q', true), Action::Quit));
        for ch in b"12345jkgeq" {
            assert!(matches!(key(*ch, false), Action::None));
        }
        assert!(matches!(key(0, true), Action::None));
    }

    #[test]
    fn nav_uses_only_keys_that_are_never_injected() {
        assert!(matches!(nav(SCAN_UP, true), Action::Move(Movement::Up)));
        assert!(matches!(nav(SCAN_DOWN, true), Action::Move(Movement::Down)));
        assert!(matches!(
            nav(SCAN_HOME, true),
            Action::Move(Movement::First)
        ));
        assert!(matches!(
            nav(SCAN_ROLLUP, true),
            Action::Move(Movement::First)
        ));
        assert!(matches!(
            nav(SCAN_ROLLDOWN, true),
            Action::Move(Movement::Last)
        ));
        /* 離しは効かない。 */
        for scan in [SCAN_UP, SCAN_DOWN, SCAN_HOME, SCAN_ROLLUP, SCAN_ROLLDOWN] {
            assert!(matches!(nav(scan, false), Action::None));
            /* 表示操作に使うキーは 1 バイトも注がない (二重作用を作らない)。 */
            assert!(crate::inject::from_key(scan, true).is_empty());
        }
        /* 注入するキーと ESC は表示を動かさない。 */
        for scan in [
            crate::inject::SCAN_RETURN,
            crate::inject::SCAN_BS,
            crate::inject::SCAN_TAB,
            crate::inject::SCAN_ESC,
        ] {
            assert!(matches!(nav(scan, true), Action::None));
        }
    }
}
