//! taskbar.rs — タスクバー (契約 V12-D の D1 / D3、票 W3 §2・§3)。
//!
//! 画面下端 24px に固定される **WM 自身の UI**。アプリの Window 表 (16) /
//! Surface / SHM スロットを 1 本も消費しない (契約 P: taskbar は app の上限を
//! 食わない)。中身は 3 つ:
//!
//! ```text
//!   左   Start ボタン           → startmenu::toggle_start
//!   中   現アプリの可視 / 最小化した top-level 窓のボタン (raise + focus。プロセス切替ではない)
//!   右   [M] (マウスキー中だけ) と時計 HH:MM (sys_time)
//! ```
//!
//! 窓ボタンの押し込み表示は、ふだんは最前面の窓、GRPH+TAB の切り替え中は
//! **選んでいる窓** (Win98 の Alt+TAB の一覧の代わり、票 KBD_NAV §1-2)。
//!
//! 作業領域 (`wm::work_area`) は「画面 − タスクバー」。新規配置とドラッグ確定は
//! そこへクランプし、既に外へ出ている窓はそのままにする (契約 D1)。
//!
//! 描画は [`draw`] が `wm::composite_rect` の最後 (クローム / モーダルの後) で
//! 行う。可視領域の計算 ([`crate::visible`]) がタスクバーの矩形を穴として引くので、
//! アプリの `Paint` / COMMIT はここへ届かない = 上書きされない。
//!
//! 時計は 1 秒より細かく更新しない。**文字列が変わったときだけ時計矩形を
//! 損傷にする** (契約 D3: 時計のために全画面 present しない)。
//!
//! 表記は設定レジストリの `gshell` / `taskbar/clock_24h` (票 S4 §1) で決まる。
//! 12 時間表記 (`h:MM AM` / `12:59 PM`) は**文字数が変わる**ので、
//! [`clock_rect`] の幅は固定値ではなく**いま出ている文字数**から出し、
//! 幅が変わる更新は **旧矩形 ∪ 新矩形**を損傷にする (票 §1 の B4: 新矩形だけ
//! だと縮んだときに旧左端の 8px が残る)。窓ボタン帯は時計の左端で切れるので、
//! 幅が変わった周は帯も描き直す。

use crate::wm::{GuiState, Rect};
use crate::{chrome, lease, startmenu};
use os32api::gfx;
use os32api::gui::proto::{
    GUI_COLOR_FACE, GUI_COLOR_LIGHT, GUI_COLOR_SHADOW, GUI_COLOR_TEXT, GUI_COLOR_TITLE_ACTIVE,
    GUI_COLOR_TITLE_TEXT, GUI_COLOR_WINDOW, GUI_MAX_WINDOWS,
};

/* ================================================================ */
/*  寸法 (契約 D1: 高さ 24px 固定)                                    */
/* ================================================================ */
pub const TASKBAR_H: i32 = 24;
/// バーの内側の余白 (ボタンの上下 2px)。
const PAD: i32 = 2;
/// ボタンの高さ (24 − 上下 2px)。
const BTN_H: i32 = 20;
/// Start ボタンの幅 ("Start" = 5 文字 × 8px + 余白)。
const START_W: i32 = 56;
/// 時計の 1 文字の幅 (ANK 8px)。
const CLOCK_CHAR_W: i32 = 8;
/// 時計の矩形の左右の余白 (文字数 × 8 に足す)。
const CLOCK_PAD: i32 = 8;
/// 時計の文字列の最大長 ("12:59 PM" = 8) と NUL。
const CLOCK_MAX: usize = 8;
/// 窓ボタン 1 個の幅と間隔。
const WBTN_W: i32 = 96;
const WBTN_GAP: i32 = 4;
/// 窓ボタンに出せる文字数 (8px/文字。左右 4px の余白を除く)。
const WBTN_CHARS: usize = 11;
/// マウスキーの表示 "[M]" の幅 (3 文字 + 余白)。
const MK_W: i32 = 3 * 8 + 8;

/// 時計を作り直す間隔 (tick = 10ms。契約 D3「1 秒より細かい更新は不要」)。
const CLOCK_INTERVAL: u32 = 100;

/* ================================================================ */
/*  矩形                                                             */
/* ================================================================ */

/// タスクバー全体 (画面座標)。
#[inline]
pub fn rect(st: &GuiState) -> Rect {
    Rect::new(0, st.screen_h - TASKBAR_H, st.screen_w, TASKBAR_H)
}

/// Start ボタン。
#[inline]
pub fn start_rect(st: &GuiState) -> Rect {
    Rect::new(PAD, st.screen_h - TASKBAR_H + PAD, START_W, BTN_H)
}

/// 時計 (右詰め)。**幅はいま出ている文字数から** (票 S4 §1)。
#[inline]
pub fn clock_rect(st: &GuiState) -> Rect {
    clock_rect_for(st, bar().clock_len)
}

/// 文字数 `n` のときの時計の矩形 (旧 ∪ 新 を取るために外から長さを渡せる)。
#[inline]
pub fn clock_rect_for(st: &GuiState, n: usize) -> Rect {
    let w = (n as i32) * CLOCK_CHAR_W + CLOCK_PAD;
    Rect::new(st.screen_w - PAD - w, st.screen_h - TASKBAR_H + PAD, w, BTN_H)
}

/// いま出ている時計の文字数 (試験の観測点)。
#[allow(dead_code)]
#[inline]
pub fn clock_len() -> usize {
    bar().clock_len
}

/// ホスト TDD の初期化 (`mocks::init`)。時計の文字数は `static` なので、
/// 前の試験の 12h 表記が次の試験の矩形計算に残らないようにする。
#[allow(dead_code)]
pub fn reset() {
    *bar() = Bar::NEW;
}

/// 表記が変わったので次の [`x3_cycle`] で作り直す (票 S4 §3 の適用)。
/// 幅が変わるので**いまの矩形**も先に損傷にしておく。
pub fn invalidate_clock(st: &mut GuiState) {
    let old = clock_rect(st);
    st.dirty_screen(old);
    let band = wbtn_band(st);
    st.dirty_screen(band);
    let b = bar();
    /* 時刻が同じでも必ず作り直させる (表記が変わったので文字列が変わる)。 */
    b.inited = false;
    b.force = true;
}

/// マウスキーの表示 "[M]" (時計の左)。マウスキーがオフなら空矩形。
pub fn mk_rect(st: &GuiState) -> Rect {
    if !st.kn.mk_on {
        return Rect::EMPTY;
    }
    let c = clock_rect(st);
    Rect::new(c.x - WBTN_GAP - MK_W, c.y, MK_W, BTN_H)
}

/// 窓ボタンが使える右端 (時計、マウスキー中は [M] の左)。
fn right_limit(st: &GuiState) -> i32 {
    let m = mk_rect(st);
    if m.is_empty() {
        clock_rect(st).x
    } else {
        m.x
    }
}

/// i 番目の窓ボタン。時計に掛かる位置なら空矩形。
fn wbtn_rect(st: &GuiState, i: usize) -> Rect {
    let x = PAD + START_W + WBTN_GAP + (i as i32) * (WBTN_W + WBTN_GAP);
    if x + WBTN_W > right_limit(st) - WBTN_GAP {
        return Rect::EMPTY;
    }
    Rect::new(x, st.screen_h - TASKBAR_H + PAD, WBTN_W, BTN_H)
}

/// 窓ボタンの帯 (損傷を絞る用)。
fn wbtn_band(st: &GuiState) -> Rect {
    let x = PAD + START_W + WBTN_GAP;
    Rect::new(x, st.screen_h - TASKBAR_H, right_limit(st) - x, TASKBAR_H)
}

/// タスクバーの上か (契約 D1: この領域の入力はアプリへ配送しない)。
#[inline]
pub fn hit(st: &GuiState, mx: i32, my: i32) -> bool {
    rect(st).contains(mx, my)
}

/* ================================================================ */
/*  状態 (時計と窓ボタンの前回値。.bss)                               */
/* ================================================================ */
struct Bar {
    /// 直近に作った文字列 (NUL 終端。"HH:MM" / "12:59 PM")。
    clock: [u8; CLOCK_MAX + 1],
    /// その長さ (NUL を除く)。矩形の幅はここから出す。
    clock_len: usize,
    /// 表記の切替で「同じ時刻でも作り直す」印 ([`invalidate_clock`])。
    force: bool,
    /// 次に時計を作り直す tick。
    next_clock: u32,
    /// 前回描いた窓ボタンの署名 (件数と最前面 id)。変化で帯を損傷にする。
    sig_n: usize,
    sig_front: u32,
    sig_ids: [u32; GUI_MAX_WINDOWS],
    /// 押し込み表示の窓 (切り替え中は選択、ふだんは最前面)。
    sig_pressed: u32,
    inited: bool,
}

impl Bar {
    const NEW: Bar = Bar {
        clock: [b'-', b'-', b':', b'-', b'-', 0, 0, 0, 0],
        clock_len: 5,
        force: false,
        next_clock: 0,
        sig_n: 0,
        sig_front: 0,
        sig_ids: [0; GUI_MAX_WINDOWS],
        sig_pressed: 0,
        inited: false,
    };
}

struct BarCell(core::cell::UnsafeCell<Bar>);
unsafe impl Sync for BarCell {}
static BAR: BarCell = BarCell(core::cell::UnsafeCell::new(Bar::NEW));

#[inline]
fn bar() -> &'static mut Bar {
    unsafe { &mut *BAR.0.get() }
}

/* ================================================================ */
/*  X3 の周期 (時計 + 窓ボタンの変化検出)                             */
/* ================================================================ */

/// X3 (`wm::wm_cycle` の Wait / Standalone) で 1 回だけ呼ぶ。
///
/// - 時計: 1 秒に 1 回だけ `sys_time()` を読み、**文字列が変わったときだけ**
///   時計矩形を損傷にする (契約 D3)。
/// - 窓ボタン: 可視 top-level 窓の並びか最前面が変わったら帯を損傷にする。
pub fn x3_cycle(st: &mut GuiState) {
    tick_clock(st);
    tick_buttons(st);
}

fn tick_clock(st: &mut GuiState) {
    {
        let b = bar();
        if b.inited && st.now.wrapping_sub(b.next_clock) >= 0x8000_0000 {
            return; /* now < next_clock */
        }
        b.next_clock = st.now.wrapping_add(CLOCK_INTERVAL);
        b.inited = true;
    }
    let t = unsafe { (os32api::api().sys_time)() };
    let mut s = [0u8; CLOCK_MAX + 1];
    let n = format_clock(t, st.cfg.clock_24h, &mut s);
    let (old_len, same) = {
        let b = bar();
        let same = !b.force && b.clock_len == n && b.clock[..n] == s[..n];
        b.force = false;
        (b.clock_len, same)
    };
    if same {
        return;
    }
    /* **旧矩形を先に**取る (幅が縮むとき、新矩形だけでは旧左端が残る)。 */
    let old = clock_rect_for(st, old_len);
    {
        let b = bar();
        b.clock = s;
        b.clock_len = n;
    }
    let new = clock_rect_for(st, n);
    st.dirty_screen(old);
    st.dirty_screen(new);
    if old_len != n {
        /* 窓ボタン帯は時計の左端で切れる。幅が変わったら並べ直す。 */
        let band = wbtn_band(st);
        st.dirty_screen(band);
    }
}

/// epoch 秒 → 表示文字列 (NUL 終端)。戻り値は長さ (NUL を除く)。
/// RTC は現地時刻なので日内秒をそのまま使う。
///
/// - 24h … `HH:MM` (5 文字、0 詰め)
/// - 12h … `h:MM AM` / `12:59 PM` (7〜8 文字、**時は空白詰めしない**。票 §1)
pub fn format_clock(epoch: u32, h24: bool, out: &mut [u8; CLOCK_MAX + 1]) -> usize {
    let sod = epoch % 86400;
    let hh = sod / 3600;
    let mm = (sod % 3600) / 60;
    let mut n = 0usize;
    if h24 {
        out[n] = b'0' + (hh / 10) as u8;
        n += 1;
        out[n] = b'0' + (hh % 10) as u8;
        n += 1;
    } else {
        /* 0 時 = 12 AM、12 時 = 12 PM。 */
        let h12 = if hh % 12 == 0 { 12 } else { hh % 12 };
        if h12 >= 10 {
            out[n] = b'0' + (h12 / 10) as u8;
            n += 1;
        }
        out[n] = b'0' + (h12 % 10) as u8;
        n += 1;
    }
    out[n] = b':';
    n += 1;
    out[n] = b'0' + (mm / 10) as u8;
    n += 1;
    out[n] = b'0' + (mm % 10) as u8;
    n += 1;
    if !h24 {
        out[n] = b' ';
        n += 1;
        out[n] = if hh < 12 { b'A' } else { b'P' };
        n += 1;
        out[n] = b'M';
        n += 1;
    }
    out[n] = 0;
    n
}

fn tick_buttons(st: &mut GuiState) {
    let mut ids = [0u32; GUI_MAX_WINDOWS];
    let n = list_windows(st, &mut ids);
    let front = st.front_id();
    let pressed = pressed_id(st);
    let b = bar();
    if b.sig_n == n
        && b.sig_front == front
        && b.sig_pressed == pressed
        && b.sig_ids[..n] == ids[..n]
    {
        return;
    }
    b.sig_n = n;
    b.sig_front = front;
    b.sig_pressed = pressed;
    b.sig_ids = ids;
    let band = wbtn_band(st);
    st.dirty_screen(band);
}

/// 押し込みで描く窓ボタン: GRPH+TAB の切り替え中は選択、ふだんは最前面。
pub fn pressed_id(st: &GuiState) -> u32 {
    match crate::kbdnav::switch_selection(st) {
        Some(id) => id,
        None => st.front_id(),
    }
}

/// タスクバーに並べる窓 (可視か最小化の top-level)。index 順で安定させる。
/// 戻り値は件数、`out` には完全な WindowId。
fn list_windows(st: &GuiState, out: &mut [u32; GUI_MAX_WINDOWS]) -> usize {
    let mut n = 0;
    let mut i = 0;
    while i < GUI_MAX_WINDOWS {
        if st.windows[i].used && (st.windows[i].visible || st.windows[i].minimized) {
            out[n] = st.windows[i].id(i);
            n += 1;
        }
        i += 1;
    }
    n
}

/* ================================================================ */
/*  マウス (X3 だけ。契約 T8)                                        */
/* ================================================================ */

/// タスクバー上の押下 1 件。**必ず WM が消費する** (契約 D1)。
pub fn on_button(st: &mut GuiState, mx: i32, my: i32) {
    if start_rect(st).contains(mx, my) {
        startmenu::toggle_start(st);
        return;
    }
    let mut ids = [0u32; GUI_MAX_WINDOWS];
    let n = list_windows(st, &mut ids);
    let mut i = 0;
    while i < n {
        let br = wbtn_rect(st, i);
        if !br.is_empty() && br.contains(mx, my) {
            activate(st, ids[i]);
            return;
        }
        i += 1;
    }
}

/// 窓ボタン = **raise + focus** (プロセス切替ではない。契約 S1)。
/// 最小化した窓は元に戻す。未確定文字は元の窓へ確定してから移す
/// (`wm::activate_index`、票 KBD_NAV §1-6)。
fn activate(st: &mut GuiState, id: u32) {
    let index = match st.win_by_id(id) {
        Some(i) => i,
        None => return,
    };
    if st.front_index() == Some(index) {
        return;
    }
    crate::wm::activate_index(st, index);
    let band = wbtn_band(st);
    st.dirty_screen(band);
}

/* ================================================================ */
/*  描画 (X3。wm::composite_rect の最後)                              */
/* ================================================================ */

/// `clip` に掛かるならタスクバー全体を描き直す (kcg にクリップが無いため)。
/// present するのは損傷矩形だけなので、時計だけの更新でも転送量は増えない。
pub fn draw(st: &GuiState, clip: Rect) {
    let r = rect(st);
    if !r.intersects(&clip) {
        return;
    }
    let mono = lease::mono(st);
    let face = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_FACE };
    unsafe {
        gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, face);
        /* 上端に 1px のハイライト (バーの立ち上がり)。 */
        if mono {
            gfx::gfx_hline(r.x, r.y, r.w, GUI_COLOR_TEXT);
        } else {
            gfx::gfx_hline(r.x, r.y, r.w, GUI_COLOR_LIGHT);
        }
        gfx::kcg_set_scale(1);
    }
    /* Start */
    let sr = start_rect(st);
    draw_button(sr, mono, startmenu::start_pressed(), b"Start\0", true);
    /* 窓ボタン */
    let mut ids = [0u32; GUI_MAX_WINDOWS];
    let n = list_windows(st, &mut ids);
    let pressed = pressed_id(st);
    let mut i = 0;
    while i < n {
        let br = wbtn_rect(st, i);
        if br.is_empty() {
            break;
        }
        let idx = match st.win_by_id(ids[i]) {
            Some(k) => k,
            None => break,
        };
        draw_wbtn(st, br, idx, mono, ids[i] == pressed);
        i += 1;
    }
    /* マウスキー中の表示 (票 KBD_NAV §1-3)。 */
    let mr = mk_rect(st);
    if !mr.is_empty() {
        draw_button(mr, mono, true, b"[M]\0", true);
    }
    /* 時計 */
    let cr = clock_rect(st);
    draw_clock(cr, mono, face);
}

/// タスクバーのボタン 1 個 (立体枠 + 中央または左寄せのラベル)。
fn draw_button(br: Rect, mono: bool, pressed: bool, label: &[u8], center: bool) {
    let face = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_FACE };
    unsafe {
        gfx::gfx_fill_rect(br.x, br.y, br.w, br.h, face);
        gfx::gfx_rect(br.x, br.y, br.w, br.h, GUI_COLOR_TEXT);
        if mono {
            if pressed {
                chrome::dither50(br.x + 1, br.y + 1, br.w - 2, 1, GUI_COLOR_TEXT);
            }
        } else if pressed {
            /* 沈んだ枠 (上/左が影)。 */
            gfx::gfx_hline(br.x + 1, br.y + 1, br.w - 2, GUI_COLOR_SHADOW);
            gfx::gfx_vline(br.x + 1, br.y + 1, br.h - 2, GUI_COLOR_SHADOW);
        } else {
            gfx::gfx_hline(br.x + 1, br.y + 1, br.w - 2, GUI_COLOR_LIGHT);
            gfx::gfx_vline(br.x + 1, br.y + 1, br.h - 2, GUI_COLOR_LIGHT);
            gfx::gfx_hline(br.x + 1, br.y + br.h - 2, br.w - 2, GUI_COLOR_SHADOW);
            gfx::gfx_vline(br.x + br.w - 2, br.y + 1, br.h - 2, GUI_COLOR_SHADOW);
        }
        let off = if pressed { 1 } else { 0 };
        let lx = if center {
            br.x + (br.w - label_width(label)) / 2
        } else {
            br.x + 4
        };
        gfx::kcg_draw_utf8(lx + off, br.y + 2 + off, label.as_ptr(), GUI_COLOR_TEXT, face);
    }
}

/// 窓ボタン: 最前面のものは沈めて描き、タイトルを 11 文字で切る。
fn draw_wbtn(st: &GuiState, br: Rect, index: usize, mono: bool, active: bool) {
    let mut label = [0u8; WBTN_CHARS + 1];
    copy_label(&st.windows[index].title, &mut label);
    draw_button(br, mono, active, &label, false);
}

/// タイトルを窓ボタンの幅へ切り詰める。**UTF-8 の境界でしか切らない**
/// (途中で切ると kcg が壊れた文字を描く)。
fn copy_label(title: &[u8; 40], out: &mut [u8; WBTN_CHARS + 1]) {
    let mut cols = 0usize; /* 表示桁 (ANK=1、全角=2) */
    let mut i = 0usize;
    let mut n = 0usize;
    while i < 40 && title[i] != 0 && n < WBTN_CHARS {
        let b = title[i];
        let len = if b < 0x80 {
            1
        } else if b >= 0xF0 {
            4
        } else if b >= 0xE0 {
            3
        } else if b >= 0xC0 {
            2
        } else {
            1
        };
        let w = if b < 0x80 { 1 } else { 2 };
        if cols + w > WBTN_CHARS || n + len > WBTN_CHARS {
            break;
        }
        let mut k = 0;
        while k < len && i + k < 40 {
            out[n] = title[i + k];
            n += 1;
            k += 1;
        }
        cols += w;
        i += len;
    }
    out[n] = 0;
}

fn draw_clock(cr: Rect, mono: bool, face: u8) {
    let b = bar();
    let (bg, fg) = if mono {
        (GUI_COLOR_WINDOW, GUI_COLOR_TEXT)
    } else {
        (GUI_COLOR_TITLE_ACTIVE, GUI_COLOR_TITLE_TEXT)
    };
    unsafe {
        gfx::gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, bg);
        gfx::gfx_rect(cr.x, cr.y, cr.w, cr.h, GUI_COLOR_TEXT);
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(cr.x + 4, cr.y + 2, b.clock.as_ptr(), fg, bg);
    }
    let _ = face;
}

fn label_width(label: &[u8]) -> i32 {
    let mut n = 0;
    while n < label.len() && label[n] != 0 {
        n += 1;
    }
    (n as i32) * 8
}

/// アプリの COMMIT (X2) がタスクバーに掛かったら描き直す。可視領域から
/// タスクバーは引いてあるので普通は起きないが、保険として持つ (モーダルと同じ)。
pub fn refresh_if_hit(st: &GuiState, r: Rect) -> bool {
    let tb = rect(st);
    if !tb.intersects(&r) {
        return false;
    }
    draw(st, tb);
    true
}
