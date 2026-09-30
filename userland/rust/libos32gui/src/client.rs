//! client.rs — `gui_call` の薄い包み (契約 T1 / T2 / T2a / T3 / T7)。票 C2 作業 1・2。
//!
//! アプリは `gui_call` を直接触らない。この層が
//!   - `OP_INIT` でスロット番号を受け、`MEM_SHM_GUI_BASE + n × 16KB` を
//!     ヘッダ / 要求 / 応答 / リング / 引数バッファに切る (K1 のオフセット)、
//!   - `proto_version` を照合し (不一致は `ERR_VERSION`)、
//!   - 戻り値 `< 0` を [`GuiErr`] に写す (**GetLastError は作らない** — 契約 T7)
//! ところまでを持つ。所有型 (`Window` / `Timer`) とウィジェット木はこの上。
//!
//! **鉄則 (契約 P)**: プリミティブごとに syscall しない。1 周の `gui_call` は
//! `POLL` + `COMMIT` + `WAIT` (+ 状態変更) だけ。描画はサーフェスへ直接 (C1 の G API)。
//! ポインタは SHM に載せない。文字列は長さ前置で引数 / 要求ブロックへ。

use core::cell::UnsafeCell;
use core::ptr;

use os32api::gui::proto::{
    GuiEvent, GuiRect16, GuiReqInvalidate, GuiReqLease, GuiReqTextCursor, GuiReqTimerKill, GuiReqTimerSet,
    GuiReqWinMove, GuiReqWinResize, GuiReqWinShow, GuiReqWinTitle, GuiReqWindow, GuiRespRect,
    GuiRgb, GuiSlotHeader, GuiString, GuiWinSpec, GUI_HDR_FLAG_OVERFLOW, GUI_OP_COMMIT,
    GUI_OP_INIT, GUI_OP_INVALIDATE, GUI_OP_LEASE_PALETTE, GUI_OP_POLL, GUI_OP_STATS,
    GUI_OP_TIMER_KILL, GUI_OP_TIMER_SET, GUI_OP_WAIT,
    GUI_OP_WIN_CLIENT_RECT, GUI_OP_WIN_CREATE, GUI_OP_WIN_DESTROY, GUI_OP_WIN_MOVE,
    GUI_OP_WIN_RAISE, GUI_OP_WIN_RESIZE, GUI_OP_WIN_SET_FOCUS, GUI_OP_WIN_SET_TEXT_CURSOR,
    GUI_OP_WIN_SET_TITLE, GUI_OP_WIN_SHOW, GUI_PROTO_VERSION, GUI_RING_CAPACITY,
    GUI_SLOT_ARGS_OFF, GUI_SLOT_ARGS_SIZE, GUI_SLOT_HDR_OFF, GUI_SLOT_REQ_OFF, GUI_SLOT_RESP_OFF,
    GUI_SLOT_RING_OFF, GUI_SLOT_SIZE, OS32_ERR_FULL, OS32_ERR_INVAL, OS32_ERR_NOSYS,
    OS32_ERR_STALE, OS32_ERR_VERSION,
};
use os32api::gui::types::{Rect, Stats};

/* ================================================================ */
/*  SHM の GUI 予約領域: proto::GUI_SHM_OFFSET (正典は memmap.h /       */
/*  os32_gui_shared.h)。`shm_base` は KAPI のデータフィールド。         */
/* ================================================================ */
pub use os32api::gui::proto::GUI_SHM_OFFSET;

/* 取り込み tick の記録 (契約 P2)。引数バッファの直後 256B = 64 × 4B。
 * 索引は `serial % 64`、各項目は u16 serial + u16 tick 下位。W1 の
 * `gshell::slot::GUI_SLOT_TRACE_OFF` と同値 (共有ヘッダには未記載。申し送り)。 */
/// 取り込み tick 表のスロット内オフセット (契約 P2)。
pub const SLOT_TRACE_OFF: usize = GUI_SLOT_ARGS_OFF + GUI_SLOT_ARGS_SIZE; /* 11280 */
/// 取り込み tick 表の項目数。
pub const TRACE_ENTRIES: usize = 64;

const _: () = assert!(SLOT_TRACE_OFF + TRACE_ENTRIES * 4 <= GUI_SLOT_SIZE);

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

/* ================================================================ */
/*  デバッグ出力 (KAPI に dbg_print は無いので kprintf を包む)        */
/* ================================================================ */

/// NUL 終端の作業バッファへ写す (カーネルの kprintf は `%.*s` を解釈しない —
/// 2026-09-06 実測: ".*s" と、桁の合わないポインタ値が出て診断が読めなかった)。
fn nul_copy(msg: &[u8], buf: &mut [u8; 96]) {
    let n = if msg.len() > 95 { 95 } else { msg.len() };
    buf[..n].copy_from_slice(&msg[..n]);
    buf[n] = 0;
}

/// 1 行のデバッグ出力 (NUL 終端不要)。
pub fn dbg_print(msg: &[u8]) {
    let mut buf = [0u8; 96];
    nul_copy(msg, &mut buf);
    unsafe {
        let a = os32api::api();
        (a.kprintf)(os32api::ATTR_YELLOW, b"%s\r\n\0".as_ptr(), buf.as_ptr());
    }
}

/// 「文字列 + 整数」のデバッグ出力。
pub fn dbg_print_num(msg: &[u8], v: i32) {
    let mut buf = [0u8; 96];
    nul_copy(msg, &mut buf);
    unsafe {
        let a = os32api::api();
        (a.kprintf)(
            os32api::ATTR_YELLOW,
            b"%s %d\r\n\0".as_ptr(),
            buf.as_ptr(),
            v,
        );
    }
}

/* ================================================================ */
/*  クライアント状態 (スロット 1 本)                                  */
/* ================================================================ */

struct Client {
    inited: bool,
    slot: u32,
    base: *mut u8,
    /// ハンドラ実行中フラグ (`wait` の再入禁止を `debug_assert` する。契約 U3)。
    in_handler: bool,
}

impl Client {
    const NEW: Client = Client {
        inited: false,
        slot: 0,
        base: ptr::null_mut(),
        in_handler: false,
    };
}

struct Cell(UnsafeCell<Client>);
unsafe impl Sync for Cell {}
static CLIENT: Cell = Cell(UnsafeCell::new(Client::NEW));

#[inline]
fn c() -> &'static mut Client {
    unsafe { &mut *CLIENT.0.get() }
}

/// `OP_INIT` 済みか。
#[inline]
pub fn is_inited() -> bool {
    c().inited
}

/// 割り当てられたスロット番号 (v1 は常に 0)。
#[inline]
pub fn slot() -> u32 {
    c().slot
}

/// スロット先頭 (デバッグ / `gui_bench` の予備領域読み出し用)。
#[inline]
pub fn slot_base() -> *mut u8 {
    c().base
}

/* ================================================================ */
/*  libos32gfx のアタッチ                                            */
/*                                                                  */
/*  gshell が既に GFX モードにしているので、アプリは `libos32gfx_init` */
/*  を呼んではいけない (中の `gfx_init()` が VRAM 両ページを消し、     */
/*  パレットを初期化してデスクトップを壊す)。`libos32gfx_attach` が    */
/*  framebuffer 記述子の取り直し・画素形式 (gfx_packed) の判定・        */
/*  サーフェス / スプライトのプール初期化をまとめて行う。判定を C 側に */
/*  置くのは、ここで個別にやると PACKED8 判定を忘れて漢字が描けなく    */
/*  なるため (2026-09-06 に実測)。                                     */
/* ================================================================ */
extern "C" {
    fn libos32gfx_attach(api: *mut os32api::KernelAPI);
}

/// framebuffer 記述子・画素形式・サーフェス/スプライトのプールを取り直す。
/// `init()` と共有ライブラリの `shlib_init` (票 C3) の両方から呼ぶ。冪等。
pub(crate) fn attach_gfx() {
    unsafe {
        libos32gfx_attach(os32api::api_ptr());
    }
    crate::gstate::refresh_screen_info();
}

/* ================================================================ */
/*  初期化 (契約 T2a / T5)                                            */
/* ================================================================ */

/// WM に接続してスロットを 1 本もらう (契約 T2a)。返り値はスロット番号。
///
/// `arg` に `proto_version` を載せる (W1 の `op_init`)。WM が古ければ
/// `ERR_VERSION`、スロットが埋まっていれば `ERR_FULL`、WM 未登録なら `ERR_NOSYS`。
pub fn init() -> GuiResult<u32> {
    let r = raw(GUI_OP_INIT, GUI_PROTO_VERSION as u32);
    if r < 0 {
        return Err(GuiErr(r));
    }
    let slot_no = r as u32;
    let shm_base = unsafe { (*os32api::api_ptr()).shm_base };
    let base = (shm_base + GUI_SHM_OFFSET + slot_no * (GUI_SLOT_SIZE as u32)) as *mut u8;

    let st = c();
    st.slot = slot_no;
    st.base = base;
    st.inited = true;

    /* WM が入れたヘッダの proto_version を照合する (契約 T5)。 */
    let h = read_header();
    if h.proto_version != GUI_PROTO_VERSION {
        st.inited = false;
        return Err(GuiErr::VERSION);
    }

    attach_gfx();
    Ok(slot_no)
}

/* ================================================================ */
/*  低レベル呼び出し                                                 */
/* ================================================================ */

#[inline]
fn raw(op: u32, arg: u32) -> i32 {
    unsafe { (os32api::api().gui_call)(op, arg) }
}

/// `gui_call` を 1 回。負の戻り値は [`GuiErr`] に写す (契約 T7)。
#[inline]
pub fn call(op: u32, arg: u32) -> GuiResult<i32> {
    let r = raw(op, arg);
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(r)
    }
}

/* ---- スロット内の各ブロック ---- */

#[inline]
fn header_ptr() -> *mut GuiSlotHeader {
    unsafe { c().base.add(GUI_SLOT_HDR_OFF) as *mut GuiSlotHeader }
}

#[inline]
fn req_ptr() -> *mut u8 {
    unsafe { c().base.add(GUI_SLOT_REQ_OFF) }
}

#[inline]
fn resp_ptr() -> *const u8 {
    unsafe { c().base.add(GUI_SLOT_RESP_OFF) as *const u8 }
}

#[inline]
fn ring_ptr() -> *const u8 {
    unsafe { c().base.add(GUI_SLOT_RING_OFF) as *const u8 }
}

/// 引数バッファ (8KB)。一括登録に使う (契約 T2)。
#[inline]
pub fn args_ptr() -> *mut u8 {
    unsafe { c().base.add(GUI_SLOT_ARGS_OFF) }
}

/// スロットヘッダを読む。
#[inline]
pub fn read_header() -> GuiSlotHeader {
    unsafe { ptr::read_unaligned(header_ptr()) }
}

/// `ring_head` だけを進める (**アプリが書けるのはここだけ** — 契約 T3)。
#[inline]
fn write_ring_head(head: u16) {
    unsafe {
        let p = header_ptr() as *mut u8;
        ptr::write_unaligned(p.add(8) as *mut u16, head);
    }
}

/// 要求ブロックへ構造体を 1 つ置く (先頭から)。
#[inline]
pub fn write_req<T: Copy>(v: &T) {
    debug_assert!(core::mem::size_of::<T>() <= os32api::gui::proto::GUI_SLOT_REQ_SIZE);
    unsafe { ptr::write_unaligned(req_ptr() as *mut T, *v) }
}

/// 応答ブロックから構造体を 1 つ読む (先頭から)。
#[inline]
pub fn read_resp<T: Copy>() -> T {
    debug_assert!(core::mem::size_of::<T>() <= os32api::gui::proto::GUI_SLOT_RESP_SIZE);
    unsafe { ptr::read_unaligned(resp_ptr() as *const T) }
}

/* ================================================================ */
/*  イベント取り出し (契約 T3) — 票 C2 作業 2                         */
/* ================================================================ */

/// `OP_POLL` 1 回の結果。
pub struct Poll {
    /// 取り出した件数 (`out` の先頭から)。
    pub count: usize,
    /// WM が取りこぼした打鍵数 (受け取った時点で WM が消す)。
    pub dropped: u16,
    /// `OVERFLOW`: 入力状態を未知として扱う (押しっぱなしの前提を捨てる)。
    pub overflow: bool,
}

/// リングに溜まったイベントを一括で取り出す (契約 T3 の 3 手順)。
///
/// (1) `OP_POLL` (WM が導出型を追記し、未読件数を返す) → (2) `[head, tail)` を
/// `out` へ写す → (3) **`head = tail` に進める** (`tail` は WM のもの。触らない)。
pub fn poll(out: &mut [GuiEvent]) -> GuiResult<Poll> {
    let n = call(GUI_OP_POLL, 0)? as usize;
    let h = read_header();
    let cap = if out.len() < GUI_RING_CAPACITY { out.len() } else { GUI_RING_CAPACITY };
    let n = if n > cap { cap } else { n };

    let base = ring_ptr();
    let mut i = 0;
    while i < n {
        let idx = (h.ring_head.wrapping_add(i as u16)) % (GUI_RING_CAPACITY as u16);
        out[i] = unsafe { ptr::read_unaligned(base.add((idx as usize) * 16) as *const GuiEvent) };
        i += 1;
    }
    /* 消費確認は head を進めることだけ (WM は head を読んで空きを判断する)。 */
    write_ring_head(h.ring_head.wrapping_add(n as u16));

    Ok(Poll {
        count: n,
        dropped: h.dropped,
        overflow: (h.flags & GUI_HDR_FLAG_OVERFLOW) != 0,
    })
}

/// `OP_WAIT` (GetMessage の代替)。`timeout_ticks` = 0 は「起こされるまで」。
///
/// **ハンドラの中から呼んではいけない** (契約 U3。`debug_assert` で守る)。
pub fn wait(timeout_ticks: u32) -> GuiResult<i32> {
    debug_assert!(!c().in_handler, "gui wait() called from inside an event handler (U3)");
    call(GUI_OP_WAIT, timeout_ticks)
}

/// ハンドラ実行中の印 (再入検出用)。`app::run` が包む。
#[inline]
pub fn enter_handler() {
    c().in_handler = true;
}

#[inline]
pub fn leave_handler() {
    c().in_handler = false;
}

/// `OP_COMMIT`: issued の矩形だけを画面へ (契約 G4)。**ループ 1 周に 1 回**。
/// `window = 0` はこのアプリの全ウィンドウ。
pub fn commit(window: u32) -> GuiResult<()> {
    call(GUI_OP_COMMIT, window).map(|_| ())
}

/// 損傷を申告する (契約 G4)。`rect` はクライアントローカル。
pub fn invalidate(window: u32, rect: Rect) -> GuiResult<()> {
    let req = GuiReqInvalidate { window, rect: r16(rect) };
    write_req(&req);
    call(GUI_OP_INVALIDATE, 0).map(|_| ())
}

/// カウンタ (契約 G7)。応答ブロック先頭に `GFX_Stats` 生で返る。
pub fn stats() -> GuiResult<Stats> {
    call(GUI_OP_STATS, 0)?;
    Ok(read_resp::<Stats>())
}

/// 取り込み tick (契約 P2)。`serial` に対応する記録が残っていれば tick の下位 16bit。
pub fn trace_tick(serial: u16) -> Option<u16> {
    if !is_inited() {
        return None;
    }
    let idx = (serial as usize) % TRACE_ENTRIES;
    unsafe {
        let p = c().base.add(SLOT_TRACE_OFF + idx * 4) as *const u16;
        let s = ptr::read_unaligned(p);
        if s != serial {
            return None;
        }
        Some(ptr::read_unaligned(p.add(1)))
    }
}

/* ================================================================ */
/*  ウィンドウ op (契約 U1) — 所有型 `window::Window` の下請け        */
/* ================================================================ */

#[inline]
fn r16(r: Rect) -> GuiRect16 {
    GuiRect16 { x: r.x, y: r.y, w: r.w, h: r.h }
}

#[inline]
fn r16_to_rect(r: GuiRect16) -> Rect {
    Rect::new(r.x, r.y, r.w, r.h)
}

/// `create_window(spec)`。戻り値は WindowId (index:16 | generation:16)。
pub fn win_create(spec: &GuiWinSpec) -> GuiResult<u32> {
    write_req(spec);
    let r = call(GUI_OP_WIN_CREATE, 0)?;
    Ok(r as u32)
}

/// 単一ハンドル op (DESTROY / RAISE / SET_FOCUS)。W1 は `arg != 0` を優先する。
fn win_simple(op: u32, window: u32) -> GuiResult<()> {
    let req = GuiReqWindow { window };
    write_req(&req);
    call(op, window).map(|_| ())
}

pub fn win_destroy(window: u32) -> GuiResult<()> {
    win_simple(GUI_OP_WIN_DESTROY, window)
}

pub fn win_raise(window: u32) -> GuiResult<()> {
    win_simple(GUI_OP_WIN_RAISE, window)
}

pub fn win_set_focus(window: u32) -> GuiResult<()> {
    win_simple(GUI_OP_WIN_SET_FOCUS, window)
}

/// クライアント矩形 (**原点は画面絶対座標**、大きさはクライアント面。W1 の規約)。
pub fn win_client_rect(window: u32) -> GuiResult<Rect> {
    let req = GuiReqWindow { window };
    write_req(&req);
    call(GUI_OP_WIN_CLIENT_RECT, window)?;
    let resp = read_resp::<GuiRespRect>();
    if resp.result < 0 {
        return Err(GuiErr(resp.result));
    }
    Ok(r16_to_rect(resp.rect))
}

pub fn win_move(window: u32, x: i16, y: i16) -> GuiResult<()> {
    let req = GuiReqWinMove { window, x, y };
    write_req(&req);
    call(GUI_OP_WIN_MOVE, 0).map(|_| ())
}

pub fn win_resize(window: u32, w: i16, h: i16) -> GuiResult<()> {
    let req = GuiReqWinResize { window, w, h };
    write_req(&req);
    call(GUI_OP_WIN_RESIZE, 0).map(|_| ())
}

pub fn win_show(window: u32, show: bool) -> GuiResult<()> {
    let req = GuiReqWinShow { window, show: show as u8, _pad: [0; 3] };
    write_req(&req);
    call(GUI_OP_WIN_SHOW, 0).map(|_| ())
}

/// `&[u8]` を長さ前置文字列へ (UTF-8 境界で 255B に切り詰め。契約 U9)。
pub fn gui_string(src: &[u8]) -> GuiString {
    let mut s = GuiString { len: 0, s: [0u8; 255] };
    let n = utf8_truncate(src, 255);
    let mut i = 0;
    while i < n {
        s.s[i] = src[i];
        i += 1;
    }
    s.len = n as u8;
    s
}

/// タイトルを差し替える。UTF-8 境界で 255B に切り詰める (契約 U9)。
pub fn win_set_title(window: u32, title: &[u8]) -> GuiResult<()> {
    let s = gui_string(title);
    let req = GuiReqWinTitle { window, title: s };
    write_req(&req);
    call(GUI_OP_WIN_SET_TITLE, 0).map(|_| ())
}

/// FEP の候補窓の位置 (契約 U2a の cursor rectangle)。座標はクライアントローカル。
pub fn win_set_text_cursor(window: u32, x: i16, y: i16, visible: bool) -> GuiResult<()> {
    let req = GuiReqTextCursor { window, x, y, visible: visible as u8, _pad: 0 };
    write_req(&req);
    call(GUI_OP_WIN_SET_TEXT_CURSOR, 0).map(|_| ())
}

/* ================================================================ */
/*  パレットのリース (契約 G8) — W1 では NOSYS (W2 待ち)              */
/*                                                                  */
/*  **PM への申し送り**: `lease_palette(first, count, entries)` の要求 */
/*  ブロックのレイアウトが共有ヘッダ (os32_gui_shared.h) に無い。ここ  */
/*  では 16 色バックエンド向けに次を仮に決めてある。W2 が実装するとき   */
/*  ヘッダへ末尾追記して両側を合わせること。                           */
/*      u16 first / u16 count / GuiRgb entries[16]                   */
/*  256 色 (最大 240 項目) は引数バッファ (8KB) 経由になる想定。        */
/* ================================================================ */
/*  パレットリース (契約 G8)。要求は proto::GuiReqLease (共有ヘッダに   */
/*  追記済み: first / count / rgb[16]、count=0 で返却)。16 色版のみ。   */
/* ================================================================ */
/// フォーカス中のウィンドウが自分の色を入れる (契約 G8)。16 色版のみ。
/// 範囲は `screen_info()` の `lease_mask` で問い合わせること。
pub fn lease_palette(first: u16, entries: &[GuiRgb]) -> GuiResult<i32> {
    if entries.is_empty() || entries.len() > 16 {
        return Err(GuiErr::INVAL);
    }
    let mut req = GuiReqLease { first, count: entries.len() as u16, rgb: [GuiRgb { r: 0, g: 0, b: 0 }; 16] };
    let mut i = 0;
    while i < entries.len() {
        req.rgb[i] = entries[i];
        i += 1;
    }
    write_req(&req);
    call(GUI_OP_LEASE_PALETTE, 0)
}

/* ================================================================ */
/*  タイマ (契約 U5)                                                  */
/* ================================================================ */

/// タイマを張る (契約 U5: id u8、間隔 tick (10ms)、repeat=false は単発で WM が消す)。
pub fn timer_set(window: u32, timer_id: u8, interval_ticks: u16, repeat: bool) -> GuiResult<()> {
    let req = GuiReqTimerSet { window, timer_id, repeat: if repeat { 1 } else { 0 }, interval_ticks };
    write_req(&req);
    call(GUI_OP_TIMER_SET, 0).map(|_| ())
}

pub fn timer_kill(window: u32, timer_id: u8) -> GuiResult<()> {
    let req = GuiReqTimerKill { window, timer_id, _pad: [0; 3] };
    write_req(&req);
    call(GUI_OP_TIMER_KILL, 0).map(|_| ())
}

/* ================================================================ */
/*  文字列 (契約 U9) — 切り詰めは UTF-8 の境界で                      */
/*                                                                  */
/*  実体は `crate::utf8core` へ移した (票 N4 §5: clip_get の境界を    */
/*  host_tests から踏むため os32api 非依存の小モジュールへ出した)。    */
/*  `client::utf8_seq_len` / `client::utf8_truncate` の名前は据置き。  */
/* ================================================================ */
pub use crate::utf8core::{utf8_seq_len, utf8_truncate};
