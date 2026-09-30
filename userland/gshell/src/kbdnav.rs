//! kbdnav.rs — マウスなしで GUI を操作する (票 `docs/tasks/gui/TASK_KBD_NAV.md`)。
//!
//! 実機 PC-9821Ra266 にバスマウスが無いので、WM の操作とポインタをキーボードだけで
//! 動かせるようにする。割り当ては **Windows 98 に合わせ**、PC-98 に無いキーだけ
//! 読み替える (Alt → GRPH、NumLock → カナ)。
//!
//! | キー | 動作 |
//! |---|---|
//! | CTRL+ESC | スタートメニューを開く / 閉じる |
//! | GRPH+TAB (SHIFT で逆) | 窓の切り替え。**GRPH を離したときに**切り替える。選択はタスクバーのボタンの押し込みで示す |
//! | GRPH+ESC | 最前面の窓を最背面 (MRU の末尾) へ回し、次の窓へ |
//! | GRPH+f･4 | 最前面の窓を閉じる。窓が無ければ Shut Down の確認 |
//! | GRPH+SPACE | 窓メニュー (元のサイズ / 移動 / サイズ / 最小化 / 最大化 / 閉じる) |
//! | SHIFT+f･10 | 窓があれば窓へ配る。無ければデスクトップのメニュー |
//! | カナ (ロック) | マウスキー。テンキー 8 方向でポインタ (CTRL で半分の速度)、5 = 左クリック、- = 右クリック、+ = 左ダブルクリック、0 = 左を押したまま、. = 離す |
//!
//! **状態機械は X3 でだけ動かす** (票 §1-4)。X4 (ポンプ) は [`is_wm_raw`] で
//! 「WM のキーか」だけを判定し、そうなら入力側がその raw 以降を退避する。
//!
//! カナの状態は **raw に焼き込まれた KANA ビット**で決める (票 §1-5)。
//! 現在値 (`kbd_get_modifiers`) を読むのは起動時 ([`init`]) だけ。

use crate::wm::{self, GuiState, Rect};
use crate::{fullscreen, input, modal, ring, startmenu, taskbar};
use os32api::gui::proto::{GUI_EV_CLOSE, GUI_MAX_WINDOWS};

/* ================================================================ */
/*  スキャンコード (drivers/kbd.h) と修飾 (SHIFT_*)                   */
/* ================================================================ */
const SC_ESC: u8 = 0x00; /* KEY_ESC */
const SC_TAB: u8 = 0x0F; /* KEY_TAB */
const SC_RETURN: u8 = 0x1C; /* KEY_RETURN */
const SC_SPACE: u8 = 0x34; /* KEY_SPACE */
const SC_UP: u8 = 0x3A; /* KEY_UP */
const SC_LEFT: u8 = 0x3B; /* KEY_LEFT */
const SC_RIGHT: u8 = 0x3C; /* KEY_RIGHT */
const SC_DOWN: u8 = 0x3D; /* KEY_DOWN */
const SC_F4: u8 = 0x65; /* KEY_F4 */
const SC_F10: u8 = 0x6B; /* KEY_F10 */
const SC_KANA: u8 = 0x72; /* KEY_KANA */

/* テンキー (drivers/kbd.c の scancode_to_ascii 0x40〜0x50)。 */
const NP_MINUS: u8 = 0x40;
const NP_SLASH: u8 = 0x41;
const NP_7: u8 = 0x42;
const NP_8: u8 = 0x43;
const NP_9: u8 = 0x44;
const NP_STAR: u8 = 0x45;
const NP_4: u8 = 0x46;
const NP_5: u8 = 0x47;
const NP_6: u8 = 0x48;
const NP_PLUS: u8 = 0x49;
const NP_1: u8 = 0x4A;
const NP_2: u8 = 0x4B;
const NP_3: u8 = 0x4C;
const NP_0: u8 = 0x4E;
const NP_DOT: u8 = 0x50;

const MOD_SHIFT: u32 = 0x01; /* SHIFT_SHIFT */
const MOD_KANA: u32 = 0x04; /* SHIFT_KANA */
const MOD_GRPH: u32 = 0x08; /* SHIFT_GRPH */
const MOD_CTRL: u32 = 0x10; /* SHIFT_CTRL */

/* drivers/mouse.h の MOUSE_BTN_*。 */
const BTN_LEFT: u8 = 0x01;
const BTN_RIGHT: u8 = 0x02;

/* ================================================================ */
/*  定数 (票 §1-2 / §1-3。数値はここ 1 か所)                          */
/* ================================================================ */

/// マウスキーの加速: 連続した make の回数 (break で数え直し) → 1 回の移動量。
/// (回数の上限, 移動量) を小さい順に。**時刻ではなく回数**で決める
/// (ホスト試験で再現できる、票 §1-3)。1〜7 回目 1 ドット、8〜15 回目 4 ドット、
/// 16 回目以降 8 ドット。
const MK_ACCEL: [(u32, i32); 3] = [(7, 1), (15, 4), (u32::MAX, 8)];

/// CTRL を押している間の移動量の割合 (分子 / 分母)。**半分の速度**
/// (ユーザー指示 2026-09-26)。1 ドットの半分は丸めると 0 になるので、端数を
/// 分母単位で軸ごとに持ち越す (`KbdNav::mk_acc_*`、マウスキーが切れたら捨てる)。
const MK_SLOW_NUM: i32 = 1;
const MK_SLOW_DEN: i32 = 2;

/// キーボードでの移動・サイズの 1 打の量 (矢印 / SHIFT+矢印)。
const KMOVE_STEP: i32 = 8;
const KMOVE_STEP_FINE: i32 = 1;

/// 窓メニューの項目 (`startmenu` の行と 1 対 1)。
pub const WM_RESTORE: usize = 0;
pub const WM_MOVE: usize = 1;
pub const WM_SIZE: usize = 2;
pub const WM_MINIMIZE: usize = 3;
pub const WM_MAXIMIZE: usize = 4;
pub const WM_CLOSE: usize = 5;
pub const WM_ITEMS: usize = 6;

/* キーボードの移動・サイズのモード。 */
const KM_NONE: u8 = 0;
const KM_MOVE: u8 = 1;
const KM_SIZE: u8 = 2;

/* ================================================================ */
/*  状態 (GuiState.kn)                                               */
/* ================================================================ */

pub struct KbdNav {
    /// マウスキー (カナ) がオンか。
    pub mk_on: bool,
    /// 移動キーの連続 make 回数 (加速用)。
    mk_count: u32,
    /// 移動の端数 (1/`MK_SLOW_DEN` ドット単位、軸ごと)。CTRL の半分速度で出る。
    mk_acc_x: i32,
    mk_acc_y: i32,
    /// WM が make を消費したキー (scan ごとの印)。対になる break も WM が消費する
    /// (モードが途中で切れても、票 §1-5)。
    consumed: [u8; 16],
    /// MRU (最後にフォーカスを持った順、先頭が最新)。完全な WindowId。
    mru: [u32; GUI_MAX_WINDOWS],
    mru_n: usize,
    /// GRPH+TAB の切り替え中 (GRPH を押している間)。
    sw_active: bool,
    sw_ids: [u32; GUI_MAX_WINDOWS],
    sw_n: usize,
    sw_sel: usize,
    /// キーボードの移動・サイズ (窓メニュー)。
    kmode: u8,
    kidx: usize,
}

impl KbdNav {
    pub const NEW: KbdNav = KbdNav {
        mk_on: false,
        mk_count: 0,
        mk_acc_x: 0,
        mk_acc_y: 0,
        consumed: [0; 16],
        mru: [0; GUI_MAX_WINDOWS],
        mru_n: 0,
        sw_active: false,
        sw_ids: [0; GUI_MAX_WINDOWS],
        sw_n: 0,
        sw_sel: 0,
        kmode: KM_NONE,
        kidx: 0,
    };
}

#[inline]
fn is_consumed(st: &GuiState, scan: u8) -> bool {
    let s = (scan & 0x7F) as usize;
    (st.kn.consumed[s >> 3] & (1 << (s & 7))) != 0
}
#[inline]
fn set_consumed(st: &mut GuiState, scan: u8, on: bool) {
    let s = (scan & 0x7F) as usize;
    if on {
        st.kn.consumed[s >> 3] |= 1 << (s & 7);
    } else {
        st.kn.consumed[s >> 3] &= !(1 << (s & 7));
    }
}

/// 起動時の初期化: カナをロックしたまま GUI に入った場合に合わせる (票 §1-5)。
/// **現在値を読むのはここだけ**。
pub fn init(st: &mut GuiState) {
    let m = unsafe { (os32api::api().kbd_get_modifiers)() };
    st.kn.mk_on = (m & MOD_KANA) != 0;
}

/* ================================================================ */
/*  MRU                                                              */
/* ================================================================ */

/// 窓がフォーカスを得た (先頭へ)。消えた窓の印もここで詰める。
pub fn mru_touch(st: &mut GuiState, id: u32) {
    if id == 0 {
        return;
    }
    let mut out = [0u32; GUI_MAX_WINDOWS];
    out[0] = id;
    let mut n = 1;
    let mut i = 0;
    while i < st.kn.mru_n {
        let x = st.kn.mru[i];
        if x != id && st.win_by_id(x).is_some() && n < GUI_MAX_WINDOWS {
            out[n] = x;
            n += 1;
        }
        i += 1;
    }
    st.kn.mru = out;
    st.kn.mru_n = n;
}

/// 窓を MRU の末尾へ (最小化 / GRPH+ESC)。
pub fn mru_to_end(st: &mut GuiState, id: u32) {
    let mut out = [0u32; GUI_MAX_WINDOWS];
    let mut n = 0;
    let mut i = 0;
    while i < st.kn.mru_n {
        let x = st.kn.mru[i];
        if x != id && st.win_by_id(x).is_some() && n < GUI_MAX_WINDOWS - 1 {
            out[n] = x;
            n += 1;
        }
        i += 1;
    }
    out[n] = id;
    n += 1;
    st.kn.mru = out;
    st.kn.mru_n = n;
}

/// 切り替えの対象になる窓か (見えているか最小化)。
#[inline]
fn switchable(st: &GuiState, id: u32) -> bool {
    match st.win_by_id(id) {
        Some(i) => st.windows[i].visible || st.windows[i].minimized,
        None => false,
    }
}

/// 切り替えの並び: 最前面 → MRU → (MRU に居ない窓を Z の前から)。
fn build_switch_list(st: &GuiState, out: &mut [u32; GUI_MAX_WINDOWS]) -> usize {
    let mut n = 0;
    let push = |out: &mut [u32; GUI_MAX_WINDOWS], n: &mut usize, id: u32| {
        if id == 0 || !switchable(st, id) || *n >= GUI_MAX_WINDOWS {
            return;
        }
        let mut k = 0;
        while k < *n {
            if out[k] == id {
                return;
            }
            k += 1;
        }
        out[*n] = id;
        *n += 1;
    };
    push(out, &mut n, st.front_id());
    let mut i = 0;
    while i < st.kn.mru_n {
        push(out, &mut n, st.kn.mru[i]);
        i += 1;
    }
    let mut z = st.z_count;
    while z > 0 {
        z -= 1;
        let idx = st.zorder[z];
        push(out, &mut n, st.windows[idx].id(idx));
    }
    n
}

/// 切り替え中に選んでいる窓 (タスクバーの押し込み表示、票 §1-2)。
pub fn switch_selection(st: &GuiState) -> Option<u32> {
    if st.kn.sw_active && st.kn.sw_sel < st.kn.sw_n {
        Some(st.kn.sw_ids[st.kn.sw_sel])
    } else {
        None
    }
}

/* ================================================================ */
/*  WM のショートカットの分類 (票 §1-2)                               */
/* ================================================================ */

#[derive(Clone, Copy, PartialEq, Eq)]
enum Shortcut {
    StartMenu,
    Switch,
    SendBack,
    Close,
    WinMenu,
    Context,
}

fn shortcut(scan: u8, mods: u32) -> Option<Shortcut> {
    let shift = (mods & MOD_SHIFT) != 0;
    let grph = (mods & MOD_GRPH) != 0;
    let ctrl = (mods & MOD_CTRL) != 0;
    if scan == SC_ESC && ctrl && !grph {
        return Some(Shortcut::StartMenu);
    }
    if grph && !ctrl {
        match scan {
            SC_TAB => return Some(Shortcut::Switch),
            SC_ESC => return Some(Shortcut::SendBack),
            SC_F4 => return Some(Shortcut::Close),
            /* SHIFT+GRPH+SPACE は FEP の切り替え (SHIFT+SPACE の規則が先)。 */
            SC_SPACE if !shift => return Some(Shortcut::WinMenu),
            _ => {}
        }
    }
    if scan == SC_F10 && shift && !grph && !ctrl {
        return Some(Shortcut::Context);
    }
    None
}

/// SHIFT+f･10 を WM が拾うか (窓が無いときだけ。全画面 / モーダル中はアプリ・
/// ダイアログへ。メニュー表示中と移動中は「無視」= WM が消費)。
fn context_is_wm(st: &GuiState) -> bool {
    if fullscreen::active() || modal::is_open() {
        return false;
    }
    startmenu::is_open() || st.drag_index >= 0 || st.front_index().is_none()
}

/// マウスキーが使うテンキーか (= と , は使わない、票 §1-3)。
#[inline]
fn is_mk_pad(scan: u8) -> bool {
    matches!(
        scan,
        NP_MINUS
            | NP_SLASH
            | NP_7
            | NP_8
            | NP_9
            | NP_STAR
            | NP_4
            | NP_5
            | NP_6
            | NP_PLUS
            | NP_1
            | NP_2
            | NP_3
            | NP_0
            | NP_DOT
    )
}

/// キーボードの移動・サイズが続いているか (窓が消えたら自然に終わる —
/// `destroy_window` が `drag_index` を落とす)。
#[inline]
fn kmove_active(st: &GuiState) -> bool {
    st.kn.kmode != KM_NONE && st.drag_index == st.kn.kidx as i32
}

/// キーボードの移動・サイズ中か (マウスの枠追従・エッジを止める)。
#[inline]
pub fn kmove_busy(st: &GuiState) -> bool {
    kmove_active(st)
}

/// ドラッグ中の窓が消えた (`wm::drop_drag_frame`): キーボードの移動・サイズの
/// 状態を捨てる。枠は呼び手が消す。
#[inline]
pub fn drop_kmove(st: &mut GuiState) {
    st.kn.kmode = KM_NONE;
}

/* ================================================================ */
/*  X4 の判定 (票 §1-4)                                              */
/* ================================================================ */

/// この raw は WM が X3 で処理するキーか。X4 はこれが真なら、その raw と以後を
/// 全部退避する。[`pre`] / [`on_key`] と**同じ判定**をする (ずれると二重配送か
/// 取りこぼしになる)。
pub fn is_wm_raw(st: &GuiState, raw: i32) -> bool {
    let scan = (raw & 0x7F) as u8;
    let down = ((raw >> 8) & 1) != 0;
    let mods = ((raw >> 9) & 0x7F) as u32;
    if scan == SC_KANA {
        return true;
    }
    /* 切り替え中に GRPH が離れた (GRPH の break そのもの、または取りこぼし)。 */
    if st.kn.sw_active && (mods & MOD_GRPH) == 0 {
        return true;
    }
    if kmove_active(st) {
        return true; /* 移動・サイズ中は make も break も WM */
    }
    if !down {
        return is_consumed(st, scan);
    }
    if st.kn.sw_active && (scan == SC_TAB || scan == SC_ESC) {
        return true;
    }
    if let Some(k) = shortcut(scan, mods) {
        return k != Shortcut::Context || context_is_wm(st);
    }
    st.kn.mk_on && !fullscreen::active() && is_mk_pad(scan)
}

/* ================================================================ */
/*  X3: 修飾キーを捨てる前 (票 §1-2 GRPH の break / §1-5 カナ)        */
/* ================================================================ */

/// X4 がアプリへ配った make の印を消す (代行レビュー P3-3: break を取りこぼした
/// 後に印が残ると、X4 で配った make の break を X3 が握り潰していた)。
pub fn pump_delivered_make(st: &mut GuiState, scan: u8) {
    set_consumed(st, scan, false);
}

/// 修飾キーとして捨てる**前**に見る。消費したら true (カナの raw)。
/// GRPH が離れたら (break そのもの、または GRPH の無い raw = 取りこぼし)
/// 切り替えを確定する — その raw 自体は呼び出し側が続けて処理する。
pub fn pre(st: &mut GuiState, scan: u8, down: bool, mods: u32) -> bool {
    if down {
        /* 新しい make は古い印を消す (印が残って無関係な break を食わない)。 */
        set_consumed(st, scan, false);
    }
    if st.kn.sw_active && (mods & MOD_GRPH) == 0 {
        finish_switch(st);
    }
    if scan == SC_KANA {
        set_mousekeys(st, (mods & MOD_KANA) != 0);
        return true;
    }
    false
}

/// マウスキーのオン / オフ。オフになったら数えを戻し、押したままの合成ボタンを
/// その場で離す (ドラッグは落とした位置で終わる、票 §1-5)。
fn set_mousekeys(st: &mut GuiState, on: bool) {
    if st.kn.mk_on == on {
        return;
    }
    st.kn.mk_on = on;
    st.kn.mk_count = 0;
    st.kn.mk_acc_x = 0; /* 端数はモードが切れたら捨てる */
    st.kn.mk_acc_y = 0;
    let tb = taskbar::rect(st);
    st.dirty_screen(tb);
    if !on {
        release_held(st);
    }
}

/* ================================================================ */
/*  X3: キー本体                                                     */
/* ================================================================ */

/// WM のキーなら処理して true (アプリへ配らない)。SHIFT+SPACE (FEP) と
/// CTRL+STOP の後、モーダル / メニュー / FEP / アプリより前に呼ぶ。
pub fn on_key(st: &mut GuiState, scan: u8, down: bool, mods: u32) -> bool {
    if !down {
        /* 移動・サイズ中は離しも WM が取る (始めた RETURN の離しがアプリへ漏れない)。 */
        if kmove_active(st) {
            set_consumed(st, scan, false);
            return true;
        }
        if is_consumed(st, scan) {
            set_consumed(st, scan, false);
            if is_mk_pad(scan) {
                st.kn.mk_count = 0; /* break で数え直し */
            }
            return true;
        }
        return false;
    }
    if st.kn.kmode != KM_NONE && !kmove_active(st) {
        /* 念のため (窓が消えたときは wm::drop_drag_frame が drop_kmove で戻す) */
        st.kn.kmode = KM_NONE;
    }
    let c = make(st, scan, mods);
    if c {
        set_consumed(st, scan, true);
    }
    c
}

fn make(st: &mut GuiState, scan: u8, mods: u32) -> bool {
    /* 移動・サイズ中はキーを全部 WM が取る (票 §1-2 の表の右端)。 */
    if kmove_active(st) {
        kmove_key(st, scan, mods);
        return true;
    }
    /* 切り替え中にモーダルが開いた: GRPH↑ では切り替えない (finish_switch) ので、
     * 以後の TAB で選択を進めず (タスクバーの押し込みを動かさず) ここで取り消す。
     * TAB 自体は下の run_shortcut がモーダル中の「無視」として消費する
     * (代行レビュー P3-B) */
    if st.kn.sw_active && modal::is_open() {
        cancel_switch(st);
    }
    if st.kn.sw_active {
        if scan == SC_TAB {
            advance_switch(st, (mods & MOD_SHIFT) != 0);
            return true;
        }
        if scan == SC_ESC {
            cancel_switch(st);
            return true;
        }
    }
    if let Some(k) = shortcut(scan, mods) {
        return run_shortcut(st, k, (mods & MOD_SHIFT) != 0);
    }
    if st.kn.mk_on && !fullscreen::active() && is_mk_pad(scan) {
        mousekey(st, scan, mods);
        return true;
    }
    false
}

/// 票 §1-2 の「状態ごとの扱い」の表。消費したら true (「無視」も消費)。
fn run_shortcut(st: &mut GuiState, k: Shortcut, shift: bool) -> bool {
    if k == Shortcut::Context {
        if !context_is_wm(st) {
            return false; /* 窓 / ダイアログ / 全画面のアプリへ */
        }
        if !startmenu::is_open() && st.drag_index < 0 {
            let (x, y) = (st.mouse_x, st.mouse_y);
            startmenu::open_context(st, x, y);
        }
        return true;
    }
    if fullscreen::active() || st.drag_index >= 0 {
        return true; /* 無視 */
    }
    if modal::is_open() {
        if k == Shortcut::Close {
            /* Win98 と同じくダイアログを閉じる (= 取消)。 */
            modal::on_key(st, SC_ESC, 0x1B, 0);
        }
        return true;
    }
    if startmenu::is_open() {
        match k {
            Shortcut::StartMenu | Shortcut::Close => {
                startmenu::close(st);
                return true;
            }
            Shortcut::Switch | Shortcut::SendBack => startmenu::close(st),
            _ => return true,
        }
    }
    match k {
        Shortcut::StartMenu => startmenu::toggle_start(st),
        Shortcut::Switch => begin_switch(st, shift),
        Shortcut::SendBack => send_back(st),
        Shortcut::Close => close_front(st),
        Shortcut::WinMenu => open_winmenu(st),
        Shortcut::Context => {}
    }
    true
}

/* ---- GRPH+TAB ---- */

/// 最初の TAB: その時点の MRU 順を写し、選択を 1 つ先 (SHIFT で 1 つ前) に置く。
/// 前面の窓が無い (全部最小化) ときは並びの先頭 (SHIFT で末尾) から。
fn begin_switch(st: &mut GuiState, back: bool) {
    let mut ids = [0u32; GUI_MAX_WINDOWS];
    let n = build_switch_list(st, &mut ids);
    if n == 0 {
        return;
    }
    st.kn.sw_ids = ids;
    st.kn.sw_n = n;
    st.kn.sw_active = true;
    st.kn.sw_sel = 0;
    if st.front_index().is_some() {
        advance_switch(st, back);
    } else if back {
        st.kn.sw_sel = n - 1;
    }
    dirty_taskbar(st);
}

/// 選択を 1 つ進める (`back` = SHIFT で 1 つ前)。
fn advance_switch(st: &mut GuiState, back: bool) {
    let n = st.kn.sw_n;
    if n == 0 {
        return;
    }
    st.kn.sw_sel = if back {
        (st.kn.sw_sel + n - 1) % n
    } else {
        (st.kn.sw_sel + 1) % n
    };
    dirty_taskbar(st);
}

/// GRPH を離した: 選んだ窓へ 1 回だけ切り替える (最小化なら元に戻す)。
fn finish_switch(st: &mut GuiState) {
    /* 途中でモーダルが開いたら切り替えない (代行レビュー P3-2)。 */
    if modal::is_open() {
        cancel_switch(st);
        return;
    }
    let id = match switch_selection(st) {
        Some(id) => id,
        None => 0,
    };
    st.kn.sw_active = false;
    st.kn.sw_n = 0;
    dirty_taskbar(st);
    if id == 0 || !switchable(st, id) {
        return;
    }
    if let Some(i) = st.win_by_id(id) {
        wm::activate_index(st, i);
    }
}

fn cancel_switch(st: &mut GuiState) {
    st.kn.sw_active = false;
    st.kn.sw_n = 0;
    dirty_taskbar(st);
}

fn dirty_taskbar(st: &mut GuiState) {
    let r = taskbar::rect(st);
    st.dirty_screen(r);
}

/* ---- GRPH+ESC ---- */

/// 最前面の窓を最背面・MRU の末尾へ回し、次の (見えている) 窓へ。
/// 最小化した窓は Z 順に見えないので自然に飛ばす (**Win98 との差**、票 §1-2)。
fn send_back(st: &mut GuiState) {
    let f = match st.front_index() {
        Some(f) => f,
        None => return,
    };
    let mut visible_n = 0;
    let mut i = 0;
    while i < st.z_count {
        let w = &st.windows[st.zorder[i]];
        if w.used && w.visible {
            visible_n += 1;
        }
        i += 1;
    }
    if visible_n < 2 {
        return;
    }
    wm::focus_leaving(st, true);
    let old = st.front_id();
    st.send_to_back(f);
    crate::visible::recompute_and_expose(st);
    let outer = st.windows[f].outer();
    st.dirty_screen(outer);
    mru_to_end(st, old);
    let new = st.front_id();
    if let Some(ni) = st.front_index() {
        let o = st.windows[ni].outer();
        st.dirty_screen(o);
    }
    input::emit_focus_change(st, old, new);
}

/* ---- GRPH+f･4 ---- */

fn close_front(st: &mut GuiState) {
    let idx = match st.front_index() {
        Some(i) => i,
        None => {
            /* 窓が無ければ Shut Down の確認 (Win98 と同じ)。 */
            startmenu::confirm_halt(st);
            return;
        }
    };
    request_close(st, idx);
}

/// 窓へ閉じる要求 (`Close`) を送る (閉じるボタンと同じ)。
fn request_close(st: &mut GuiState, idx: usize) {
    let w = st.windows[idx];
    if !w.has_close() {
        return;
    }
    if let Some(slot) = st.slot_of_owner(w.owner) {
        let ev = ring::ev_simple(GUI_EV_CLOSE, 0, w.id(idx));
        ring::append(st, slot, &ev);
    }
}

/* ---- GRPH+SPACE (窓メニュー) ---- */

fn open_winmenu(st: &mut GuiState) {
    let idx = match st.front_index() {
        Some(i) => i,
        None => return,
    };
    let w = st.windows[idx];
    let (x, y) = (w.x + wm::BORDER_W, w.y + wm::BORDER_W + wm::TITLEBAR_H);
    startmenu::open_winmenu(st, w.id(idx), x, y);
}

/// 窓メニューの項目が使えるか (使えない項目は灰色、選んでも何もしない)。
pub fn winmenu_enabled(st: &GuiState, id: u32, item: usize) -> bool {
    let idx = match st.win_by_id(id) {
        Some(i) => i,
        None => return false,
    };
    let w = &st.windows[idx];
    match item {
        WM_RESTORE => w.maximized || w.minimized,
        WM_MOVE | WM_SIZE | WM_MAXIMIZE => w.movable() && !w.maximized,
        WM_MINIMIZE => !w.minimized,
        WM_CLOSE => w.has_close(),
        _ => false,
    }
}

/// 窓メニューの項目を実行する (メニューは閉じた後)。
pub fn winmenu_run(st: &mut GuiState, id: u32, item: usize) {
    if !winmenu_enabled(st, id, item) {
        return;
    }
    let idx = match st.win_by_id(id) {
        Some(i) => i,
        None => return,
    };
    match item {
        WM_RESTORE => wm::restore_size(st, idx),
        WM_MOVE => start_kmove(st, idx, KM_MOVE),
        WM_SIZE => start_kmove(st, idx, KM_SIZE),
        WM_MINIMIZE => wm::minimize(st, idx),
        WM_MAXIMIZE => wm::maximize(st, idx),
        WM_CLOSE => request_close(st, idx),
        _ => {}
    }
}

/* ---- 移動・サイズ (矢印 8 ドット / SHIFT+矢印 1 ドット / RETURN / ESC) ---- */

fn start_kmove(st: &mut GuiState, idx: usize, mode: u8) {
    if st.drag_index >= 0 {
        return;
    }
    st.kn.kmode = mode;
    st.kn.kidx = idx;
    input::begin_frame(st, idx);
}

fn kmove_key(st: &mut GuiState, scan: u8, mods: u32) {
    let step = if (mods & MOD_SHIFT) != 0 {
        KMOVE_STEP_FINE
    } else {
        KMOVE_STEP
    };
    let (dx, dy) = match scan {
        SC_UP => (0, -step),
        SC_DOWN => (0, step),
        SC_LEFT => (-step, 0),
        SC_RIGHT => (step, 0),
        SC_RETURN => {
            finish_kmove(st, true);
            return;
        }
        SC_ESC => {
            finish_kmove(st, false);
            return;
        }
        _ => return, /* 他のキーは無視 (消費) */
    };
    let f = st.drag_frame;
    let wa = wm::work_area(st);
    let nf = if st.kn.kmode == KM_MOVE {
        let (nx, ny) = wm::clamp_to_work_area(st, f.x + dx, f.y + dy, f.w, f.h);
        Rect::new(nx, ny, f.w, f.h)
    } else {
        let (mw, mh) = wm::min_size(&st.windows[st.kn.kidx]);
        let mut w = f.w + dx;
        let mut h = f.h + dy;
        if w > wa.right() - f.x {
            w = wa.right() - f.x;
        }
        if h > wa.bottom() - f.y {
            h = wa.bottom() - f.y;
        }
        if w < mw {
            w = mw;
        }
        if h < mh {
            h = mh;
        }
        Rect::new(f.x, f.y, w, h)
    };
    input::redraw_frame(st, nf);
}

/// RETURN (`apply`) で枠の位置・大きさへ窓を移す。ESC は元のまま。
fn finish_kmove(st: &mut GuiState, apply: bool) {
    let idx = st.kn.kidx;
    let frame = st.drag_frame;
    st.kn.kmode = KM_NONE;
    st.drag_index = -1;
    st.drag_frame = Rect::EMPTY;
    input::erase_frame(st, frame);
    if apply {
        wm::set_outer(st, idx, frame);
    }
}

/* ---- マウスキー ---- */

/// 連続 make の回数から 1 回の移動量。
fn mk_step(count: u32) -> i32 {
    let mut i = 0;
    while i < MK_ACCEL.len() {
        if count <= MK_ACCEL[i].0 {
            return MK_ACCEL[i].1;
        }
        i += 1;
    }
    MK_ACCEL[MK_ACCEL.len() - 1].1
}

/// 1 軸ぶんの移動: `units` (= 方向 × 加速の量) を端数 `acc` に足して、
/// 整数ドットになった分だけ返す。CTRL なしは分母倍して足すので端数は増えない。
fn mk_axis(acc: &mut i32, units: i32, slow: bool) -> i32 {
    *acc += if slow { units * MK_SLOW_NUM } else { units * MK_SLOW_DEN };
    let d = *acc / MK_SLOW_DEN; /* 0 方向への切り捨て (端数の符号は残る) */
    *acc -= d * MK_SLOW_DEN;
    d
}

fn mousekey(st: &mut GuiState, scan: u8, mods: u32) {
    let dir = match scan {
        NP_8 => Some((0, -1)),
        NP_2 => Some((0, 1)),
        NP_4 => Some((-1, 0)),
        NP_6 => Some((1, 0)),
        NP_7 => Some((-1, -1)),
        NP_9 => Some((1, -1)),
        NP_1 => Some((-1, 1)),
        NP_3 => Some((1, 1)),
        _ => None,
    };
    if let Some((ux, uy)) = dir {
        st.kn.mk_count = st.kn.mk_count.saturating_add(1);
        let s = mk_step(st.kn.mk_count);
        /* CTRL は raw に焼き込まれた修飾で見る (現在値は読まない、票 §1-5)。 */
        let slow = (mods & MOD_CTRL) != 0;
        let dx = mk_axis(&mut st.kn.mk_acc_x, ux * s, slow);
        let dy = mk_axis(&mut st.kn.mk_acc_y, uy * s, slow);
        let mut nx = st.mouse_x + dx;
        let mut ny = st.mouse_y + dy;
        if nx < 0 {
            nx = 0;
        }
        if ny < 0 {
            ny = 0;
        }
        if nx > st.screen_w - 1 {
            nx = st.screen_w - 1;
        }
        if ny > st.screen_h - 1 {
            ny = st.screen_h - 1;
        }
        if nx != st.mouse_x || ny != st.mouse_y {
            input::move_x3(st, nx, ny);
        }
        return;
    }
    /* ボタンは**選ばずに直接**割り当てる (ユーザー決定 2026-09-26、手数を
     * 少なく — Win98 の「/ - * でボタンを選んでから 5」は持たない)。
     * / と * はマウスモード中は消費して何もしない (数字として窓へ渡すと、
     * ポインタを操作しているつもりの打鍵が文字として入る)。 */
    match scan {
        NP_5 => click(st, BTN_LEFT),
        NP_MINUS => click(st, BTN_RIGHT),
        NP_PLUS => {
            /* 同じ X3 周期の中で down・up・down・up (ダブルクリックの判定は
             * tick の差で見るので、同じ周期なら差は 0 で必ず入る)。 */
            click(st, BTN_LEFT);
            click(st, BTN_LEFT);
        }
        NP_0 => {
            if (st.synth_buttons & BTN_LEFT) == 0 {
                st.synth_buttons |= BTN_LEFT;
                let (x, y) = (st.mouse_x, st.mouse_y);
                input::edge_x3(st, x, y, BTN_LEFT, true);
            }
        }
        NP_DOT => release_held(st),
        _ => {} /* NP_SLASH / NP_STAR: 消費して何もしない */
    }
}

/// ボタン `b` で押下 → 離し (その場でエッジを順に処理する、票 §1-3)。
/// 押したまま (0) のボタンは押し直さない。
fn click(st: &mut GuiState, b: u8) {
    if (st.synth_buttons & b) != 0 {
        return;
    }
    let (x, y) = (st.mouse_x, st.mouse_y);
    st.synth_buttons |= b;
    input::edge_x3(st, x, y, b, true);
    st.synth_buttons &= !b;
    let (x, y) = (st.mouse_x, st.mouse_y);
    input::edge_x3(st, x, y, b, false);
}

/// 押したままの合成ボタンを離す。
fn release_held(st: &mut GuiState) {
    for b in [BTN_LEFT, BTN_RIGHT] {
        if (st.synth_buttons & b) != 0 {
            st.synth_buttons &= !b;
            let (x, y) = (st.mouse_x, st.mouse_y);
            input::edge_x3(st, x, y, b, false);
        }
    }
}

#[cfg(test)]
#[path = "../host/kbdnav_tests.rs"]
mod tests;
