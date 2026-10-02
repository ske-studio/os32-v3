//! input.rs — 入力の取り込み (契約 T3 / U2a)。
//!
//! `kbd_trygetrawkey` の生 make/break を `Key` (down 0/1) に、印字可能キーは加えて
//! `Text` にして、フォーカス窓の所有者スロットのリングへ積む。`mouse_poll` から
//! `Pointer` (最新 1 件へ畳む) と `Button` を作る。修飾は raw エントリに載る (イベント時点)。
//! `kbd_dropped_count` の差分を `dropped` に足して `OVERFLOW` (契約 T3)。
//!
//! WM 自身の UI (ドラッグ / 閉じる / フォーカス切替) は [`Ctx::Wait`] /
//! [`Ctx::Standalone`] (= `OP_WAIT` の中 / gshell 単独ループ、契約 X3) でだけ
//! 進める。ポンプ [`Ctx::Pump`] (X4) は入力のリング追記とカーソル移動だけで、
//! 状態機械を進めない。
//!
//! **W2 で足したもの** (契約 U2a / U4): SHIFT+SPACE を WM が消費して FEP を
//! 切り替え、押下は先に FEP ([`crate::fep`]) へ通して**消費されたキーは `Key` と
//! して配送しない**。確定文字列は取り込みの最後に `Text` でまとめて流す。
//! モーダル中は宛先をダイアログに限定する。GUI 中は cooked 待ち行列
//! (`kbd_buf`) にカーネルが積まないので (K2)、打鍵の入口は raw 1 本だけ。

use crate::wm::{GuiState, Rect};
use crate::{cursor, fep, kbdnav, modal, ring, slot, startmenu, taskbar, visible, wm};
use os32api::gui::proto::{GuiRect16, GUI_EV_CONFIGURE, GUI_EV_FOCUS};

/// 入力取り込みの実行文脈 (契約 T8)。
///
/// - [`Ctx::Pump`]       … X4。int 0x80 の入口 (K2)。入力のリング追記とカーソル移動だけ。
/// - [`Ctx::Wait`]       … X3。アプリの `OP_WAIT` の中。WM の状態機械を全部進める。
/// - [`Ctx::Standalone`] … X3。gshell 単独ループ。Wait に加えて
///   [`standalone_key`] (デバッグ専用の ESC / F1〜F5) を横取りする。
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Ctx {
    Pump,
    Wait,
    Standalone,
}

impl Ctx {
    /// WM 自身の UI 状態機械 (ドラッグ / 閉じる / フォーカス切替) を進めてよいか。
    #[inline]
    pub fn wm_ui(self) -> bool {
        self != Ctx::Pump
    }
}

/// ABORTED owner exit consumes the same STOP in every WM input representation.
pub fn discard_stop(st: &mut GuiState) {
    st.abort_seen = false;
    let mut kept = 0;
    for i in 0..st.pending_raw_n {
        let raw = st.pending_raw[i];
        if raw & 0x1ff == (SC_STOP as i32 | 0x100) && raw & ((MOD_CTRL as i32) << 9) != 0 {
            continue;
        }
        st.pending_raw[kept] = raw;
        kept += 1;
    }
    st.pending_raw_n = kept;
}

/* 修飾ビット (drivers/kbd.h の SHIFT_* と一致)。 */
const MOD_SHIFT: u32 = 0x01;
const MOD_CAPS: u32 = 0x02;
const MOD_CTRL: u32 = 0x10;

/* 修飾キーのスキャンコード (KEY_SHIFT..KEY_CTRL)。Key として配送しない。 */
const SC_SHIFT: u8 = 0x70;
const SC_CTRL: u8 = 0x74;
/* WM が単独時に横取りするキー (KEY_ESC / KEY_F1〜F5)。**デバッグ専用**
 * (`crate::DEBUG_SHORTCUTS`、既定 false)。F1〜F4 = `crate::LAUNCH_APPS` の起動、
 * F5 = ファイル選択ダイアログ。製品ではどれも横取りしない。 */
const SC_ESC: u8 = 0x00;
const SC_F1: u8 = 0x62;
const SC_F4: u8 = 0x65;
const SC_F5: u8 = 0x66;
/* KEY_SPACE。SHIFT+SPACE で FEP を切り替える (契約 U2a)。 */
const SC_SPACE: u8 = 0x34;
const SC_STOP: u8 = 0x60; /* STOP キー (kbd.h KEY_STOP)。CTRL+STOP = 強制脱出 (契約 T6) */

/* drivers/mouse.h の MOUSE_BTN_*。`GuiEvtButton.button` にもこの値を載せる
 * (契約 D4 の「前面窓のクライアントへ `Button{button=2}`」)。 */
const MOUSE_BTN_LEFT: u8 = 0x01;
const MOUSE_BTN_RIGHT: u8 = 0x02;

/// mouse_poll(*mut u8) が書き込む構造体 (os32_kapi_shared.h MouseInfo と同一)。
#[repr(C)]
#[derive(Clone, Copy)]
struct MouseInfo {
    x: i16,
    y: i16,
    dx: i16,
    dy: i16,
    buttons: u8,
    mode: u8,
}

/* drivers/kbd.c の scancode_to_ascii[128] の写し (8 個 / 行、C 側と同じ並び)。 */
#[rustfmt::skip]
static SC2A: [u8; 128] = [
    /* 0x00 */ 0x1B, b'1',  b'2',  b'3',  b'4',  b'5',  b'6',  b'7',
    /* 0x08 */ b'8',  b'9',  b'0',  b'-',  b'^',  b'\\', 0x08,  0x09,
    /* 0x10 */ b'q',  b'w',  b'e',  b'r',  b't',  b'y',  b'u',  b'i',
    /* 0x18 */ b'o',  b'p',  b'@',  b'[',  0x0D,  b'a',  b's',  b'd',
    /* 0x20 */ b'f',  b'g',  b'h',  b'j',  b'k',  b'l',  b';',  b':',
    /* 0x28 */ b']',  b'z',  b'x',  b'c',  b'v',  b'b',  b'n',  b'm',
    /* 0x30 */ b',',  b'.',  b'/',  0,     b' ',  0,     0x12,  0x03,
    /* 0x38 */ 0x16,  0x7F,  0x1E,  0x1D,  0x1C,  0x1F,  0x01,  0x05,
    /* 0x40 */ b'-',  b'/',  b'7',  b'8',  b'9',  b'*',  b'4',  b'5',
    /* 0x48 */ b'6',  b'+',  b'1',  b'2',  b'3',  b'=',  b'0',  b',',
    /* 0x50 */ b'.',  0, 0, 0, 0, 0, 0, 0,
    /* 0x58 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x68 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x78 */ 0, 0, 0, 0, 0, 0, 0, 0,
];

/* drivers/kbd.c の scancode_to_ascii_shift[128] の写し。 */
#[rustfmt::skip]
static SC2A_SHIFT: [u8; 128] = [
    /* 0x00 */ 0x1B, b'!',  b'"',  b'#',  b'$',  b'%',  b'&',  b'\'',
    /* 0x08 */ b'(',  b')',  0,     b'=',  b'`',  b'|',  0x08,  0x09,
    /* 0x10 */ b'Q',  b'W',  b'E',  b'R',  b'T',  b'Y',  b'U',  b'I',
    /* 0x18 */ b'O',  b'P',  b'~',  b'{',  0x0D,  b'A',  b'S',  b'D',
    /* 0x20 */ b'F',  b'G',  b'H',  b'J',  b'K',  b'L',  b'+',  b'*',
    /* 0x28 */ b'}',  b'Z',  b'X',  b'C',  b'V',  b'B',  b'N',  b'M',
    /* 0x30 */ b'<',  b'>',  b'?',  b'_',  b' ',  0,     0x12,  0x03,
    /* 0x38 */ 0x16,  0x7F,  0x1E,  0x1D,  0x1C,  0x1F,  0x01,  0x05,
    /* 0x40 */ b'-',  b'/',  b'7',  b'8',  b'9',  b'*',  b'4',  b'5',
    /* 0x48 */ b'6',  b'+',  b'1',  b'2',  b'3',  b'=',  b'0',  b',',
    /* 0x50 */ b'.',  0, 0, 0, 0, 0, 0, 0,
    /* 0x58 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x68 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x78 */ 0, 0, 0, 0, 0, 0, 0, 0,
];

/// スキャンコード + 修飾 → ASCII (drivers/kbd.c と同じ規則。無ければ 0)。
fn translate(scan: u8, mods: u32) -> u8 {
    let i = (scan & 0x7F) as usize;
    let mut a = if (mods & MOD_SHIFT) != 0 { SC2A_SHIFT[i] } else { SC2A[i] };
    if (mods & MOD_CAPS) != 0 {
        if a >= b'a' && a <= b'z' {
            a -= 32;
        } else if a >= b'A' && a <= b'Z' {
            a += 32;
        }
    }
    if (mods & MOD_CTRL) != 0 && a >= b'a' && a <= b'z' {
        a = a - b'a' + 1;
    }
    a
}

/// フォーカス窓 (最前面) の配送先。無ければ None。
pub struct Target {
    pub slot: usize,
    pub win_id: u32,
    pub cox: i32,
    pub coy: i32,
}

/* ================================================================ */
/*  ボタンの捕捉 (押下の配送先を離しまで保つ)                        */
/* ================================================================ */

/// 押下をアプリへ配った相手。対になる離しは**どこで離しても同じ相手へ**返す。
///
/// 契約 D1 の「タスクバー領域の入力をアプリへ配送しない」は、押していない
/// ボタンの離しがアプリへ飛ぶのを防ぐための規則なので、**対になる離しには
/// 掛けない**。掛けるとアプリ側のドラッグ状態やウィジェットの armed が
/// 解除されず、押されたままの表示が残る。
#[derive(Clone, Copy)]
struct Capture {
    active: bool,
    slot: usize,
    win_id: u32,
    cox: i32,
    coy: i32,
}

impl Capture {
    const NONE: Capture = Capture {
        active: false,
        slot: 0,
        win_id: 0,
        cox: 0,
        coy: 0,
    };
}

struct CapCell(core::cell::UnsafeCell<[Capture; 2]>);
unsafe impl Sync for CapCell {}
static CAPTURE: CapCell = CapCell(core::cell::UnsafeCell::new([Capture::NONE; 2]));

#[inline]
fn cap(button: u8) -> &'static mut Capture {
    let i = if button == MOUSE_BTN_RIGHT { 1 } else { 0 };
    unsafe { &mut (*CAPTURE.0.get())[i] }
}

/// 捕捉している離しを配る。配ったら true。
///
/// 捕捉が無い = その押下をアプリへ配っていない (WM が処理した / 握り潰した)
/// ので、離しも配らない。
fn release_capture(st: &mut GuiState, mx: i32, my: i32, button: u8) -> bool {
    let c = *cap(button);
    *cap(button) = Capture::NONE;
    if !c.active {
        return false;
    }
    /* 捕捉した窓がもう無ければ捨てる。 */
    if st.win_by_id(c.win_id).is_none() {
        return false;
    }
    let ev = ring::ev_button(
        false,
        c.win_id,
        (mx - c.cox) as i16,
        (my - c.coy) as i16,
        button,
        next_serial(st, c.slot),
    );
    ring::append(st, c.slot, &ev);
    true
}

/// 完全な id の窓への配送先 (窓が消えていれば None)。
pub fn target_of(st: &GuiState, win_id: u32) -> Option<Target> {
    let index = st.win_by_id(win_id)?;
    let owner = st.windows[index].owner;
    let slot = st.slot_of_owner(owner)?;
    let (cox, coy) = st.windows[index].client_origin();
    Some(Target {
        slot,
        win_id,
        cox,
        coy,
    })
}

pub fn focus_target(st: &GuiState) -> Option<Target> {
    let index = st.front_index()?;
    let owner = st.windows[index].owner;
    let slot = st.slot_of_owner(owner)?;
    let (cox, coy) = st.windows[index].client_origin();
    Some(Target {
        slot,
        win_id: st.windows[index].id(index),
        cox,
        coy,
    })
}

/* ================================================================ */
/*  入力取り込み本体                                                 */
/* ================================================================ */

/// 入力を取り込みリングへ流す (契約 T3 / T8)。
pub fn capture(st: &mut GuiState, ctx: Ctx) {
    st.now = unsafe { (os32api::api().get_tick)() };
    capture_keyboard(st, ctx);
    capture_mouse(st, ctx);
}

/// gshell 単独 (窓が 1 枚も無い) ときに WM が横取りするキー (make のみ)。
///
/// **デバッグ専用** ([`crate::DEBUG_SHORTCUTS`])。G5 (v1.2 完成) で既定 `false`
/// に倒したので、製品ではこの関数は入口で戻る = ESC も F1〜F5 も効かない。
/// CUI へ戻る経路は Start → "CUI mode" → 確認ダイアログだけ (契約 S6)。
pub fn standalone_key(st: &mut GuiState, scan: u8) {
    if !crate::DEBUG_SHORTCUTS {
        return;
    }
    if scan == SC_ESC {
        st.quit = true;
    } else if scan >= SC_F1 && scan <= SC_F4 {
        /* F1〜F4 = 検証用アプリの起動 (G2 / G3)。実際に走らせるのは
         * 単独ループ (`lib.rs` の `launch_app`) — ここでは予約だけ。 */
        let p = crate::LAUNCH_APPS[(scan - SC_F1) as usize];
        st.set_launch_path(p, p.len() - 1); /* 末尾 NUL は set 側が付ける */
        st.launch_pending = true;
    } else if scan == SC_F5 {
        /* WM 自身のファイル選択ダイアログ (契約 U4 の標準ダイアログ)。 */
        modal::open_wm_file(st, b"/");
    }
}

/// raw リング (`kbd_trygetrawkey`) のエントリ = `keycode | down<<8 | mods<<9`。
/// `mods` は**そのイベント時点**の修飾状態 (kbd.c の SHIFT_*、MOD_* と同値)。
/// 取り込み時の最新状態で変換すると Shift↓ A↓ A↑ Shift↑ が溜まったときに
/// a/A を取り違えるため、イベントごとの値を使う (レビュー #3 ②)。
/// cooked キュー (`kbd_buf`) は GUI モード中カーネルが積まない (K2) ので触らない。
///
/// **W2 で足したもの** (契約 U2a / U4):
///   1. SHIFT+SPACE は常に WM が消費して FEP を on/off する (配送しない)。
///   2. モーダル中は宛先をダイアログに限定する。
///   3. 押下は先に FEP へ通し、FEP が消費したら `Key` を配送しない。
///      確定文字列は取り込みの最後に `Text` でまとめて流す。
pub(crate) fn capture_keyboard(st: &mut GuiState, ctx: Ctx) {
    /* X4 で見た SHIFT+SPACE をここで実行する (契約 T8: 辞書を開く重い処理は X3)。 */
    if ctx.wm_ui() {
        fep::apply_pending_toggle(st);
        /* X1 (アプリの set_focus 等) でフォーカスが移ったときの未確定文字を、
         * 次の打鍵より先に元の窓へ確定する (票 KBD_NAV §1-6)。 */
        fep::apply_pending_commit(st);
    }

    /* 取りこぼしの差分を dropped に加算 (契約 T3)。 */
    let cur_drop = unsafe { (os32api::api().kbd_dropped_count)() };
    let delta = cur_drop.wrapping_sub(st.last_kbd_dropped);
    st.last_kbd_dropped = cur_drop;
    if delta > 0 {
        if let Some(t) = focus_target(st) {
            let n = if delta > 0xFFFF { 0xFFFF } else { delta as u16 };
            ring::add_dropped(st, t.slot, n);
        }
    }

    loop {
        /* モーダル中の X4 は取り込まない — 宛先はダイアログで、その状態機械を
         * 進めてよいのは X3 だけ (契約 T8)。打鍵はカーネル待ち行列に残す。 */
        if ctx == Ctx::Pump && (modal::is_open() || startmenu::is_open()) {
            break;
        }
        /* X4 は満杯に近ければ取り込まない。X3 は STOP を探して読み続ける。
         * FEP の確定文字列が続く可能性があるので 4 件分を見る。 */
        let space_ok = if modal::is_open() || startmenu::is_open() {
            /* 打鍵の宛先はダイアログ (WM 自身) でリングへは積まない。アプリの
             * リングが満杯でもダイアログは操作できなければならない (W4 §7.5)。 */
            true
        } else {
            match focus_target(st) {
                Some(t) => ring::space(st, t.slot) >= 4,
                None => true, /* 宛先無し: 取り込んでも捨てるだけなので読む (キューを空ける) */
            }
        };
        if !space_ok && ctx == Ctx::Pump {
            break;
        }
        /* X4 (ポンプ) で FEP がオンなら変換をここでは行わない (契約 T8: 辞書検索
         * まで走る)。退避して次の X3 に回す。SHIFT+SPACE が保留中 (on/off が次の
         * X3 で確定する) のときと、既に退避した打鍵があるときも退避する — でないと
         * 「SHIFT+SPACE → n i h o n g o」が X4 に来た場合、toggle 前の打鍵が FEP を
         * 素通りして先にアプリへ届く (2026-09-06 実測: 変換されず "nihongo" が入った)。
         *
         * 退避先の空きは **カーネルから取り出す前に** 見る (レビュー #5 ①)。取り出した
         * 後に満杯を知ると、その 1 件はカーネル待ち行列にも退避列にも残らず、dropped
         * にも数えられずに消える。空きが無ければ読まずにカーネル側へ残す (契約 T3)。 */
        let defer = ctx == Ctx::Pump
            && (fep::is_on() || fep::toggle_pending() || st.pending_raw_n > 0);
        /* WM のキー (票 KBD_NAV §1-4) かどうかは raw を**取り出した後**でないと
         * 分からないので、X4 では退避先に空きが無ければ (退避しない種類の
         * キーであっても) 読まずに止める。 */
        if ctx == Ctx::Pump && st.pending_raw_n >= st.pending_raw.len() {
            break;
        }
        /* X3 は前の X4 が退避した raw を先に消費する (順序を保つ)。 */
        let raw = if ctx.wm_ui() && st.pending_raw_n > 0 {
            let r = st.pending_raw[0];
            let mut q = 1;
            while q < st.pending_raw_n {
                st.pending_raw[q - 1] = st.pending_raw[q];
                q += 1;
            }
            st.pending_raw_n -= 1;
            r
        } else {
            unsafe { (os32api::api().kbd_trygetrawkey)() }
        };
        if raw < 0 {
            break;
        }
        /* WM のショートカットとマウスキーは状態機械を動かすので X3 でだけ
         * 実行する (票 KBD_NAV §1-4)。X4 で見つけたら FEP と同じく、この raw と
         * 以後を全部退避して次の X3 へ回す (順序を保つ)。 */
        if defer || (ctx == Ctx::Pump && kbdnav::is_wm_raw(st, raw)) {
            /* 空きは取り出す前に確認済み。 */
            st.pending_raw[st.pending_raw_n] = raw;
            st.pending_raw_n += 1;
            continue;
        }
        let scan = (raw & 0x7F) as u8;
        let down = ((raw >> 8) & 1) != 0;
        let mods = ((raw >> 9) & 0x7F) as u32; /* イベント時点の修飾状態 */
        /* X3 must reach STOP behind undeliverable input. Count every discarded
         * raw event, without running FEP/WM actions for it. */
        if !space_ok {
            if scan == SC_STOP && down && (mods & MOD_CTRL) != 0 {
                st.abort_seen = true;
                // Preserve later raw input until STOP changes the foreground.
                break;
            } else if let Some(t) = focus_target(st) {
                ring::add_dropped(st, t.slot, 1);
            }
            continue;
        }
        /* X4 まで来た make は WM のキーではない (上で退避していない)。古い印を
         * 消して、対になる break を X4 がそのまま配れるようにする。 */
        if ctx == Ctx::Pump && down {
            kbdnav::pump_delivered_make(st, scan);
        }

        /* 修飾キーとして捨てる**前**に: カナ (マウスキーの切り替え) と、GRPH+TAB
         * 切り替え中の GRPH の離し (票 KBD_NAV §1-2 / §1-5)。 */
        if ctx.wm_ui() && kbdnav::pre(st, scan, down, mods) {
            continue;
        }

        /* 修飾キー自体は Key として配送しない (状態は mods で見る)。 */
        if scan >= SC_SHIFT && scan <= SC_CTRL {
            continue;
        }
        /* CTRL+STOP (契約 T6): カーネルは要求を立てて cooked には配らないが、raw には
         * 積む。アプリが OP_WAIT で寝ている間はカーネルの判定点 (syscall 入口 /
         * CPL=3 を割り込んだ IRQ1) を通らないので、WM が待ちを抜けてアプリへ戻し、
         * syscall の出口で畳ませる (exec.c ring3_syscall_dispatch)。打鍵としては
         * どこにも配らない (2026-09-06、v1.2 G2 で実測した隙間)。 */
        if scan == SC_STOP && (mods & MOD_CTRL) != 0 {
            if down {
                st.abort_seen = true;
            }
            continue;
        }

        /* SHIFT+SPACE = FEP の on/off。**常に WM が消費**し、押下も離しも
         * アプリへ配送しない (契約 U2a)。 */
        if scan == SC_SPACE && (mods & MOD_SHIFT) != 0 {
            if down {
                if ctx.wm_ui() {
                    fep::toggle(st);
                } else {
                    fep::request_toggle();
                }
            }
            continue;
        }

        /* WM のショートカットとマウスキー (票 KBD_NAV §1-2 / §1-3)。モーダル・
         * メニュー・FEP・アプリより前。状態ごとの扱いは kbdnav 側の表。 */
        if ctx.wm_ui() && kbdnav::on_key(st, scan, down, mods) {
            continue;
        }

        /* モーダル中は宛先をダイアログに限定する (契約 U4)。
         *
         * W4 §6: Input dialog では**先に FEP へ通す**。WM がアプリへ配るときと
         * 同じ経路 (`fep::feed`) を使い、消費された打鍵はダイアログにも渡さない
         * (変換中の BS が field の本文まで消す事故を防ぐ)。確定文字列は
         * `fep::flush_text` が field へ差し込む。 */
        if modal::is_open() {
            if down && ctx.wm_ui() {
                let ch = translate(scan, mods);
                if modal::is_input() && fep::feed(st, scan, ch, mods) == fep::Fed::Consumed {
                    continue;
                }
                /* 確定した文字は**次の打鍵を処理する前に** field へ入れる。
                 * flush が周期末尾のままだと、Enter 連打で確定 Enter と決定
                 * Enter が同じ吸い出し周期に入ったとき、確定文字が field に
                 * 入る前にダイアログが閉じて結果から抜ける。 */
                fep::flush_text(st);
                modal::on_key(st, scan, ch, mods);
            }
            continue;
        }

        /* Start / context メニュー (契約 D2 / D4): UP/DOWN/RETURN/ESC。
         * モーダルより下・アプリより上。X3 でだけ状態機械を進める。 */
        if startmenu::is_open() {
            if down && ctx.wm_ui() {
                startmenu::on_key(st, scan);
            }
            continue;
        }

        /* WM 単独時 (フォーカス窓なし) の横取り。**デバッグ専用**で、製品
         * (`DEBUG_SHORTCUTS == false`) では `standalone_key` が即戻るため何も
         * 起きない。アプリの OP_WAIT (Ctx::Wait) では元から横取りしない
         * — ESC はアプリのもの。メニュー / モーダルの ESC はここより上で処理済み。 */
        if ctx == Ctx::Standalone && st.front_index().is_none() {
            if down {
                standalone_key(st, scan);
            }
            continue;
        }

        let ch = translate(scan, mods);

        /* X1 の set_focus 等で予約した確定を、モーダルが閉じた後の最初の打鍵より
         * 先に元の窓へ流す (同じ周期でモーダルが閉じた場合、票 KBD_NAV §1-6)。 */
        if ctx.wm_ui() {
            fep::apply_pending_commit(st);
        }

        /* 押下は**先に FEP へ通す** (契約 U2a)。FEP が消費したキー (かな入力、
         * 未確定の編集、候補操作、確定、取消) は `Key` として配送しない。 */
        if down && fep::feed(st, scan, ch, mods) == fep::Fed::Consumed {
            continue;
        }

        /* 同じ理由 (モーダル側の注記)。ここでは確定文字の `Text` を、この
         * 打鍵の `Key` / `Text` より**先に**リングへ積んで順序を保つ。 */
        fep::flush_text(st);

        let t = match focus_target(st) {
            Some(t) => t,
            None => continue,
        };
        let serial = next_serial(st, t.slot);
        let ev = ring::ev_key(down, t.win_id, scan, ch, mods as u8, serial);
        ring::append(st, t.slot, &ev);

        /* 印字可能キーは Text も配送 (契約 U2a。FEP が消費していれば上で
         * continue しているので二重にはならない)。 */
        if down && ch >= 0x20 && ch <= 0x7E {
            let mut utf8 = [0u8; 8];
            utf8[0] = ch;
            let s2 = next_serial(st, t.slot);
            let evt = ring::ev_text(t.win_id, utf8, 1, s2);
            ring::append(st, t.slot, &evt);
        }
    }

    /* FEP の確定文字列を `Text` (8B ずつ、`more`) で流す (契約 U2)。 */
    fep::flush_text(st);
}

fn capture_mouse(st: &mut GuiState, ctx: Ctx) {
    let mut mi = MouseInfo {
        x: 0,
        y: 0,
        dx: 0,
        dy: 0,
        buttons: 0,
        mode: 0,
    };
    unsafe {
        (os32api::api().mouse_poll)(&mut mi as *mut MouseInfo as *mut u8);
    }
    /* 全画面 GFX 中はマウスを**誰にも配らない** (票 T8 D4c: タスクバー /
     * Start / 窓を含めて無視)。位置と押下だけ同期しておき、復帰した最初の
     * 周期で古いエッジが「クリック」に化けるのを防ぐ。カーソルも描かない
     * (画面はプログラムのもの)。 */
    if crate::fullscreen::active() {
        st.mouse_x = mi.x as i32;
        st.mouse_y = mi.y as i32;
        st.real_x = st.mouse_x;
        st.real_y = st.mouse_y;
        st.prev_buttons = mi.buttons;
        return;
    }
    /* 位置は**実マウスが動いたときだけ**実マウスの値へ (最後に動いた方が勝つ)。
     * マウスキー (票 KBD_NAV §1-3) が動かした位置を、止まっている実マウスの
     * 値で毎周巻き戻さない。 */
    let rx = mi.x as i32;
    let ry = mi.y as i32;
    let real_moved = rx != st.real_x || ry != st.real_y;
    st.real_x = rx;
    st.real_y = ry;
    let (mx, my) = if real_moved { (rx, ry) } else { (st.mouse_x, st.mouse_y) };
    let moved = mx != st.mouse_x || my != st.mouse_y;
    st.mouse_x = mx;
    st.mouse_y = my;
    let btn = mi.buttons;
    /* 左右それぞれのエッジ (契約 D4 / W3 §4.2)。右は v1.1 では見ていなかった。 */
    let down_edge = (btn & MOUSE_BTN_LEFT) != 0 && (st.prev_buttons & MOUSE_BTN_LEFT) == 0;
    let up_edge = (btn & MOUSE_BTN_LEFT) == 0 && (st.prev_buttons & MOUSE_BTN_LEFT) != 0;
    let rdown_edge = (btn & MOUSE_BTN_RIGHT) != 0 && (st.prev_buttons & MOUSE_BTN_RIGHT) == 0;
    let rup_edge = (btn & MOUSE_BTN_RIGHT) == 0 && (st.prev_buttons & MOUSE_BTN_RIGHT) != 0;
    /* アプリへ配る `buttons` は実マウスと合成ボタン (マウスキー) の和。 */
    let pbtn = btn | st.synth_buttons;

    /* ---- モーダル中は宛先をダイアログに限定する (契約 U4) ---- */
    if modal::is_open() {
        if moved {
            cursor::move_to(st, mx, my);
        }
        /* 状態機械を進めるのは X3 だけ (契約 T8)。X4 (ポンプ) では
         * prev_buttons を**進めない** — 進めると X4 が先に押下を見たとき
         * 次の X3 に down_edge が立たず、ダイアログのボタンが反応しない
         * (レビュー #4 ④)。X3 が現在値と prev を比べてエッジを拾う。
         * エッジの扱いは [`edge_x3`] のモーダル分岐 (マウスキーと共通)。 */
        if ctx.wm_ui() {
            if down_edge {
                edge_x3(st, mx, my, MOUSE_BTN_LEFT, true);
            }
            if up_edge {
                edge_x3(st, mx, my, MOUSE_BTN_LEFT, false);
            }
            if rup_edge {
                edge_x3(st, mx, my, MOUSE_BTN_RIGHT, false);
            }
            st.prev_buttons = btn;
        }
        return;
    }

    if ctx.wm_ui() {
        /* ---- ドラッグ追従 (枠だけ動かす。実体は drop で移す。R2) ----
         * キーボードの移動・サイズ中 (窓メニュー) は枠をキーが持つ。 */
        if st.drag_index >= 0 && !kbdnav::kmove_busy(st) {
            update_drag(st, mx, my);
        } else if moved {
            /* カーソルの移動は損傷に含めない (別経路で退避・再描画)。 */
            cursor::move_to(st, mx, my);
        }
        if down_edge {
            edge_x3(st, mx, my, MOUSE_BTN_LEFT, true);
        } else if up_edge {
            edge_x3(st, mx, my, MOUSE_BTN_LEFT, false);
        }
        if rdown_edge {
            edge_x3(st, mx, my, MOUSE_BTN_RIGHT, true);
        } else if rup_edge {
            edge_x3(st, mx, my, MOUSE_BTN_RIGHT, false);
        }
        if moved && !down_edge && !up_edge && !rdown_edge && !rup_edge {
            /* 移動: フォーカス窓へ Pointer (畳み込み)。
             * **動いたときだけ** — `moved` を見ないと、マウスが止まっていても
             * `wm_cycle` のたびに `Pointer` を 1 件積んでしまう。リングが空だと
             * `ring::append` の畳み込みも効かないので、そのままアプリのリングに
             * 溜まり、`OP_WAIT` は `ring::pending > 0` で毎回すぐ戻る = アプリは
             * 二度と眠らず PIT の速さで回り続ける (v1.2 G3 実測: 無操作で毎秒
             * 33 周、マウス移動中は毎秒 2000 周)。契約 P と U5 の趣旨に反する。 */
            forward_pointer(st, mx, my, pbtn);
        }
    } else {
        /* ポンプ (X4): 状態機械を進めず、入力の追記とカーソル移動だけ。
         *
         * ボタンのエッジは **WM が扱うものなら prev_buttons を進めない**
         * (モーダルと同じ扱い、レビュー #4 ④ の一般形)。進めてしまうと X4 が
         * 先に見たエッジは次の X3 に立たず、ドラッグ中の離しは永久に来ず
         * (枠が残ったまま次の押下まで動き続ける)、タイトルバー / 閉じる /
         * 背面窓への押下はアプリのクライアントへ誤配される (2026-09-06 に
         * /api/mouse で実測: 離しが失われ、× を押すと前面化だけ起きた)。
         * アプリの領分 (前面窓のクライアント / 枠) のエッジだけをここで配る。 */
        if moved {
            cursor::move_to(st, mx, my);
        }
        /* 左右のどちらかでも WM の領分なら **prev_buttons を進めずに戻る**。
         * 次の X3 が両方のエッジを derive し直すので二重配送にはならない。 */
        let hold_l =
            (down_edge || up_edge) && wm_owns_edge(st, mx, my, down_edge, MOUSE_BTN_LEFT);
        let hold_r =
            (rdown_edge || rup_edge) && wm_owns_edge(st, mx, my, rdown_edge, MOUSE_BTN_RIGHT);
        if hold_l || hold_r {
            return;
        }
        /* 離しは X3 と同じく捕捉経由で返す。ここで直接配ると捕捉が残り、
         * 次の無関係な離しが古い相手へ飛ぶ。 */
        if down_edge {
            forward_button(st, mx, my, MOUSE_BTN_LEFT, true);
        } else if up_edge {
            release_capture(st, mx, my, MOUSE_BTN_LEFT);
        }
        if rdown_edge {
            forward_button(st, mx, my, MOUSE_BTN_RIGHT, true);
        } else if rup_edge {
            release_capture(st, mx, my, MOUSE_BTN_RIGHT);
        }
        if moved && !down_edge && !up_edge && !rdown_edge && !rup_edge {
            /* X3 と同じ理由で `moved` が要る (上のコメント)。 */
            forward_pointer(st, mx, my, pbtn);
        }
    }
    st.prev_buttons = btn;
}

/// ボタンのエッジ 1 つを X3 で処理する。**実マウスとマウスキーの共通経路**
/// (票 KBD_NAV §1-3): モーダル中はダイアログへ (背後の窓へは届かない)、
/// それ以外は WM の状態機械 (`wm_button_down/up`・`wm_right_down/up`)。
pub fn edge_x3(st: &mut GuiState, x: i32, y: i32, button: u8, down: bool) {
    if modal::is_open() {
        if down {
            if button == MOUSE_BTN_LEFT {
                let _ = modal::on_button(st, x, y);
            }
        } else {
            /* 契約 U4 は**新しい入力**の宛先を決める規則で、モーダルが開く前の
             * 押下と対になる離しは、その押下を受けたアプリのもの。ここで
             * 捕捉を返さないと、アプリ内で押したまま自分でダイアログを開いた
             * 場合 (OP_MODAL_OPEN) に離しが永久に届かない (レビュー #4 [P2])。
             * 捕捉が無ければ何も配らないので、モーダル中に始まった押下・離しは
             * 今までどおりダイアログだけのものになる。 */
            release_capture(st, x, y, button);
        }
        return;
    }
    /* キーボードの移動・サイズ中はマウスで枠を動かさない。前に配った押下の
     * 離しだけは相手へ返す (押されたままを残さない)。 */
    if kbdnav::kmove_busy(st) {
        if !down {
            release_capture(st, x, y, button);
        }
        return;
    }
    match (button, down) {
        (MOUSE_BTN_LEFT, true) => wm_button_down(st, x, y),
        (MOUSE_BTN_LEFT, false) => wm_button_up(st, x, y),
        (MOUSE_BTN_RIGHT, true) => wm_right_down(st, x, y),
        (MOUSE_BTN_RIGHT, false) => wm_right_up(st, x, y),
        _ => {}
    }
}

/// ポインタの移動 1 回を X3 で処理する (マウスキー用、票 KBD_NAV §1-3)。
/// 実マウスの移動と同じ分岐: モーダル中はカーソルだけ、WM のドラッグ中は
/// 枠の追従、それ以外はカーソルとフォーカス窓への `Pointer`。
pub fn move_x3(st: &mut GuiState, x: i32, y: i32) {
    st.mouse_x = x;
    st.mouse_y = y;
    if modal::is_open() || kbdnav::kmove_busy(st) {
        cursor::move_to(st, x, y);
        return;
    }
    if st.drag_index >= 0 {
        update_drag(st, x, y);
    } else {
        cursor::move_to(st, x, y);
    }
    let b = st.prev_buttons | st.synth_buttons;
    forward_pointer(st, x, y, b);
}

/// このボタンエッジは WM の状態機械 (X3) が処理すべきものか。
/// - メニューが開いている / タスクバーの上: 左右とも押下も離しも WM
///   (契約 D1「taskbar 領域の入力をアプリへ配送しない」)
/// - ドラッグ中: 押下も離しも WM (離しで drop する)
/// - 押下: 窓の外 (デスクトップ) / 背面の窓 (前面化 + Focus) / 閉じるボタン /
///   タイトルバー (ドラッグ開始) は WM。前面窓のクライアント・枠だけがアプリ
/// - 右ボタン: デスクトップ / タスクバーは WM の context menu、前面窓の
///   クライアントはアプリ (契約 D4 / W3 §4.2)
///
/// [`wm_button_down`] / [`wm_right_down`] と**同じ判定順**で見る
/// (ずれると二重配送か取りこぼしになる。POLICY_DEBUG §4-22)。
fn wm_owns_edge(st: &GuiState, mx: i32, my: i32, down_edge: bool, button: u8) -> bool {
    if startmenu::is_open() {
        return true;
    }
    /* 押下でメニューが閉じた直後の離しは WM が捨てる (X3 で消費する)。 */
    if !down_edge && startmenu::swallow_up_pending() {
        return true;
    }
    if st.drag_index >= 0 {
        return true;
    }
    if taskbar::hit(st, mx, my) {
        return true;
    }
    if !down_edge {
        return false; /* ドラッグ外の離しはアプリの領分 */
    }
    let idx = match st.hit_window(mx, my) {
        Some(i) => i,
        None => return true, /* デスクトップ: 左=無視 / 右=context menu */
    };
    if st.front_index() != Some(idx) {
        return true;
    }
    if button == MOUSE_BTN_RIGHT {
        /* 契約 D4: 前面窓でも**クライアント上だけ**がアプリの領分。
         * タイトルバー / 枠は WM が握り潰す ([`wm_right_down`] と同じ判定)。 */
        return !st.windows[idx].client_rect_screen().contains(mx, my);
    }
    let w = st.windows[idx];
    if w.has_close() && w.close_rect().contains(mx, my) {
        return true;
    }
    if w.movable() && w.titlebar_rect().contains(mx, my) {
        return true;
    }
    false
}

/* ---- WM 状態機械 (X3 のみ) ---- */

fn wm_button_down(st: &mut GuiState, mx: i32, my: i32) {
    /* メニューが開いている間は押下を全部メニューが取る (外側は「閉じる」)。 */
    if startmenu::is_open() {
        startmenu::on_button(st, mx, my);
        return;
    }
    /* タスクバー: Start / 窓ボタン / 時計。アプリへは配送しない (契約 D1)。 */
    if taskbar::hit(st, mx, my) {
        taskbar::on_button(st, mx, my);
        return;
    }
    let hit = st.hit_window(mx, my);
    let idx = match hit {
        Some(i) => i,
        None => return,
    };
    /* 前面化 + フォーカス切替 (Focus イベント)。未確定文字は元の窓へ確定
     * してから移す (票 KBD_NAV §1-6)。 */
    let changed = st.front_index() != Some(idx);
    if changed {
        wm::focus_leaving(st, true);
    }
    let old_front = st.front_id();
    if changed {
        st.bring_to_front(idx);
        visible::recompute_and_expose(st);
        let vac = st.windows[idx].outer();
        st.dirty_screen(vac);
        let new_front = st.windows[idx].id(idx);
        emit_focus_change(st, old_front, new_front);
    }

    /* 閉じるボタン? */
    let w = st.windows[idx];
    if w.has_close() && w.close_rect().contains(mx, my) {
        if let Some(slot) = st.slot_of_owner(w.owner) {
            let ev = ring::ev_simple(os32api::gui::proto::GUI_EV_CLOSE, 0, w.id(idx));
            ring::append(st, slot, &ev);
        }
        return;
    }
    /* タイトルバー? → ドラッグ開始 (実体は動かさない)。 */
    if w.movable() && w.titlebar_rect().contains(mx, my) {
        st.drag_index = idx as i32;
        st.drag_dx = mx - w.x;
        st.drag_dy = my - w.y;
        st.drag_frame = w.outer();
        cursor::hide(st);
        draw_live_frame(st, &[]);
        cursor::show(st);
        let cr = cursor::rect(st);
        wm::queue_present(st, cr);
        wm::flush_present();
        return;
    }
    /* それ以外 (クライアント / 枠) → Button をアプリへ。 */
    forward_button(st, mx, my, MOUSE_BTN_LEFT, true);
}

fn wm_button_up(st: &mut GuiState, mx: i32, my: i32) {
    if st.drag_index >= 0 {
        let idx = st.drag_index as usize;
        let old_outer = st.windows[idx].outer();
        /* 枠の最終位置へ実体を移す。ドラッグ確定は作業領域へクランプする
         * (契約 D1。枠の追従でも同じ規則を使っているので普通は動かない)。 */
        let nf = st.drag_frame;
        let (cx, cy) = wm::clamp_to_work_area(st, nf.x, nf.y, nf.w, nf.h);
        st.windows[idx].x = cx;
        st.windows[idx].y = cy;
        st.drag_index = -1;
        st.drag_frame = Rect::EMPTY;
        /* 最後の枠が確定後の外形と違えば、枠の縁を消す (下地は損傷、クライアント
         * 面は Paint)。普通は update_drag が同じクランプで枠を決めるので一致するが、
         * ドラッグ中にアプリが resize_window し、マウスキーの離し (NP_DOT) が
         * update_drag を通らずに確定すると大きさがずれる (Codex レビュー 2 回目 P2)。 */
        if nf != st.windows[idx].outer() {
            for e in frame_edges(nf).iter() {
                st.dirty_screen(*e);
            }
            expose_frame_edges(st, nf);
        }

        /* 露出計算 + 旧位置と新位置を画面損傷に。 */
        st.dirty_screen(old_outer);
        st.dirty_screen(st.windows[idx].outer());
        visible::recompute_and_expose(st);
        /* 移動した窓のクライアントは全面再描画が要る (中身は同じでも位置が変わる)。 */
        crate::damage::set_dirty_full(&mut st.windows[idx]);
        /* Configure を通知 (座標確定、R2)。 */
        st.windows[idx].configure_pending = true;
        emit_configure(st, idx);
        return;
    }
    /* メニューの押下で閉じた直後の離しを捨てる旗は、ここで必ず消費する。
     * (その押下はアプリへ配っていないので捕捉も無く、下も何もしない。) */
    startmenu::take_swallow_up();
    /* 押下をアプリへ配っていたなら、**どこで離しても**同じ相手へ返す。
     * タスクバーの上で離しても取りこぼさない (押されたままの表示が残る)。
     * 捕捉が無い = その押下を配っていない = 離しも配らない (契約 D1 の
     * 「押していないボタンの離しを作らない」はこちらで担保される)。 */
    release_capture(st, mx, my, MOUSE_BTN_LEFT);
}

/* ---- 右ボタン (契約 D4 / W3 §4.2) ---- */

/// 右押下。デスクトップ / タスクバーは WM の context menu、前面窓の
/// クライアントはアプリへ `Button{button=2}`。判定順は [`wm_owns_edge`] と対。
fn wm_right_down(st: &mut GuiState, mx: i32, my: i32) {
    if startmenu::is_open() {
        startmenu::close(st);
        startmenu::set_swallow_up(); /* 対になる離しは捨てる */
        return;
    }
    if st.drag_index >= 0 {
        return; /* ドラッグ中の右クリックは捨てる */
    }
    if taskbar::hit(st, mx, my) {
        startmenu::open_context(st, mx, my);
        return;
    }
    let idx = match st.hit_window(mx, my) {
        Some(i) => i,
        None => {
            startmenu::open_context(st, mx, my);
            return;
        }
    };
    if st.front_index() != Some(idx) {
        /* 背面窓の上: 前面化 + フォーカスだけ (左と同じ)。 */
        wm::focus_leaving(st, true);
        let old_front = st.front_id();
        st.bring_to_front(idx);
        visible::recompute_and_expose(st);
        let vac = st.windows[idx].outer();
        st.dirty_screen(vac);
        let new_front = st.windows[idx].id(idx);
        emit_focus_change(st, old_front, new_front);
        return;
    }
    /* 契約 D4: 配るのは**前面窓のクライアント上**だけ。hit_window は外形
     * (タイトルバー・枠を含む) なので、ここで client 矩形を見ないと
     * 負のクライアント座標がアプリへ届く。タイトルバー / 枠の右クリックは
     * WM が握り潰す (対になる離しも捕捉が無いので配らない)。 */
    if !st.windows[idx].client_rect_screen().contains(mx, my) {
        return;
    }
    forward_button(st, mx, my, MOUSE_BTN_RIGHT, true);
}

fn wm_right_up(st: &mut GuiState, mx: i32, my: i32) {
    /* 左と同じ (`wm_button_up` の注記)。 */
    startmenu::take_swallow_up();
    release_capture(st, mx, my, MOUSE_BTN_RIGHT);
}

fn update_drag(st: &mut GuiState, mx: i32, my: i32) {
    let idx = st.drag_index as usize;
    let w = st.windows[idx];
    let nx0 = mx - st.drag_dx;
    let ny0 = my - st.drag_dy;
    /* 作業領域 (画面 − タスクバー) へクランプする (契約 D1)。 */
    let (nx, ny) = wm::clamp_to_work_area(st, nx0, ny0, w.w, w.h);
    redraw_frame(st, Rect::new(nx, ny, w.w, w.h));
}

/// ドラッグ枠を `new_frame` へ描き替える (マウスのドラッグとキーボードの
/// 移動・サイズの共通経路)。旧枠を下地で消し、新枠とカーソルを 1 回で present。
pub fn redraw_frame(st: &mut GuiState, new_frame: Rect) {
    let old_frame = st.drag_frame;
    let old_cursor = cursor::rect(st);
    let cursor_moved = st.mouse_x != st.cursor.x || st.mouse_y != st.cursor.y;
    if new_frame == old_frame && !cursor_moved {
        return;
    }
    st.drag_frame = new_frame;
    /* 旧枠を下地で消し、新枠を描いて、両者の縁とカーソルを 1 回で present。 */
    cursor::hide(st);
    erase_frame_edges(st, old_frame);
    expose_frame_edges(st, old_frame);
    /* 旧枠の消去 (下地の再合成) は FEP の候補窓を描かないので、旧枠の縁も
     * 最前面物の判定に入れる。 */
    let old_edges = if old_frame.is_empty() { [Rect::EMPTY; 4] } else { frame_edges(old_frame) };
    draw_live_frame(st, &old_edges);
    st.cursor.x = st.mouse_x;
    st.cursor.y = st.mouse_y;
    cursor::show(st);
    queue_frame_edges(st, old_frame);
    wm::queue_present(st, old_cursor);
    let cr = cursor::rect(st);
    wm::queue_present(st, cr);
    wm::flush_present();
}

/// キーボードの移動・サイズを始める: 窓 `idx` の外形に枠を描く
/// (マウスのタイトルバー押下と同じ描き方)。
pub fn begin_frame(st: &mut GuiState, idx: usize) {
    let w = st.windows[idx];
    st.drag_index = idx as i32;
    st.drag_dx = 0;
    st.drag_dy = 0;
    st.drag_frame = w.outer();
    cursor::hide(st);
    draw_live_frame(st, &[]);
    cursor::show(st);
    let cr = cursor::rect(st);
    wm::queue_present(st, cr);
    wm::flush_present();
}

/// 枠を消す (キーボードの移動・サイズの終わり)。枠は窓のクライアントにも
/// 掛かるので、下地に加えて掛かった窓へ露出の `Paint` を返す
/// (メニューを閉じるときと同じ扱い)。
pub fn erase_frame(st: &mut GuiState, f: Rect) {
    if f.is_empty() {
        return;
    }
    st.dirty_screen(f);
    let mut i = 0;
    while i < st.windows.len() {
        if st.windows[i].used && st.windows[i].visible {
            let (ox, oy) = st.windows[i].client_origin();
            crate::damage::add_dirty(&mut st.windows[i], f.translate(-ox, -oy));
        }
        i += 1;
    }
}

/* ---- アプリへの配送 ---- */

fn forward_pointer(st: &mut GuiState, mx: i32, my: i32, btn: u8) {
    /* WM の領分 (メニュー / タスクバー) の上ではアプリへ動きも配らない
     * (契約 D1「taskbar 領域の入力をアプリへ配送しない」)。 */
    if startmenu::is_open() || taskbar::hit(st, mx, my) {
        return;
    }
    let t = match focus_target(st) {
        Some(t) => t,
        None => return,
    };
    let cx = (mx - t.cox) as i16;
    let cy = (my - t.coy) as i16;
    let serial = next_serial(st, t.slot);
    let ev = ring::ev_pointer(t.win_id, cx, cy, btn, serial);
    ring::append(st, t.slot, &ev);
}

/// `Button` をフォーカス窓へ配る。`button` は `MOUSE_BTN_LEFT` (1) /
/// `MOUSE_BTN_RIGHT` (2) をそのまま `GuiEvtButton.button` に載せる (契約 D4)。
fn forward_button(st: &mut GuiState, mx: i32, my: i32, button: u8, down: bool) {
    let t = match focus_target(st) {
        Some(t) => t,
        None => return,
    };
    if down {
        /* 対になる離しは、どこで離してもこの相手へ返す。 */
        *cap(button) = Capture {
            active: true,
            slot: t.slot,
            win_id: t.win_id,
            cox: t.cox,
            coy: t.coy,
        };
    }
    let cx = (mx - t.cox) as i16;
    let cy = (my - t.coy) as i16;
    let serial = next_serial(st, t.slot);
    let ev = ring::ev_button(down, t.win_id, cx, cy, button, serial);
    ring::append(st, t.slot, &ev);
}

/* ---- Focus / Configure ---- */

/// フォーカスの移動を両者へ `Focus` で知らせる (契約 U2)。
pub fn emit_focus_change(st: &mut GuiState, old_id: u32, new_id: u32) {
    /* GRPH+TAB の並び (票 KBD_NAV §1-2 の MRU)。 */
    kbdnav::mru_touch(st, new_id);
    if old_id != 0 {
        if let Some(oi) = st.win_by_id(old_id) {
            if let Some(slot) = st.slot_of_owner(st.windows[oi].owner) {
                let ev = ring::ev_simple(GUI_EV_FOCUS, 0, old_id);
                ring::append(st, slot, &ev);
            }
        }
    }
    if new_id != 0 {
        if let Some(ni) = st.win_by_id(new_id) {
            if let Some(slot) = st.slot_of_owner(st.windows[ni].owner) {
                let ev = ring::ev_simple(GUI_EV_FOCUS, 1, new_id);
                ring::append(st, slot, &ev);
            }
        }
    }
}

/// 座標が確定したウィンドウの `Configure` (導出型) を 1 件流す。
/// 矩形は**クライアント矩形の画面絶対座標** (C2 が `create_window_surface` の
/// 原点に使う)。リングに入らなければ `configure_pending` を残して次周へ。
pub fn emit_configure(st: &mut GuiState, index: usize) {
    let w = &st.windows[index];
    if !w.configure_pending {
        return;
    }
    let owner = w.owner;
    let id = w.id(index);
    let (cw, ch) = w.client_size();
    let (cox, coy) = w.client_origin();
    let rect = GuiRect16 {
        x: cox as i16,
        y: coy as i16,
        w: cw as i16,
        h: ch as i16,
    };
    if let Some(slot) = st.slot_of_owner(owner) {
        let ev = ring::ev_rect(GUI_EV_CONFIGURE, id, rect);
        if ring::append(st, slot, &ev) {
            st.windows[index].configure_pending = false;
        }
    }
}

/// 入力イベントの `serial` を 1 つ払い出し、**取り込んだ tick を記録する**
/// (契約 P2: serial ごとに直近 64 件をスロットの予備領域へ)。
#[inline]
pub fn next_serial(st: &mut GuiState, slot_no: usize) -> u16 {
    st.slots[slot_no].serial = st.slots[slot_no].serial.wrapping_add(1);
    let s = st.slots[slot_no].serial;
    slot::record_trace(st, slot_no, s, st.now);
    s
}

/* ---- ドラッグ枠の縁の present / 消去 (wm compositor へ委譲) ---- */

fn frame_edges(f: Rect) -> [Rect; 4] {
    let t = 2; /* 枠の太さ */
    [
        Rect::new(f.x, f.y, f.w, t),                       /* 上 */
        Rect::new(f.x, f.y + f.h - t, f.w, t),             /* 下 */
        Rect::new(f.x, f.y, t, f.h),                       /* 左 */
        Rect::new(f.x + f.w - t, f.y, t, f.h),             /* 右 */
    ]
}

/// 枠の縁 4 本を転送キューへ積む (flush は呼ぶ側で 1 回)。
fn queue_frame_edges(st: &GuiState, f: Rect) {
    for e in frame_edges(f).iter() {
        wm::queue_present(st, *e);
    }
}

fn erase_frame_edges(st: &mut GuiState, f: Rect) {
    for e in frame_edges(f).iter() {
        wm::composite_rect(st, *e);
    }
}

/// 枠の縁 4 本が横切った窓のクライアント面へ `Paint` を返す (損傷に足す)。
///
/// 枠はバックバッファへ**そのまま**描いている (XOR ではない)。消去の
/// [`erase_frame_edges`] (= `composite_rect`) は下地とクロームしか描き直さず、
/// クライアント面は WM が持っていないので、枠が窓の中を通った跡はアプリに
/// 描き直させる以外に消せない。これが無かったので、途中位置の枠の線が背面の
/// 窓 (gui_gate v11 の Help) に残っていた (2026-09-29、PEGC / Cirrus)。
/// 足すのは縁の帯だけなので、アプリが描き直すのは細い帯で済む。
fn expose_frame_edges(st: &mut GuiState, f: Rect) {
    if f.is_empty() {
        return;
    }
    for e in frame_edges(f).iter() {
        let mut i = 0;
        while i < st.windows.len() {
            if st.windows[i].used && st.windows[i].visible {
                let (ox, oy) = st.windows[i].client_origin();
                crate::damage::add_dirty_band(&mut st.windows[i], e.translate(-ox, -oy));
            }
            i += 1;
        }
    }
}

/// 今のドラッグ枠の縁 4 本のうち `touched` に掛かるものがあれば、縁 4 本を返す
/// (枠は全周を描き直すので 4 本とも)。ドラッグ中でなければ / 掛からなければ `None`。
///
/// 旧枠の帯は 32px に丸めて Paint にするので、今の枠にも掛かることがある。アプリは
/// Paint の矩形を丸ごと塗るので、そのまま present すると今の枠の一部が消え、次の
/// WM 周で戻る = ちらつく (Codex レビュー P2)。COMMIT (X2) はこれを見て枠を戻す。
pub fn live_frame_edges_hit(st: &GuiState, touched: Rect) -> Option<[Rect; 4]> {
    if st.drag_index < 0 || st.drag_frame.is_empty() {
        return None;
    }
    let es = frame_edges(st.drag_frame);
    if es.iter().any(|e| e.intersects(&touched)) {
        Some(es)
    } else {
        None
    }
}

/// 今のドラッグ枠を描いて縁を present に積む (最前面物・カーソルは呼ぶ側)。
pub fn draw_live_outline(st: &GuiState) {
    let f = st.drag_frame;
    crate::chrome::draw_drag_outline(f.x, f.y, f.w, f.h, crate::lease::mono(st));
    queue_frame_edges(st, f);
}

/// 今のドラッグ枠を描き、枠 (と `extra`) が掛かった最前面物 (モーダル /
/// タスクバー / メニュー / FEP の候補窓) を上に描き直す。重なり順は
/// 「アプリ < 枠 < 最前面物 < カーソル」で、X2 (COMMIT) と X3 で揃える
/// (Codex レビュー 3 回目 P2: 枠の縁が FEP の候補窓に線を残していた)。
/// カーソルは呼ぶ側が先に hide し、最後に show する。
pub fn draw_live_frame(st: &mut GuiState, extra: &[Rect]) {
    if st.drag_index < 0 || st.drag_frame.is_empty() {
        return;
    }
    draw_live_outline(st);
    let es = frame_edges(st.drag_frame);
    let mut regions = [Rect::EMPTY; 12];
    let mut n = 0;
    for e in es.iter() {
        regions[n] = *e;
        n += 1;
    }
    for r in extra.iter() {
        if n < regions.len() {
            regions[n] = *r;
            n += 1;
        }
    }
    let ov = wm::overlays_to_refresh(st, &regions[..n]);
    wm::refresh_overlays(st, &ov);
}

#[cfg(test)]
#[path = "../host/wm_tests.rs"]
mod tests;
