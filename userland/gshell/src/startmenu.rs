//! startmenu.rs — Start メニューとデスクトップの右クリックメニュー
//! (契約 V12-D の D2 / D4、票 W3 §4・§8)。
//!
//! どちらも**WM 自身のオーバーレイ**で、popup 用の Window ABI は作らない
//! (契約 D4)。`Window` 表にも載らないので、アプリの上限 16 を食わない。
//! 可視領域の計算 ([`crate::visible`]) がメニュー矩形を穴として引くので、
//! 下のアプリはメニューの下を描かず、閉じたときに露出分の `Paint` を受ける
//! (モーダルと同じ扱い)。
//!
//! ```text
//!   Start (root)          Programs / File Manager / Run... / Settings... /
//!                         CUI mode / Shut Down
//!   Start (programs)      /usr/bin/*.bin を最大 96 件 (超過は "..." 行)
//!   Context (右クリック)  File Manager / Run... / Refresh Programs
//!   Window (GRPH+SPACE)   Restore / Move / Size / Minimize / Maximize / Close
//! ```
//!
//! 窓メニュー (票 KBD_NAV §1-2) は対象の窓の id を持ち、項目の実行は
//! [`crate::kbdnav::winmenu_run`] に任せる。使えない項目は灰色で描き、選んでも
//! 何もしない。
//!
//! **ディレクトリ走査は X3 でだけ行う** (契約 S8 / §9)。メニューを開いた
//! ときに `scan_pending` を立て、[`x3_cycle`] が 1 回だけ `sys_ls` する。
//! 開いている間は cache し、`Refresh Programs` と session の実行で捨てる。
//!
//! `CUI mode` / `Shut Down` は **WM 内蔵の確認ダイアログ (Yes/No) を経てから**
//! SessionAction を立てる (契約 S6)。1 キーで無言のまま CUI へ落ちる経路は作らない。

use crate::wm::{GuiState, Rect};
use crate::{chrome, lease, modal, session, visible};
use os32api::gfx;
use os32api::gui::proto::{
    GUI_COLOR_FACE, GUI_COLOR_LIGHT, GUI_COLOR_SEL_BG, GUI_COLOR_SEL_TEXT, GUI_COLOR_SHADOW,
    GUI_COLOR_TEXT, GUI_COLOR_WINDOW, GUI_MODAL_YES_NO,
};

/* ================================================================ */
/*  寸法                                                             */
/* ================================================================ */
/// 1 行の高さ。
const ITEM_H: i32 = 18;
/// 枠 (1px) + 内側の余白。
const BORDER: i32 = 2;
/// Start の root メニューの幅。
const ROOT_W: i32 = 160;
/// Programs 一覧の幅。
const PROG_W: i32 = 200;
/// Programs 一覧に出す行数 (固定。PM が座標で叩けるように動かさない)。
const PROG_ROWS: usize = 12;
/// 右クリックメニューの幅。
const CTX_W: i32 = 160;
/// 窓メニューの幅。
const WIN_W: i32 = 120;

/// `/usr/bin` から拾う最大件数 (契約 D2)。超過は "..." 行で示す。
pub const MAX_PROGS: usize = 96;
/// 実行ファイル名の最大長。
const NAME_LEN: usize = 40;
/// 起動パスの組み立て先。
const PATH_LEN: usize = 128;
/// 走査するディレクトリ (契約 D2)。
static PROG_DIR: &[u8] = b"/usr/bin\0";

/* root メニューの項目。**行番号は台本 (`tools/gui_gate.py`) の
 * `ROW_PROGRAMS` / `ROW_FILEMAN` / `ROW_RUN` / `ROW_SETTINGS` / `ROW_CUI` /
 * `ROW_HALT`) と 1 対 1**。Settings... を Run... の次に足したので
 * CUI mode / Shut Down が 1 行下がった (票 S4 §3)。 */
const IT_PROGRAMS: usize = 0;
const IT_FILEMAN: usize = 1;
const IT_RUN: usize = 2;
const IT_SETTINGS: usize = 3;
const IT_CUI: usize = 4;
const IT_HALT: usize = 5;
pub const ROOT_ITEMS: usize = 6;

/* context メニューの項目。 */
const CT_FILEMAN: usize = 0;
const CT_RUN: usize = 1;
const CT_REFRESH: usize = 2;
const CTX_ITEMS: usize = 3;

/* 種別。 */
const KIND_NONE: u8 = 0;
const KIND_ROOT: u8 = 1;
const KIND_PROGS: u8 = 2;
const KIND_CTX: u8 = 3;
const KIND_WIN: u8 = 4;

/// File Manager (契約 D2 / F1)。
static FILER_BIN: &[u8] = b"/usr/bin/filer.bin\0";

/* ================================================================ */
/*  DirEntry_Ext (os32_kapi_shared.h) の写し                         */
/* ================================================================ */
#[repr(C)]
struct DirEntryExt {
    name: [u8; 256],
    size: u32,
    ftype: u8,
}
const FILE_TYPE_DIR: u8 = 2;

/* ================================================================ */
/*  状態                                                             */
/* ================================================================ */
pub struct Menu {
    kind: u8,
    pub rect: Rect,
    nitems: usize,
    cursor: usize,
    scroll: usize,
    /// メニューを開いた直後に 1 回だけ走査する (X3)。
    scan_pending: bool,
    /// 押下でメニューが閉じた直後の**離しを捨てる**。閉じた後の up edge が
    /// そのままアプリへ届くと、押していないボタンの離しが飛ぶ。
    swallow_up: bool,
    /// 窓メニューの対象 (完全な WindowId、KIND_WIN のときだけ)。
    win: u32,
    /* ---- /usr/bin の cache ---- */
    progs: [[u8; NAME_LEN]; MAX_PROGS],
    nprogs: usize,
    prog_overflow: bool,
    progs_valid: bool,
}

impl Menu {
    const NEW: Menu = Menu {
        kind: KIND_NONE,
        rect: Rect::EMPTY,
        nitems: 0,
        cursor: 0,
        scroll: 0,
        scan_pending: false,
        swallow_up: false,
        win: 0,
        progs: [[0; NAME_LEN]; MAX_PROGS],
        nprogs: 0,
        prog_overflow: false,
        progs_valid: false,
    };
}

struct MenuCell(core::cell::UnsafeCell<Menu>);
unsafe impl Sync for MenuCell {}
static MENU: MenuCell = MenuCell(core::cell::UnsafeCell::new(Menu::NEW));

#[inline]
fn m() -> &'static mut Menu {
    unsafe { &mut *MENU.0.get() }
}

/// メニューが開いているか。
#[inline]
pub fn is_open() -> bool {
    m().kind != KIND_NONE
}

/// ホスト TDD の初期化 (`mocks::init`)。`static` を跨いで前の試験の
/// 開きっぱなしが次の試験の合成に写り込まないようにする。
#[allow(dead_code)]
pub fn reset() {
    *m() = Menu::NEW;
}

/// Start ボタンを沈めて描くか (root / programs のどちらでも押されたまま)。
#[inline]
pub fn start_pressed() -> bool {
    let k = m().kind;
    k == KIND_ROOT || k == KIND_PROGS
}

/// メニューの外形 (画面座標)。閉じていれば空。
#[inline]
pub fn rect() -> Rect {
    let mm = m();
    if mm.kind == KIND_NONE {
        Rect::EMPTY
    } else {
        mm.rect
    }
}

/* ================================================================ */
/*  開く / 閉じる (X3 だけ。契約 T8 / S8)                            */
/* ================================================================ */

/// Start ボタン: 開いていれば閉じ、閉じていれば root を開く。
pub fn toggle_start(st: &mut GuiState) {
    if is_open() {
        close(st);
    } else {
        open_root(st);
    }
}

fn open_root(st: &mut GuiState) {
    let h = (ROOT_ITEMS as i32) * ITEM_H + BORDER * 2;
    let y = st.screen_h - crate::taskbar::TASKBAR_H - h;
    set_open(st, KIND_ROOT, Rect::new(2, y, ROOT_W, h), ROOT_ITEMS);
}

fn open_programs(st: &mut GuiState) {
    let h = (PROG_ROWS as i32) * ITEM_H + BORDER * 2;
    let y = st.screen_h - crate::taskbar::TASKBAR_H - h;
    let n = prog_item_count();
    set_open(st, KIND_PROGS, Rect::new(2, y, PROG_W, h), n);
    /* cache が無ければ X3 で 1 回だけ走査する (契約 D2: X4 では走らせない)。 */
    if !m().progs_valid {
        m().scan_pending = true;
    }
}

/// デスクトップ / タスクバーの右クリックメニュー (契約 D4)。
pub fn open_context(st: &mut GuiState, mx: i32, my: i32) {
    let h = (CTX_ITEMS as i32) * ITEM_H + BORDER * 2;
    let wa = crate::wm::work_area(st);
    let mut x = mx;
    let mut y = my;
    if x + CTX_W > wa.right() {
        x = wa.right() - CTX_W;
    }
    if x < wa.x {
        x = wa.x;
    }
    if y + h > wa.bottom() {
        y = wa.bottom() - h;
    }
    if y < wa.y {
        y = wa.y;
    }
    set_open(st, KIND_CTX, Rect::new(x, y, CTX_W, h), CTX_ITEMS);
}

/// 窓メニュー (GRPH+SPACE、票 KBD_NAV §1-2)。(x, y) は窓のクライアントの左上。
pub fn open_winmenu(st: &mut GuiState, win: u32, x: i32, y: i32) {
    let h = (crate::kbdnav::WM_ITEMS as i32) * ITEM_H + BORDER * 2;
    let wa = crate::wm::work_area(st);
    let mut x = x;
    let mut y = y;
    if x + WIN_W > wa.right() {
        x = wa.right() - WIN_W;
    }
    if x < wa.x {
        x = wa.x;
    }
    if y + h > wa.bottom() {
        y = wa.bottom() - h;
    }
    if y < wa.y {
        y = wa.y;
    }
    set_open(st, KIND_WIN, Rect::new(x, y, WIN_W, h), crate::kbdnav::WM_ITEMS);
    m().win = win;
    /* 初期カーソルは最初の使える項目 (ふつうの窓では灰色の Restore ではなく
     * Move、代行レビュー P3-4)。 */
    let mut i = 0;
    while i < crate::kbdnav::WM_ITEMS {
        if crate::kbdnav::winmenu_enabled(st, win, i) {
            m().cursor = i;
            break;
        }
        i += 1;
    }
}

/// Shut Down の確認 (Start の Shut Down と、窓が無いときの GRPH+f･4)。
/// 契約 S6: 確認 (Yes/No) を経てからでないと止めない。
pub fn confirm_halt(st: &mut GuiState) {
    modal::open_wm_message(
        st,
        GUI_MODAL_YES_NO,
        "システムを停止します。保存していない内容は失われます\0".as_bytes(),
        modal::WM_PURPOSE_CONFIRM_HALT,
    );
}

fn set_open(st: &mut GuiState, kind: u8, r: Rect, nitems: usize) {
    let old = rect();
    {
        let mm = m();
        mm.kind = kind;
        mm.rect = r;
        mm.nitems = nitems;
        mm.cursor = 0;
        mm.scroll = 0;
    }
    if !old.is_empty() {
        st.dirty_screen(old);
    }
    st.dirty_screen(r);
    visible::recompute_and_expose(st);
}

/// メニューを閉じる。下地 (デスクトップ + クローム + アプリ) を描き直させる。
pub fn close(st: &mut GuiState) {
    let r = rect();
    if r.is_empty() {
        return;
    }
    {
        let mm = m();
        mm.kind = KIND_NONE;
        mm.rect = Rect::EMPTY;
        /* 走査待ちは取り消す。cache 自体は残し、`Refresh Programs` と
         * session の実行でだけ捨てる (契約 D2 は「捨ててよい」で必須ではない)。 */
        mm.scan_pending = false;
    }
    st.dirty_screen(r);
    /* 下のアプリはメニューの下を描いていない。露出分の Paint を返す。 */
    let mut i = 0;
    while i < st.windows.len() {
        if st.windows[i].used && st.windows[i].visible {
            let (ox, oy) = st.windows[i].client_origin();
            crate::damage::add_dirty(&mut st.windows[i], r.translate(-ox, -oy));
        }
        i += 1;
    }
    visible::recompute_and_expose(st);
}

/* ================================================================ */
/*  X3 の周期 (ディレクトリ走査。契約 S8: X4 では絶対にやらない)      */
/* ================================================================ */

/// X3 (`wm::wm_cycle` の Wait / Standalone) で 1 回だけ呼ぶ。
pub fn x3_cycle(st: &mut GuiState) {
    if !m().scan_pending {
        return;
    }
    m().scan_pending = false;
    scan_programs();
    let n = prog_item_count();
    m().nitems = n;
    if m().cursor >= n && n > 0 {
        m().cursor = n - 1;
    }
    let r = rect();
    if !r.is_empty() {
        st.dirty_screen(r);
    }
}

/// 一覧に出す行数 (末尾の "..." を含む)。
fn prog_item_count() -> usize {
    let mm = m();
    mm.nprogs + if mm.prog_overflow { 1 } else { 0 }
}

extern "C" fn ls_cb(entry: *const DirEntryExt, _ctx: *mut u8) {
    let mm = m();
    if entry.is_null() {
        return;
    }
    let e = unsafe { &*entry };
    if e.ftype == FILE_TYPE_DIR {
        return;
    }
    if !has_bin_suffix(&e.name) {
        return;
    }
    if mm.nprogs >= MAX_PROGS {
        mm.prog_overflow = true; /* 超過は "..." 行 1 本で示す (契約 D2) */
        return;
    }
    let mut i = 0;
    while i < NAME_LEN - 1 && e.name[i] != 0 {
        mm.progs[mm.nprogs][i] = e.name[i];
        i += 1;
    }
    mm.progs[mm.nprogs][i] = 0;
    /* 名前が 40B に収まらないものは起動パスを組めないので出さない。 */
    if e.name[i] != 0 {
        return;
    }
    mm.nprogs += 1;
}

/// `name` が ".bin" で終わるか (NUL 終端、256B)。
fn has_bin_suffix(name: &[u8; 256]) -> bool {
    let mut n = 0;
    while n < 256 && name[n] != 0 {
        n += 1;
    }
    if n < 5 {
        return false;
    }
    name[n - 4] == b'.' && name[n - 3] == b'b' && name[n - 2] == b'i' && name[n - 1] == b'n'
}

fn scan_programs() {
    {
        let mm = m();
        mm.nprogs = 0;
        mm.prog_overflow = false;
    }
    unsafe {
        (os32api::api().sys_ls)(
            PROG_DIR.as_ptr(),
            ls_cb as *const () as *mut u8,
            core::ptr::null_mut(),
        );
    }
    m().progs_valid = true;
}

/* ================================================================ */
/*  キーボード (X3 だけ)                                             */
/* ================================================================ */

/* スキャンコード (drivers/kbd.h)。 */
const SC_ESC: u8 = 0x00;
const SC_RETURN: u8 = 0x1C;
const SC_SPACE: u8 = 0x34;
const SC_ROLLUP: u8 = 0x36;
const SC_ROLLDOWN: u8 = 0x37;
const SC_UP: u8 = 0x3A;
const SC_LEFT: u8 = 0x3B;
const SC_DOWN: u8 = 0x3D;
const SC_HOME: u8 = 0x3E;

/// キー 1 件 (押下のみ)。メニューが閉じたら true。
pub fn on_key(st: &mut GuiState, scan: u8) -> bool {
    match scan {
        SC_ESC | SC_LEFT => {
            if m().kind == KIND_PROGS {
                open_root(st); /* Programs → root へ戻る */
                return false;
            }
            close(st);
            return true;
        }
        SC_UP => move_cursor(st, -1),
        SC_DOWN => move_cursor(st, 1),
        SC_ROLLUP => move_cursor(st, -(PROG_ROWS as i32)),
        SC_ROLLDOWN => move_cursor(st, PROG_ROWS as i32),
        SC_HOME => move_cursor(st, -(MAX_PROGS as i32)),
        SC_RETURN | SC_SPACE => return activate(st),
        _ => {}
    }
    false
}

fn move_cursor(st: &mut GuiState, delta: i32) {
    {
        let mm = m();
        if mm.nitems == 0 {
            return;
        }
        let mut c = mm.cursor as i32 + delta;
        if c < 0 {
            c = 0;
        }
        if c >= mm.nitems as i32 {
            c = mm.nitems as i32 - 1;
        }
        mm.cursor = c as usize;
        let rows = visible_rows(mm.kind);
        if mm.cursor < mm.scroll {
            mm.scroll = mm.cursor;
        } else if mm.cursor >= mm.scroll + rows {
            mm.scroll = mm.cursor + 1 - rows;
        }
    }
    let r = rect();
    st.dirty_screen(r);
}

#[inline]
fn visible_rows(kind: u8) -> usize {
    match kind {
        KIND_PROGS => PROG_ROWS,
        KIND_CTX => CTX_ITEMS,
        KIND_WIN => crate::kbdnav::WM_ITEMS,
        _ => ROOT_ITEMS,
    }
}

/* ================================================================ */
/*  マウス (X3 だけ)                                                 */
/* ================================================================ */

/// 行 i (画面上の行番号 0..rows) の矩形。
fn row_rect(mm: &Menu, row: usize) -> Rect {
    Rect::new(
        mm.rect.x + BORDER,
        mm.rect.y + BORDER + (row as i32) * ITEM_H,
        mm.rect.w - BORDER * 2,
        ITEM_H,
    )
}

/// メニューの押下で閉じた直後か (対になる離しを捨てるため)。
/// 呼ぶと落ちる (1 回だけ効く)。
#[inline]
pub fn take_swallow_up() -> bool {
    let mm = m();
    let v = mm.swallow_up;
    mm.swallow_up = false;
    v
}

/// メニューの押下で閉じた直後か (落とさずに見るだけ。X4 の領分判定用)。
#[inline]
pub fn swallow_up_pending() -> bool {
    m().swallow_up
}

/// 「押下でメニューを閉じた」を記録する (対になる離しを 1 回だけ捨てる)。
#[inline]
pub fn set_swallow_up() {
    m().swallow_up = true;
}

/// 押下 1 件。メニューが閉じたら true。**メニューが開いている間、押下は
/// すべて WM が消費する** (外側は「閉じる」)。
pub fn on_button(st: &mut GuiState, mx: i32, my: i32) -> bool {
    if !rect().contains(mx, my) {
        close(st);
        m().swallow_up = true;
        return true;
    }
    let rows = visible_rows(m().kind);
    let mut row = 0;
    while row < rows {
        let rr = row_rect(m(), row);
        if rr.contains(mx, my) {
            let idx = m().scroll + row;
            if idx >= m().nitems {
                return false;
            }
            m().cursor = idx;
            let closed = activate(st);
            if closed {
                m().swallow_up = true;
            }
            return closed;
        }
        row += 1;
    }
    false
}

/* ================================================================ */
/*  項目の実行                                                       */
/* ================================================================ */

/// フォーカス中の項目を実行する。メニューが閉じたら true。
fn activate(st: &mut GuiState) -> bool {
    let (kind, cursor, nitems) = {
        let mm = m();
        (mm.kind, mm.cursor, mm.nitems)
    };
    if nitems == 0 {
        return false;
    }
    match kind {
        KIND_ROOT => match cursor {
            IT_PROGRAMS => {
                open_programs(st);
                false
            }
            IT_FILEMAN => {
                close(st);
                let _ = session::set_wm_launch(st, FILER_BIN);
                true
            }
            IT_RUN => {
                close(st);
                modal::open_wm_input(st, b"Run: absolute path\0", modal::WM_PURPOSE_RUN);
                true
            }
            IT_SETTINGS => {
                close(st);
                /* **DB には触らない** — top-level への予約だけ (票 S4 §3 の B1)。
                 * ここはアプリが `OP_WAIT` 中なら `handler` の文脈で走る。 */
                crate::settings::request_open();
                true
            }
            IT_CUI => {
                close(st);
                /* 契約 S6: 確認 (Yes/No) を経てからでないと CUI へ落とさない。 */
                modal::open_wm_message(
                    st,
                    GUI_MODAL_YES_NO,
                    "CUI モードへ切り替えます。GUI の状態は保持されません\0".as_bytes(),
                    modal::WM_PURPOSE_CONFIRM_CUI,
                );
                true
            }
            IT_HALT => {
                close(st);
                confirm_halt(st);
                true
            }
            _ => false,
        },
        KIND_PROGS => {
            /* 末尾の "..." (超過表示) は起動できない。 */
            if cursor >= m().nprogs {
                return false;
            }
            let mut path = [0u8; PATH_LEN];
            let n = build_prog_path(cursor, &mut path);
            close(st);
            if n > 0 {
                let _ = session::set_wm_launch(st, &path);
            }
            true
        }
        KIND_CTX => match cursor {
            CT_FILEMAN => {
                close(st);
                let _ = session::set_wm_launch(st, FILER_BIN);
                true
            }
            CT_RUN => {
                close(st);
                modal::open_wm_input(st, b"Run: absolute path\0", modal::WM_PURPOSE_RUN);
                true
            }
            CT_REFRESH => {
                /* cache を捨てるだけ。走査は次に Programs を開いた X3 で。 */
                m().progs_valid = false;
                close(st);
                true
            }
            _ => false,
        },
        KIND_WIN => {
            let win = m().win;
            if !crate::kbdnav::winmenu_enabled(st, win, cursor) {
                return false; /* 灰色の項目は何もしない (メニューも閉じない) */
            }
            close(st);
            crate::kbdnav::winmenu_run(st, win, cursor);
            true
        }
        _ => false,
    }
}

/// `/usr/bin/` + 名前 (NUL 終端)。戻り値はバイト数 (0 = 組めない)。
fn build_prog_path(idx: usize, out: &mut [u8; PATH_LEN]) -> usize {
    let dir = b"/usr/bin/";
    let mut n = 0;
    while n < dir.len() {
        out[n] = dir[n];
        n += 1;
    }
    let mm = m();
    let mut i = 0;
    while i < NAME_LEN && mm.progs[idx][i] != 0 && n < PATH_LEN - 1 {
        out[n] = mm.progs[idx][i];
        n += 1;
        i += 1;
    }
    out[n] = 0;
    if i == 0 {
        return 0;
    }
    n
}

/* ================================================================ */
/*  描画 (X3。wm::composite_rect の最後)                              */
/* ================================================================ */

/// `clip` に掛かるならメニュー全体を描き直す (kcg にクリップが無いため)。
pub fn draw(st: &GuiState, clip: Rect) {
    let mm = m();
    if mm.kind == KIND_NONE || !mm.rect.intersects(&clip) {
        return;
    }
    let mono = lease::mono(st);
    let face = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_FACE };
    let r = mm.rect;
    unsafe {
        gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, face);
        gfx::gfx_rect(r.x, r.y, r.w, r.h, GUI_COLOR_TEXT);
        if mono {
            chrome::dither50(r.x + 1, r.y + r.h - 2, r.w - 2, 1, GUI_COLOR_TEXT);
        } else {
            gfx::gfx_hline(r.x + 1, r.y + 1, r.w - 2, GUI_COLOR_LIGHT);
            gfx::gfx_vline(r.x + 1, r.y + 1, r.h - 2, GUI_COLOR_LIGHT);
            gfx::gfx_hline(r.x + 1, r.y + r.h - 2, r.w - 2, GUI_COLOR_SHADOW);
            gfx::gfx_vline(r.x + r.w - 2, r.y + 1, r.h - 2, GUI_COLOR_SHADOW);
        }
        gfx::kcg_set_scale(1);
    }
    let rows = visible_rows(mm.kind);
    let mut row = 0;
    while row < rows {
        let idx = mm.scroll + row;
        let rr = row_rect(mm, row);
        let sel = idx == mm.cursor && idx < mm.nitems;
        let bg = if !sel {
            face
        } else if mono {
            GUI_COLOR_TEXT
        } else {
            GUI_COLOR_SEL_BG
        };
        let fg = if !sel {
            if item_enabled(st, mm, idx) {
                GUI_COLOR_TEXT
            } else {
                GUI_COLOR_SHADOW
            }
        } else if mono {
            GUI_COLOR_WINDOW
        } else {
            GUI_COLOR_SEL_TEXT
        };
        unsafe {
            gfx::gfx_fill_rect(rr.x, rr.y, rr.w, rr.h, bg);
        }
        if idx < mm.nitems {
            let mut buf = [0u8; NAME_LEN + 1];
            let label = item_label(mm, idx, &mut buf);
            unsafe {
                gfx::kcg_draw_utf8(rr.x + 4, rr.y + 1, label, fg, bg);
            }
        }
        row += 1;
    }
}

/// root メニューの項目 idx のラベル (**NUL を含まない**)。
///
/// `tools/gui_gate.py` の `ROW_*` が指す行の中身を試験から確かめるための窓口
/// (票 S4 §5 の (7))。行の順が変わると台本が別の項目に当たるので、
/// 「6 項目・この順」をここで固定する。
pub fn root_label(idx: usize) -> &'static [u8] {
    let p = match idx {
        IT_PROGRAMS => b"Programs\0".as_slice(),
        IT_FILEMAN => b"File Manager\0".as_slice(),
        IT_RUN => b"Run...\0".as_slice(),
        IT_SETTINGS => b"Settings...\0".as_slice(),
        IT_CUI => b"CUI mode\0".as_slice(),
        IT_HALT => b"Shut Down\0".as_slice(),
        _ => b"\0".as_slice(),
    };
    &p[..p.len() - 1]
}

/// 窓メニューの項目 idx のラベル (**NUL を含まない**)。
pub fn win_label(idx: usize) -> &'static [u8] {
    let p = match idx {
        crate::kbdnav::WM_RESTORE => b"Restore\0".as_slice(),
        crate::kbdnav::WM_MOVE => b"Move\0".as_slice(),
        crate::kbdnav::WM_SIZE => b"Size\0".as_slice(),
        crate::kbdnav::WM_MINIMIZE => b"Minimize\0".as_slice(),
        crate::kbdnav::WM_MAXIMIZE => b"Maximize\0".as_slice(),
        crate::kbdnav::WM_CLOSE => b"Close\0".as_slice(),
        _ => b"\0".as_slice(),
    };
    &p[..p.len() - 1]
}

/// 項目が使えるか (窓メニューだけが灰色の項目を持つ)。
fn item_enabled(st: &GuiState, mm: &Menu, idx: usize) -> bool {
    if mm.kind != KIND_WIN {
        return true;
    }
    crate::kbdnav::winmenu_enabled(st, mm.win, idx)
}

/// いまのカーソル行 (試験の観測点)。
#[allow(dead_code)]
pub fn cursor() -> usize {
    m().cursor
}

/// 窓メニューが開いていればその対象 (試験の観測点)。
#[allow(dead_code)]
pub fn winmenu_target() -> Option<u32> {
    if m().kind == KIND_WIN {
        Some(m().win)
    } else {
        None
    }
}

/// 項目 idx のラベル (NUL 終端のポインタ)。Programs は cache から組む。
fn item_label(mm: &Menu, idx: usize, buf: &mut [u8; NAME_LEN + 1]) -> *const u8 {
    match mm.kind {
        /* 文字列は [`root_label`] が正典 (試験が見るのと同じ表)。 */
        KIND_ROOT => root_label(idx).as_ptr(),
        KIND_CTX => match idx {
            CT_FILEMAN => b"File Manager\0".as_ptr(),
            CT_RUN => b"Run...\0".as_ptr(),
            _ => b"Refresh Programs\0".as_ptr(),
        },
        KIND_WIN => {
            let l = win_label(idx);
            let mut i = 0;
            while i < l.len() && i < NAME_LEN {
                buf[i] = l[i];
                i += 1;
            }
            buf[i] = 0;
            buf.as_ptr()
        }
        _ => {
            if idx >= mm.nprogs {
                return b"...\0".as_ptr();
            }
            let mut i = 0;
            while i < NAME_LEN && mm.progs[idx][i] != 0 {
                buf[i] = mm.progs[idx][i];
                i += 1;
            }
            buf[i] = 0;
            buf.as_ptr()
        }
    }
}

/// アプリの COMMIT (X2) がメニューに掛かったら描き直す (保険)。
pub fn refresh_if_hit(st: &GuiState, r: Rect) -> bool {
    let mr = rect();
    if mr.is_empty() || !mr.intersects(&r) {
        return false;
    }
    draw(st, mr);
    true
}

/// SessionAction の実行と `Refresh Programs` で cache を捨てる
/// (契約 D2 の「menu close / session change で cache を捨ててよい」)。
#[inline]
pub fn invalidate_cache() {
    m().progs_valid = false;
}
