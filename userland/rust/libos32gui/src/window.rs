//! window.rs — ウィンドウの所有型 (契約 U1 / T4)。票 C2 作業 4。
//!
//! アプリは座標を持たない。位置と大きさは WM が決め、`Configure` で返ってくる
//! (`rect` は**クライアント矩形の画面絶対座標**)。この型はそれを受けて
//! クライアント面のサーフェス (C1 の `create_window_surface`) を作り直すだけ。
//!
//! `Drop` で `OP_WIN_DESTROY` を送り、登録表とウィジェット木を落とす。破棄後の
//! ID は WM 側で `ERR_STALE` になり、こちらの登録表からも消えるので、古い
//! イベントは generation 不一致で捨てられる (契約 U2)。

use crate::client::{self, GuiResult};
use crate::uistate::{s, GUI_NONE};
use crate::widget::{self, WidgetId};
use os32api::gui::proto::{GuiRect16, GuiWinSpec, GUI_WF_DEFAULT};
use os32api::gui::types::{Rect, SurfaceId};

/* ================================================================ */
/*  spec (契約 U1) — title は 40B 固定                               */
/* ================================================================ */

/// `create_window` の指定。
#[derive(Clone, Copy)]
pub struct WindowSpec {
    inner: GuiWinSpec,
}

impl WindowSpec {
    /// タイトルと外枠矩形 (画面座標) から作る。タイトルは 39B で UTF-8 境界切り。
    pub fn new(title: &[u8], rect: Rect) -> WindowSpec {
        let mut t = [0u8; 40];
        let n = client::utf8_truncate(title, 39);
        let mut i = 0;
        while i < n {
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

/* ================================================================ */
/*  非所有の下請け (ジャンプ表 E_WINDOW_* の実体。票 C3)              */
/*                                                                  */
/*  所有型 `Window` はアプリ側 (スタブ) にあり、`Drop` で             */
/*  [`drop_window`] を呼ぶ。登録表・サーフェス・ウィジェット木は      */
/*  すべてライブラリの `.data`/`.bss` にある。                        */
/* ================================================================ */

/// ウィンドウを作り、クライアント面のサーフェスを用意する。戻り値は WindowId。
pub fn create_raw(spec: &GuiWinSpec) -> GuiResult<u32> {
    let id = client::win_create(spec)?;
    let rect = client::win_client_rect(id)?;

    let st = s();
    let slot = match st.alloc_win() {
        Some(i) => i,
        None => {
            let _ = client::win_destroy(id);
            return Err(client::GuiErr::FULL);
        }
    };
    st.windows[slot].id = id;
    st.windows[slot].client = rect;
    st.windows[slot].surface = crate::surface::create_window_surface(rect);
    /* 新しい窓は WM が最前面に置く (`z_add_top`)。W1 の `set_focus` は
     * 既に最前面なら `Focus` を出さないので、こちら側で初期値を入れておく
     * (**PM への申し送り**: 1 枚しか窓を持たないアプリは `Focus{in}` を
     * 一度も受け取らない)。後から届く `Focus{out}` で正しく落ちる。 */
    let mut k = 0;
    while k < st.windows.len() {
        if st.windows[k].used {
            st.windows[k].focused = k == slot;
        }
        k += 1;
    }
    Ok(id)
}

/// クライアント面のサーフェス (C1 の G API に渡す)。
pub fn surface_of(id: u32) -> SurfaceId {
    match s().win_slot(id) {
        Some(i) => s().windows[i].surface,
        None => SurfaceId::NULL,
    }
}

/// クライアント面の大きさ (ローカル原点 0,0)。
pub fn client_size_of(id: u32) -> (i16, i16) {
    match s().win_slot(id) {
        Some(i) => (s().windows[i].client.w, s().windows[i].client.h),
        None => (0, 0),
    }
}

/// ウィジェット木の根を差し替える (`row` / `column`)。既存の木は捨てる。
pub fn set_root_of(id: u32, root: WidgetId) -> GuiResult<()> {
    let slot = match s().win_slot(id) {
        Some(i) => i,
        None => return Err(client::GuiErr::STALE),
    };
    let old = s().windows[slot].root;
    if old != GUI_NONE {
        widget::free_tree(old as usize - 1);
    }
    let idx = match widget::resolve(root) {
        Some(i) => i,
        None => return Err(client::GuiErr::STALE),
    };
    s().windows[slot].root = (idx as u16) + 1;
    widget::assign_window(idx, id);
    s().windows[slot].focus = widget::first_focusable((idx as u16) + 1);
    relayout_of(id);
    Ok(())
}

/// 根から箱レイアウトを引き直し、全面を `invalidate` する (契約 U7)。
pub fn relayout_of(id: u32) {
    let slot = match s().win_slot(id) {
        Some(i) => i,
        None => return,
    };
    let root = s().windows[slot].root;
    let client = s().windows[slot].client;
    crate::layout::layout_window(root, client);
    let (w, h) = client_size_of(id);
    s().damage.push(id, Rect::new(0, 0, w, h));
}

/// 損傷を申告する (クライアントローカル)。描くのは次の `Paint`。
pub fn invalidate_of(id: u32, rect: Rect) {
    s().damage.push(id, rect);
}

/// WM からフォーカスを持っているか (`Focus{in}` の記録)。
pub fn is_focused_of(id: u32) -> bool {
    match s().win_slot(id) {
        Some(i) => s().windows[i].focused,
        None => false,
    }
}

/// 所有型の `Drop` 相当: 登録表から落として WM へ `DESTROY` を送る。
pub fn drop_window(id: u32) {
    unregister(id);
    let _ = client::win_destroy(id);
}

impl Window {
    /// ウィンドウを作り、クライアント面のサーフェスを用意する。
    pub fn create(spec: &WindowSpec) -> GuiResult<Window> {
        Ok(Window { id: create_raw(&spec.inner)? })
    }

    /// WindowId (index:16 | generation:16)。
    #[inline]
    pub fn id(&self) -> u32 {
        self.id
    }

    /// クライアント面のサーフェス (C1 の G API に渡す)。
    pub fn surface(&self) -> SurfaceId {
        surface_of(self.id)
    }

    /// クライアント面の大きさ (ローカル原点 0,0)。
    pub fn client_size(&self) -> (i16, i16) {
        client_size_of(self.id)
    }

    /// クライアント面のローカル矩形。
    pub fn client_rect(&self) -> Rect {
        let (w, h) = self.client_size();
        Rect::new(0, 0, w, h)
    }

    /// ウィジェット木の根を差し替える (`row` / `column`)。既存の木は捨てる。
    pub fn set_root(&self, root: WidgetId) -> GuiResult<()> {
        set_root_of(self.id, root)
    }

    /// 根から箱レイアウトを引き直し、全面を `invalidate` する (契約 U7)。
    pub fn relayout(&self) {
        relayout_of(self.id)
    }

    /// 損傷を申告する (クライアントローカル)。描くのは次の `Paint`。
    /// 実際の `OP_INVALIDATE` は 1 周分をまとめて送る (契約 P。[`crate::app::run_vt`])。
    pub fn invalidate(&self, rect: Rect) -> GuiResult<()> {
        invalidate_of(self.id, rect);
        Ok(())
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

    /// FEP の候補窓の位置 (契約 U2a)。テキストボックスのフォーカス時に送る。
    pub fn set_text_cursor(&self, x: i16, y: i16, visible: bool) -> GuiResult<()> {
        client::win_set_text_cursor(self.id, x, y, visible)
    }

    /// WM からフォーカスを持っているか (`Focus{in}` の記録)。
    pub fn is_focused(&self) -> bool {
        is_focused_of(self.id)
    }
}

impl Drop for Window {
    fn drop(&mut self) {
        drop_window(self.id);
    }
}

/* ================================================================ */
/*  登録表の後始末 (Drop と `Quit` の両方から)                        */
/* ================================================================ */

/// 登録表から 1 枚落とす (サーフェスとウィジェット木も解放)。
pub fn unregister(id: u32) {
    let slot = match s().win_slot(id) {
        Some(i) => i,
        None => return,
    };
    let root = s().windows[slot].root;
    if root != GUI_NONE {
        widget::free_tree(root as usize - 1);
    }
    let surf = s().windows[slot].surface;
    if !surf.is_null() {
        crate::surface::destroy_surface(surf);
    }
    s().windows[slot] = crate::uistate::WinEnt::EMPTY;
}

/// `Configure` を受けたときの張り替え: サーフェスを作り直し、木を並べ直す。
pub(crate) fn on_configure(id: u32, screen_rect: Rect) {
    let slot = match s().win_slot(id) {
        Some(i) => i,
        None => return,
    };
    let old = s().windows[slot].client;
    let same_size = old.w == screen_rect.w && old.h == screen_rect.h;
    let old_surf = s().windows[slot].surface;
    if !old_surf.is_null() {
        crate::surface::destroy_surface(old_surf);
    }
    s().windows[slot].client = screen_rect;
    s().windows[slot].surface = crate::surface::create_window_surface(screen_rect);
    if !same_size {
        let root = s().windows[slot].root;
        crate::layout::layout_window(root, screen_rect);
    }
}

/// 登録済みのウィンドウ枚数。0 になったらアプリは終われる。
pub fn count() -> usize {
    let st = s();
    let mut n = 0;
    let mut i = 0;
    while i < st.windows.len() {
        if st.windows[i].used {
            n += 1;
        }
        i += 1;
    }
    n
}
