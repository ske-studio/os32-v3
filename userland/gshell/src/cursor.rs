//! cursor.rs — マウスカーソル (票 W1 の作業 7)。
//!
//! 「移動は損傷に含めない」= カーソルはウィンドウの dirty / issued とは別経路で
//! 下地を退避・復元する。実体は 10×16 の固定ビットマップと、その下にあった
//! バックバッファ 160 画素の退避配列 (固定長、alloc なし)。
//!
//! 状態は 3 つ:
//!   - `shown = false`            … 画面にカーソルは無く、`saved` は無効。
//!   - `shown = true, valid`      … 画面にカーソルがあり、`saved` に下地がある。
//!   - `discard()` の直後         … 画面上のカーソルは他者 (アプリの COMMIT や
//!                                   WM の再合成) に潰された。`saved` は捨てる。
//!
//! 呼ぶ側の作法 (契約 T8):
//!   - 何かをバックバッファへ描く前に [`hide`]、描いた後に [`show`]。
//!   - アプリが自分のクライアント面を描いた後 (COMMIT) は下地が変わっているので
//!     [`discard`] してから [`show`] (退避し直す)。
//!   - present は呼ぶ側が `wm::queue_present` + `wm::flush_present` でまとめる
//!     (commits を 1 周 1 回に保つ)。

use crate::ffi;
use crate::wm::{GuiState, Rect};
use os32api::gfx;
use os32api::gui::proto::{GUI_COLOR_TEXT, GUI_COLOR_WINDOW};

pub const CUR_W: i32 = 10;
pub const CUR_H: i32 = 16;
const NPIX: usize = (CUR_W * CUR_H) as usize;

/* 0 = 透明 / 1 = 輪郭 (TEXT) / 2 = 内側 (WINDOW) */
#[rustfmt::skip]
static SHAPE: [u8; NPIX] = [
    1,0,0,0,0,0,0,0,0,0,
    1,1,0,0,0,0,0,0,0,0,
    1,2,1,0,0,0,0,0,0,0,
    1,2,2,1,0,0,0,0,0,0,
    1,2,2,2,1,0,0,0,0,0,
    1,2,2,2,2,1,0,0,0,0,
    1,2,2,2,2,2,1,0,0,0,
    1,2,2,2,2,2,2,1,0,0,
    1,2,2,2,2,2,2,2,1,0,
    1,2,2,2,2,2,1,1,1,1,
    1,2,2,1,2,2,1,0,0,0,
    1,2,1,0,1,2,2,1,0,0,
    1,1,0,0,1,2,2,1,0,0,
    1,0,0,0,0,1,2,2,1,0,
    0,0,0,0,0,1,2,2,1,0,
    0,0,0,0,0,0,1,1,0,0,
];

#[derive(Clone, Copy)]
pub struct Cursor {
    pub shown: bool,
    pub x: i32,
    pub y: i32,
    saved: [u8; NPIX],
}

impl Cursor {
    pub const EMPTY: Cursor = Cursor { shown: false, x: 0, y: 0, saved: [0; NPIX] };
}

/// 現在のカーソル矩形 (画面座標)。
#[inline]
pub fn rect(st: &GuiState) -> Rect {
    Rect::new(st.cursor.x, st.cursor.y, CUR_W, CUR_H)
}

/// 画素 (px,py) が画面内か。
#[inline]
fn on_screen(st: &GuiState, px: i32, py: i32) -> bool {
    px >= 0 && py >= 0 && px < st.screen_w && py < st.screen_h
}

/// 下地を退避してカーソルを描く。既に出ていれば何もしない。
pub fn show(st: &mut GuiState) {
    /* 全画面 GFX 中は画面を持っていない (票 T8 D4a)。復帰の `composite_full` が
     * 描き直す (そのときは門が開いている)。 */
    if crate::fullscreen::active() {
        return;
    }
    if st.cursor.shown {
        return;
    }
    let (cx, cy) = (st.cursor.x, st.cursor.y);
    /* 下地の退避 (dirty 登録なし: gfx_get_pixel は読むだけ)。 */
    let mut i = 0;
    while i < NPIX {
        let px = cx + (i as i32) % CUR_W;
        let py = cy + (i as i32) / CUR_W;
        st.cursor.saved[i] = if on_screen(st, px, py) {
            unsafe { ffi::gfx_get_pixel(px, py) }
        } else {
            0
        };
        i += 1;
    }
    /* 形を描く。 */
    let mut k = 0;
    while k < NPIX {
        let s = SHAPE[k];
        if s != 0 {
            let px = cx + (k as i32) % CUR_W;
            let py = cy + (k as i32) / CUR_W;
            if on_screen(st, px, py) {
                let c = if s == 1 { GUI_COLOR_TEXT } else { GUI_COLOR_WINDOW };
                unsafe { gfx::gfx_pixel(px, py, c) };
            }
        }
        k += 1;
    }
    st.cursor.shown = true;
}

/// 退避した下地を戻してカーソルを消す。出ていなければ何もしない。
pub fn hide(st: &mut GuiState) {
    if !st.cursor.shown {
        return;
    }
    let (cx, cy) = (st.cursor.x, st.cursor.y);
    let mut k = 0;
    while k < NPIX {
        if SHAPE[k] != 0 {
            let px = cx + (k as i32) % CUR_W;
            let py = cy + (k as i32) / CUR_W;
            if on_screen(st, px, py) {
                unsafe { gfx::gfx_pixel(px, py, st.cursor.saved[k]) };
            }
        }
        k += 1;
    }
    st.cursor.shown = false;
}

/// 下地が他者に書き潰されたので退避を捨てる (復元しない)。
/// アプリの COMMIT や WM の全面再合成の直後に使う。
#[inline]
pub fn discard(st: &mut GuiState) {
    st.cursor.shown = false;
}

/// カーソルを (x,y) へ動かす。バックバッファの更新と present までを行う
/// (X3 / X4 のどちらからでも呼べる最小の描画)。動きが無ければ何もしない。
pub fn move_to(st: &mut GuiState, x: i32, y: i32) {
    /* 全画面 GFX 中は画面を持っていない (票 T8 D4a)。present は門で止まるが、
     * バックバッファへの画素書き込み自体を行わない。 */
    if crate::fullscreen::active() {
        return;
    }
    if st.cursor.shown && st.cursor.x == x && st.cursor.y == y {
        return;
    }
    let old = rect(st);
    let was_shown = st.cursor.shown;
    hide(st);
    st.cursor.x = x;
    st.cursor.y = y;
    show(st);
    if was_shown {
        crate::wm::queue_present(st, old);
    }
    let newr = rect(st);
    crate::wm::queue_present(st, newr);
    crate::wm::flush_present();
}

/// 与えられた矩形がカーソルに掛かるなら、退避を捨てて描き直す。
/// present はしない (呼ぶ側が queue / flush する)。掛かったら true。
pub fn refresh_if_hit(st: &mut GuiState, r: Rect) -> bool {
    if !st.cursor.shown {
        return false;
    }
    if !rect(st).intersects(&r) {
        return false;
    }
    discard(st);
    show(st);
    true
}
