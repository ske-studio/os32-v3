//! client.rs — 契約 T のスタブ。`gui_call` の包みはライブラリ側にある。
//!
//! ここにあるのはエラー型 (値だけ) とジャンプ表への転送。

use crate::shcall;
use os32api::gui::proto::{
    GuiEvent, GuiRgb, GuiSlotHeader, GUI_RING_CAPACITY, OS32_ERR_FULL, OS32_ERR_INVAL,
    OS32_ERR_NOSYS, OS32_ERR_STALE, OS32_ERR_VERSION,
};
use os32api::gui::stub as sh;
use os32api::gui::types::{Rect, Stats};

/* SHM の GUI 予約領域と取り込み tick 表 (契約 P2)。値だけなので写す。 */
pub use os32api::gui::proto::GUI_SHM_OFFSET;

/// 取り込み tick 表のスロット内オフセット (契約 P2)。
pub const SLOT_TRACE_OFF: usize =
    os32api::gui::proto::GUI_SLOT_ARGS_OFF + os32api::gui::proto::GUI_SLOT_ARGS_SIZE;
/// 取り込み tick 表の項目数。
pub const TRACE_ENTRIES: usize = 64;

/* ================================================================ */
/*  エラー (契約 T7) — GetLastError は作らない                        */
/* ================================================================ */

/// `gui_call` の負の戻り値 (`OS32_ERR_*`)。
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct GuiErr(pub i32);

impl GuiErr {
    pub const INVAL: GuiErr = GuiErr(OS32_ERR_INVAL);
    pub const NOSYS: GuiErr = GuiErr(OS32_ERR_NOSYS);
    pub const STALE: GuiErr = GuiErr(OS32_ERR_STALE);
    pub const VERSION: GuiErr = GuiErr(OS32_ERR_VERSION);
    pub const FULL: GuiErr = GuiErr(OS32_ERR_FULL);

    #[inline]
    pub const fn code(self) -> i32 {
        self.0
    }

    /// 人間が読む名前 (`dbg_print` 用。NUL 終端しない)。
    pub fn name(self) -> &'static [u8] {
        match self.0 {
            OS32_ERR_INVAL => b"ERR_ARG",
            OS32_ERR_NOSYS => b"ERR_NOSYS",
            OS32_ERR_STALE => b"ERR_STALE",
            OS32_ERR_VERSION => b"ERR_VERSION",
            OS32_ERR_FULL => b"ERR_FULL",
            _ => b"ERR",
        }
    }
}

pub type GuiResult<T> = Result<T, GuiErr>;

/// 0 / 負のエラーを `GuiResult<()>` へ。
#[inline]
pub(crate) fn ok0(r: i32) -> GuiResult<()> {
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(())
    }
}

/* ================================================================ */
/*  デバッグ出力                                                     */
/* ================================================================ */

/// 1 行のデバッグ出力 (NUL 終端不要)。
pub fn dbg_print(msg: &[u8]) {
    shcall!(
        sh::E_DBG_PRINT,
        extern "C" fn(*const u8, u32),
        msg.as_ptr(),
        msg.len() as u32
    )
}

/// 「文字列 + 整数」のデバッグ出力。
pub fn dbg_print_num(msg: &[u8], v: i32) {
    shcall!(
        sh::E_DBG_PRINT_NUM,
        extern "C" fn(*const u8, u32, i32),
        msg.as_ptr(),
        msg.len() as u32,
        v
    )
}

/* ================================================================ */
/*  スロット                                                         */
/* ================================================================ */

/// `OP_INIT` 済みか。
pub fn is_inited() -> bool {
    shcall!(sh::E_IS_INITED, extern "C" fn() -> u32) != 0
}

/// 割り当てられたスロット番号 (v1 は常に 0)。
pub fn slot() -> u32 {
    shcall!(sh::E_SLOT, extern "C" fn() -> u32)
}

/// スロット先頭 (デバッグ / `gui_bench` の予備領域読み出し用)。
pub fn slot_base() -> *mut u8 {
    shcall!(sh::E_SLOT_BASE, extern "C" fn() -> *mut u8)
}

/// 引数バッファ (8KB)。
pub fn args_ptr() -> *mut u8 {
    shcall!(sh::E_ARGS_PTR, extern "C" fn() -> *mut u8)
}

/// スロットヘッダを読む。
pub fn read_header() -> GuiSlotHeader {
    let mut h = GuiSlotHeader {
        proto_version: 0,
        flags: 0,
        seq: 0,
        ring_head: 0,
        ring_tail: 0,
        dropped: 0,
        reserved: 0,
    };
    shcall!(
        sh::E_READ_HEADER,
        extern "C" fn(*mut GuiSlotHeader),
        &mut h as *mut GuiSlotHeader
    );
    h
}

/// `gui_call` を 1 回。負の戻り値は [`GuiErr`] に写す (契約 T7)。
pub fn call(op: u32, arg: u32) -> GuiResult<i32> {
    let r = shcall!(sh::E_RAW_CALL, extern "C" fn(u32, u32) -> i32, op, arg);
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(r)
    }
}

/* ================================================================ */
/*  イベント取り出し (契約 T3)                                        */
/* ================================================================ */

/// `OP_POLL` 1 回の結果。
pub struct Poll {
    /// 取り出した件数 (`out` の先頭から)。
    pub count: usize,
    /// WM が取りこぼした打鍵数。
    pub dropped: u16,
    /// `OVERFLOW`: 入力状態を未知として扱う。
    pub overflow: bool,
}

/// リングに溜まったイベントを一括で取り出す (契約 T3)。
pub fn poll(out: &mut [GuiEvent]) -> GuiResult<Poll> {
    let cap = if out.len() < GUI_RING_CAPACITY { out.len() } else { GUI_RING_CAPACITY };
    let mut dropped: u16 = 0;
    let mut overflow: u8 = 0;
    let n = shcall!(
        sh::E_POLL,
        extern "C" fn(*mut GuiEvent, u32, *mut u16, *mut u8) -> i32,
        out.as_mut_ptr(),
        cap as u32,
        &mut dropped as *mut u16,
        &mut overflow as *mut u8,
    );
    if n < 0 {
        return Err(GuiErr(n));
    }
    Ok(Poll { count: n as usize, dropped, overflow: overflow != 0 })
}

/// `OP_WAIT` (GetMessage の代替)。**ハンドラの中から呼んではいけない**。
pub fn wait(timeout_ticks: u32) -> GuiResult<i32> {
    let r = shcall!(sh::E_WAIT, extern "C" fn(u32) -> i32, timeout_ticks);
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(r)
    }
}

pub fn enter_handler() {
    shcall!(sh::E_ENTER_HANDLER, extern "C" fn())
}

pub fn leave_handler() {
    shcall!(sh::E_LEAVE_HANDLER, extern "C" fn())
}

/// `OP_COMMIT` (契約 G4)。**ループ 1 周に 1 回**。
pub fn commit(window: u32) -> GuiResult<()> {
    ok0(shcall!(sh::E_COMMIT, extern "C" fn(u32) -> i32, window))
}

/// 損傷を申告する (契約 G4)。`rect` はクライアントローカル。
pub fn invalidate(window: u32, rect: Rect) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_INVALIDATE,
        extern "C" fn(u32, Rect) -> i32,
        window,
        rect
    ))
}

/// カウンタ (契約 G7)。
pub fn stats() -> GuiResult<Stats> {
    let mut out = Stats::ZERO;
    let r = shcall!(
        sh::E_CLIENT_STATS,
        extern "C" fn(*mut Stats) -> i32,
        &mut out as *mut Stats
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(out)
    }
}

/// 取り込み tick (契約 P2)。
pub fn trace_tick(serial: u16) -> Option<u16> {
    let mut t: u16 = 0;
    let r = shcall!(
        sh::E_TRACE_TICK,
        extern "C" fn(u16, *mut u16) -> i32,
        serial,
        &mut t as *mut u16
    );
    if r > 0 {
        Some(t)
    } else {
        None
    }
}

/* ================================================================ */
/*  ウィンドウ op (契約 U1) — 所有型 `window::Window` の下請け        */
/* ================================================================ */

pub fn win_destroy(window: u32) -> GuiResult<()> {
    ok0(shcall!(sh::E_WIN_DESTROY, extern "C" fn(u32) -> i32, window))
}

pub fn win_raise(window: u32) -> GuiResult<()> {
    ok0(shcall!(sh::E_WIN_RAISE, extern "C" fn(u32) -> i32, window))
}

pub fn win_set_focus(window: u32) -> GuiResult<()> {
    ok0(shcall!(sh::E_WIN_SET_FOCUS, extern "C" fn(u32) -> i32, window))
}

/// クライアント矩形 (**原点は画面絶対座標**)。
pub fn win_client_rect(window: u32) -> GuiResult<Rect> {
    let mut r = Rect::EMPTY;
    let rc = shcall!(
        sh::E_WIN_CLIENT_RECT,
        extern "C" fn(u32, *mut Rect) -> i32,
        window,
        &mut r as *mut Rect
    );
    if rc < 0 {
        Err(GuiErr(rc))
    } else {
        Ok(r)
    }
}

pub fn win_move(window: u32, x: i16, y: i16) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_WIN_MOVE,
        extern "C" fn(u32, i32, i32) -> i32,
        window,
        x as i32,
        y as i32
    ))
}

pub fn win_resize(window: u32, w: i16, h: i16) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_WIN_RESIZE,
        extern "C" fn(u32, i32, i32) -> i32,
        window,
        w as i32,
        h as i32
    ))
}

pub fn win_show(window: u32, show: bool) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_WIN_SHOW,
        extern "C" fn(u32, u32) -> i32,
        window,
        show as u32
    ))
}

/// タイトルを差し替える。UTF-8 境界で 255B に切り詰める (契約 U9)。
pub fn win_set_title(window: u32, title: &[u8]) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_WIN_SET_TITLE,
        extern "C" fn(u32, *const u8, u32) -> i32,
        window,
        title.as_ptr(),
        title.len() as u32
    ))
}

/// FEP の候補窓の位置 (契約 U2a)。
pub fn win_set_text_cursor(window: u32, x: i16, y: i16, visible: bool) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_WIN_SET_TEXT_CURSOR,
        extern "C" fn(u32, i32, i32, u32) -> i32,
        window,
        x as i32,
        y as i32,
        visible as u32
    ))
}

/* ================================================================ */
/*  パレットのリース (契約 G8)                                       */
/* ================================================================ */

/// フォーカス中のウィンドウが自分の色を入れる (契約 G8)。16 色版のみ。
pub fn lease_palette(first: u16, entries: &[GuiRgb]) -> GuiResult<i32> {
    if entries.is_empty() || entries.len() > 16 {
        return Err(GuiErr::INVAL);
    }
    let r = shcall!(
        sh::E_LEASE_PALETTE,
        extern "C" fn(u16, *const GuiRgb, u32) -> i32,
        first,
        entries.as_ptr(),
        entries.len() as u32
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(r)
    }
}

/* ================================================================ */
/*  タイマ (契約 U5)                                                  */
/* ================================================================ */

pub fn timer_set(window: u32, timer_id: u8, interval_ticks: u16, repeat: bool) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_TIMER_SET,
        extern "C" fn(u32, u32, u32, u32) -> i32,
        window,
        timer_id as u32,
        interval_ticks as u32,
        repeat as u32
    ))
}

pub fn timer_kill(window: u32, timer_id: u8) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_TIMER_KILL,
        extern "C" fn(u32, u32) -> i32,
        window,
        timer_id as u32
    ))
}

/* ================================================================ */
/*  文字列 (契約 U9)                                                  */
/* ================================================================ */

/// UTF-8 の先頭バイトから符号単位のバイト長を得る (不正バイトは 1)。
pub fn utf8_seq_len(b: u8) -> usize {
    shcall!(sh::E_UTF8_SEQ_LEN, extern "C" fn(u8) -> u32, b) as usize
}

/// `max` バイト以内に収まる UTF-8 の切れ目を返す (NUL 終端も尊重)。
pub fn utf8_truncate(s: &[u8], max: usize) -> usize {
    shcall!(
        sh::E_UTF8_TRUNCATE,
        extern "C" fn(*const u8, u32, u32) -> u32,
        s.as_ptr(),
        s.len() as u32,
        max as u32
    ) as usize
}
