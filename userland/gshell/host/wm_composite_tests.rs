//! wm_composite_tests.rs — 合成器 (`wm::composite_rect`) のホスト試験。
//! `src/wm.rs` の末尾から `#[cfg(test)] #[path]` で取り込む (入力系の `wm_tests.rs` とは別)。
//!
//! ここで見るのは **「WM が描いてよい画素はどこか」** だけ。ホストの画素 RAM
//! (`mocks`) は実機の VRAM / バックバッファの証拠にはならない。
use super::*;

/// クライアント面に置く見張り色 (システム色と重ならない値)。
const SENTINEL: u8 = 200;

/// 検査用のウィンドウを 1 枚組み立てる。
fn win(x: i32, y: i32, w: i32, h: i32, title: &[u8]) -> Win {
    use os32api::gui::proto::{GUI_WF_BORDER, GUI_WF_HAS_CLOSE, GUI_WF_MOVABLE};
    let mut v = Win::EMPTY;
    v.used = true;
    v.visible = true;
    v.gen = 1;
    v.owner = 2;
    v.x = x;
    v.y = y;
    v.w = w;
    v.h = h;
    v.flags = GUI_WF_BORDER | GUI_WF_HAS_CLOSE | GUI_WF_MOVABLE;
    let mut i = 0;
    while i < title.len() && i < 39 {
        v.title[i] = title[i];
        i += 1;
    }
    v
}

/// W-3 の配置 (PM 実測 2026-09-11、32MB 構成 / G3 の 4 本起動)。
/// z = 0: gui_bench、1: Help、2: File Manager (前面)。
fn w3_state() -> GuiState {
    let mut st = GuiState::NEW;
    st.screen_w = 640;
    st.screen_h = 400;
    st.windows[0] = win(25, 150, 418, 218, b"gui_bench");
    st.windows[1] = win(373, 66, 242, 179, b"Help");
    st.windows[2] = win(15, 10, 568, 335, b"File Manager");
    st.zorder[0] = 0;
    st.zorder[1] = 1;
    st.zorder[2] = 2;
    st.z_count = 3;
    st
}

fn fill_rect_raw(r: Rect, c: u8) {
    unsafe { os32api::gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, c) };
}

/// `r` の画素が全部 `c` であることを確かめる (最初の違反を座標つきで報告)。
fn assert_all(r: Rect, c: u8, what: &str) {
    let px = crate::mocks::pixels();
    let mut y = r.y;
    while y < r.bottom() {
        let mut x = r.x;
        while x < r.right() {
            let got = px[y as usize * crate::mocks::W + x as usize];
            assert_eq!(got, c, "{what}: ({x},{y}) が {got} (期待 {c})");
            x += 1;
        }
        y += 1;
    }
}

fn px_at(x: i32, y: i32) -> u8 {
    crate::mocks::pixels()[y as usize * crate::mocks::W + x as usize]
}

/// **W-3 (2026-09-11) の回帰試験。**
///
/// 3 枚以上が重なると、背面窓のクローム (枠・タイトルバー・閉じるボタン) が
/// 窓の外形まるごとに描かれ、前面窓のクライアント面に 1px の枠線が残っていた
/// (File Manager のリスト面を gui_bench の右辺と Help の下辺が貫いた)。
/// WM はクライアント面を持たない (契約 v13 CONTRACTS) ので、一度書いた画素は
/// そのアプリが `Paint` を返すまで消えない。**枠はその窓の可視領域だけに描く。**
#[test]
fn regression_w3_lower_chrome_never_enters_a_higher_windows_client() {
    use crate::mocks;
    use os32api::gui::proto::GUI_COLOR_TEXT;
    mocks::init();
    let st = w3_state();
    /* T5b の常駐パネルは feat/gui で撤去済み (重なりの前提は無い)。 */

    /* 前面 (File Manager) のクライアント面をアプリが描いた状態にしておく。 */
    let front = st.windows[2].client_rect_screen();
    fill_rect_raw(front, SENTINEL);

    composite_rect(&st, Rect::new(0, 0, st.screen_w, st.screen_h));

    /* (1) 前面窓のクライアント面は 1 画素も触られていない。 */
    assert_all(front, SENTINEL, "前面窓のクライアント面に WM が描いた");

    /* (2) 空振りでないこと: 見えている枠はちゃんと描かれている。
     *     gui_bench の右辺 (x=442) は File Manager の下 (y<345) では隠れるが、
     *     その下 (y=350) では見えている。 */
    assert_eq!(
        px_at(442, 350),
        GUI_COLOR_TEXT,
        "gui_bench の右辺が見えている場所にも描かれていない"
    );
    /* Help の下辺 (y=244) は File Manager の右外 (x=590) では見えている。 */
    assert_eq!(
        px_at(590, 244),
        GUI_COLOR_TEXT,
        "Help の下辺が見えている場所にも描かれていない"
    );
    /* 前面窓自身の枠は当然描かれる。 */
    assert_eq!(px_at(15, 10), GUI_COLOR_TEXT, "前面窓の外枠が無い");
    assert_eq!(px_at(582, 344), GUI_COLOR_TEXT, "前面窓の外枠が無い");
}

/// 背面窓のタイトル文字も前面窓のクライアント面へ出てはいけない。
/// `kcg_draw_utf8` にはクリップが無いので、セルが可視領域に収まらない文字は
/// **落とす** (半分だけ描くと WM には消せない)。
#[test]
fn regression_w3_lower_title_text_is_dropped_instead_of_spilling() {
    use crate::mocks;
    mocks::init();
    let mut st = w3_state();
    /* gui_bench のタイトルバー (y 152..170) は File Manager の下に完全に隠れる。 */
    st.windows[0].title = [0; 40];
    let t = b"gui_bench";
    let mut i = 0;
    while i < t.len() {
        st.windows[0].title[i] = t[i];
        i += 1;
    }
    let front = st.windows[2].client_rect_screen();
    fill_rect_raw(front, SENTINEL);
    composite_rect(&st, Rect::new(0, 0, st.screen_w, st.screen_h));
    assert_all(front, SENTINEL, "背面窓のタイトル文字が前面窓へ漏れた");
}

/// 部分損傷でクロームを描き直しても、文字セルが半分だけ塗り替わって欠けない。
/// (枠に渡すクリップは「損傷矩形」ではなく「その窓の可視な外形」であること。)
#[test]
fn partial_damage_repaints_whole_chrome_of_the_touched_window() {
    use crate::mocks;
    mocks::init();
    let st = w3_state();
    let whole = Rect::new(0, 0, st.screen_w, st.screen_h);
    composite_rect(&st, whole);
    let before = mocks::pixels();
    /* 前面窓のタイトルバーの中の 1 画素だけを損傷として渡す。 */
    composite_rect(&st, Rect::new(200, 20, 1, 1));
    let after = mocks::pixels();
    let mut i = 0;
    while i < before.len() {
        assert_eq!(
            after[i],
            before[i],
            "部分損傷の再合成で画面が変わった ({},{})",
            i % mocks::W,
            i / mocks::W
        );
        i += 1;
    }
}
