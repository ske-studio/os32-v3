//! modal_tests.rs — T7b-b5: 入力欄の幅・caret が描画 (text::draw) と同じ規則で数える。
use super::*;

#[test]
fn b5_modal_caret_follows_the_latin_half_rule() {
    crate::mocks::init();
    /* Å・¥ (U+00A5)・a は 8px、あ・α は 16px。 */
    let s = "Å¥aあα".as_bytes();
    let n = s.len();
    assert_eq!(text_width(s, n, 0, n), 8 + 8 + 8 + 16 + 16);
    assert_eq!(text_width(s, n, 0, 2), 8, "Å (2 バイト) は半角");
    assert_eq!(text_width(s, n, 2, 5), 16, "¥ と a");
    /* 描画の送りと一致する。 */
    let w = crate::text::draw(Rect::new(0, 0, 640, 400), 0, 0, &[s, &[0]].concat(), 1, 0);
    assert_eq!(text_width(s, n, 0, n), w);
}
