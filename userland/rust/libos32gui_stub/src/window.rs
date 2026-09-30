//! window.rs — ウィンドウの所有型 (契約 U1 / T4) のスタブ。
//!
//! `Window` が持つのは WindowId だけ。登録表・クライアント面サーフェス・
//! ウィジェット木は**ライブラリ側の `.data`/`.bss`** にある。`Drop` は
//! ジャンプ表の `E_WINDOW_DROP` を呼ぶ (登録表の後始末 + `OP_WIN_DESTROY`)。

use crate::client::{self, ok0, GuiErr, GuiResult};
use crate::shcall;
use crate::widget::WidgetId;
use os32api::gui::proto::{GuiRect16, GuiWinSpec, GUI_WF_DEFAULT};
use os32api::gui::stub as sh;
use os32api::gui::types::{Rect, SurfaceId};

/* ================================================================ */
/*  spec (契約 U1) — title は 40B 固定                               */
/* ================================================================ */

/// `create_window` の指定。
#[derive(Clone, Copy)]
pub struct WindowSpec {
    pub(crate) inner: GuiWinSpec,
}

impl WindowSpec {
    /// タイトルと外枠矩形 (画面座標) から作る。タイトルは 39B で UTF-8 境界切り。
    pub fn new(title: &[u8], rect: Rect) -> WindowSpec {
        let mut t = [0u8; 40];
        let n = client::utf8_truncate(title, 39);
        let mut i = 0;
        while i < n && i < 39 {
            t[i] = title[i];
            i += 1;
        }
        WindowSpec {
            inner: GuiWinSpec {
                title: t,
                rect: GuiRect16 { x: rect.x, y: rect.y, w: rect.w, h: rect.h },
                flags: GUI_WF_DEFAULT,
                min_w: 0,
                min_h: 0,
            },
        }
    }

    /// `GUI_WF_*` を差し替える。
    pub fn flags(mut self, flags: u16) -> WindowSpec {
        self.inner.flags = flags;
        self
    }

    /// 最小の外枠サイズ。
    pub fn min_size(mut self, w: i16, h: i16) -> WindowSpec {
        self.inner.min_w = w;
        self.inner.min_h = h;
        self
    }
}

/* ================================================================ */
/*  所有型                                                           */
/* ================================================================ */

/// ウィンドウ 1 枚。`Drop` で WM へ `DESTROY` を送る (契約 T4)。
pub struct Window {
    id: u32,
}

impl Window {
    /// ウィンドウを作り、クライアント面のサーフェスを用意する。
    pub fn create(spec: &WindowSpec) -> GuiResult<Window> {
        let mut id = 0u32;
        let r = shcall!(
            sh::E_WINDOW_CREATE,
            extern "C" fn(*const GuiWinSpec, *mut u32) -> i32,
            &spec.inner as *const GuiWinSpec,
            &mut id as *mut u32
        );
        if r < 0 {
            Err(GuiErr(r))
        } else {
            Ok(Window { id })
        }
    }

    /// WindowId (index:16 | generation:16)。
    #[inline]
    pub fn id(&self) -> u32 {
        self.id
    }

    /// クライアント面のサーフェス (G API に渡す)。
    pub fn surface(&self) -> SurfaceId {
        SurfaceId(shcall!(
            sh::E_WINDOW_SURFACE,
            extern "C" fn(u32) -> u32,
            self.id
        ))
    }

    /// クライアント面の大きさ (ローカル原点 0,0)。
    pub fn client_size(&self) -> (i16, i16) {
        let mut w = 0i16;
        let mut h = 0i16;
        shcall!(
            sh::E_WINDOW_CLIENT_SIZE,
            extern "C" fn(u32, *mut i16, *mut i16),
            self.id,
            &mut w as *mut i16,
            &mut h as *mut i16
        );
        (w, h)
    }

    /// クライアント面のローカル矩形。
    pub fn client_rect(&self) -> Rect {
        let (w, h) = self.client_size();
        Rect::new(0, 0, w, h)
    }

    /// ウィジェット木の根を差し替える。既存の木は捨てる。
    pub fn set_root(&self, root: WidgetId) -> GuiResult<()> {
        ok0(shcall!(
            sh::E_WINDOW_SET_ROOT,
            extern "C" fn(u32, u32) -> i32,
            self.id,
            root.raw()
        ))
    }

    /// 根から箱レイアウトを引き直し、全面を `invalidate` する (契約 U7)。
    pub fn relayout(&self) {
        shcall!(sh::E_WINDOW_RELAYOUT, extern "C" fn(u32), self.id)
    }

    /// 損傷を申告する (クライアントローカル)。描くのは次の `Paint`。
    pub fn invalidate(&self, rect: Rect) -> GuiResult<()> {
        ok0(shcall!(
            sh::E_WINDOW_INVALIDATE,
            extern "C" fn(u32, Rect) -> i32,
            self.id,
            rect
        ))
    }

    /// クライアント面の全面を申告する。
    pub fn invalidate_all(&self) -> GuiResult<()> {
        let (w, h) = self.client_size();
        self.invalidate(Rect::new(0, 0, w, h))
    }

    pub fn move_to(&self, x: i16, y: i16) -> GuiResult<()> {
        client::win_move(self.id, x, y)
    }

    pub fn resize(&self, w: i16, h: i16) -> GuiResult<()> {
        client::win_resize(self.id, w, h)
    }

    pub fn show(&self, visible: bool) -> GuiResult<()> {
        client::win_show(self.id, visible)
    }

    pub fn set_title(&self, title: &[u8]) -> GuiResult<()> {
        client::win_set_title(self.id, title)
    }

    pub fn raise(&self) -> GuiResult<()> {
        client::win_raise(self.id)
    }

    pub fn set_focus(&self) -> GuiResult<()> {
        client::win_set_focus(self.id)
    }

    /// FEP の候補窓の位置 (契約 U2a)。
    pub fn set_text_cursor(&self, x: i16, y: i16, visible: bool) -> GuiResult<()> {
        client::win_set_text_cursor(self.id, x, y, visible)
    }

    /// WM からフォーカスを持っているか (`Focus{in}` の記録)。
    pub fn is_focused(&self) -> bool {
        shcall!(
            sh::E_WINDOW_IS_FOCUSED,
            extern "C" fn(u32) -> u32,
            self.id
        ) != 0
    }
}

impl Drop for Window {
    fn drop(&mut self) {
        shcall!(sh::E_WINDOW_DROP, extern "C" fn(u32), self.id)
    }
}

/// 登録済みのウィンドウ枚数。0 になったらアプリは終われる。
pub fn count() -> usize {
    shcall!(sh::E_WINDOW_COUNT, extern "C" fn() -> u32) as usize
}
