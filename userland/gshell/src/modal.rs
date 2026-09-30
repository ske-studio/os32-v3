//! modal.rs — モーダルダイアログ (契約 U4、票 W2 の C / 票 W4 の完成版)。
//!
//! **入れ子ループを作らない**のが要点。`MODAL_OPEN` は「入力の宛先をダイアログに
//! 限定する」状態を立てるだけで、WM は X3 (`OP_WAIT`) の周期の中でダイアログを
//! 描き、キーとクリックを解釈する。親アプリは自分のループを回したまま `Paint` を
//! 受け続け、完了は `Modal{dialog, result}` イベント 1 件で受け取る。
//!
//! ダイアログの中身は **WM 自身の窓** なので、`Window` 表には載せず (アプリの
//! 所有物ではない)、契約 U8 の「直接呼び出し」で描く。可視領域の計算
//! ([`crate::visible`]) はダイアログの矩形を**上に載っている窓**として扱うので、
//! 下のアプリはダイアログの下を描かず、閉じたときに露出分の `Paint` を受ける。
//!
//! 標準ダイアログ:
//!
//! | `buttons` | 種類 | ボタン |
//! |---|---|---|
//! | [`GUI_MODAL_OK`] | メッセージ | OK |
//! | [`GUI_MODAL_OK_CANCEL`] | メッセージ | OK / Cancel |
//! | [`GUI_MODAL_YES_NO`] | メッセージ | Yes / No |
//! | [`GUI_MODAL_FILE_OPEN`] | ファイル選択 | Open / Cancel |
//! | [`GUI_MODAL_INPUT`] | 1 行入力 (v1.2 M4) | OK / Cancel |
//!
//! `result` は 1 = OK / Yes / Open、0 = Cancel / No / ESC。
//!
//! ## v1.2 (W4) で足したもの
//!
//! ### completed result (契約 V12-M の M2)
//!
//! ダイアログの**値** (ファイルの絶対パス / 入力文字列) はイベントリングでは
//! 運べない (`GuiEvent` のペイロードは 8B)。そこで GUI スロットごとに
//! [`Completed`] を 1 件だけ持ち、`GUI_OP_MODAL_RESULT` (op 65) で取りに来て
//! もらう。完了時の順序は W4 §1 のとおり:
//!
//! 1. [`store_completed`] で結果をスロットへ保存する (**リングとは独立**)。
//! 2. `modal_event_pending` (sticky) を立てる。
//! 3. [`deliver_pending`] でリングへ `GUI_EV_MODAL` を積む。
//! 4. 満杯なら pending を残し、`OP_POLL` の準備 ([`retry_pending`]) と
//!    毎 X3 ([`retry_pending_all`]) で空きができるまで再試行する。
//!    **`dropped` には数えない** (W4 §8: 制御イベントは捨てない)。
//!
//! 結果本体はリングが溢れても失われない。consume 後の再取得と ID 不一致は
//! `OS32_ERR_STALE`、未 consume の結果がある間の `MODAL_OPEN` は
//! `OS32_ERR_FULL` (規則の実装は [`has_completed`] / [`fill_resp`] /
//! [`consume_completed`])。
//!
//! ### Input dialog (M4)
//!
//! 1 行 edit + OK/Cancel。ANK / UTF-8 Text、BS / DEL、LEFT / RIGHT は
//! **UTF-8 コードポイント境界**で移動、HOME / END、SHIFT+SPACE で FEP。
//! FEP の未確定行 / 候補窓は [`fep_caret`] が返す field の caret 位置に出る
//! (`SET_TEXT_CURSOR` 相当の内部位置を modal 自身が [`crate::fep`] へ渡す)。
//! 最大 255B で、超える入力は**入れずに既存内容を保つ** (切り詰めない)。

#![allow(dead_code)]

use crate::wm::{self, GuiState, Rect};
use crate::{chrome, damage, fep, lease, ring, session, visible};
use os32api::gfx;
use os32api::gui::proto::{
    GuiString, GUI_COLOR_FACE, GUI_COLOR_HIGHLIGHT, GUI_COLOR_LIGHT, GUI_COLOR_SEL_BG,
    GUI_COLOR_SEL_TEXT, GUI_COLOR_SHADOW, GUI_COLOR_TEXT, GUI_COLOR_TITLE_ACTIVE,
    GUI_COLOR_TITLE_TEXT, GUI_COLOR_WINDOW, GUI_MAX_WINDOWS, GUI_SESSION_SHUTDOWN,
    GUI_SESSION_SWITCH_CUI, GUI_SLOT_MAX, OS32_ERR_FULL, OS32_ERR_INVAL, OS32_ERR_STALE,
};

/* ================================================================ */
/*  WM 自身が開いたダイアログの用途 (W3)                             */
/*                                                                  */
/*  WM owned のダイアログは completed result を作らない (アプリに宛先が */
/*  無い)。代わりに `finish` が用途ごとの後始末を行う。               */
/* ================================================================ */
/// v1.1 の F5: ファイル選択で選んだ `.bin` をそのまま起動する。
pub const WM_PURPOSE_FILE_LAUNCH: u8 = 0;
/// Start → Run...: 入力された絶対パスを `LAUNCH` にする (契約 D2)。
pub const WM_PURPOSE_RUN: u8 = 1;
/// Start → CUI mode の確認 (契約 S6)。Yes で初めて `SWITCH_CUI`。
pub const WM_PURPOSE_CONFIRM_CUI: u8 = 2;
/// Start → Shut Down の確認 (契約 S6 / S7)。Yes で初めて `SHUTDOWN`。
pub const WM_PURPOSE_CONFIRM_HALT: u8 = 3;
/// 通知だけ (起動失敗 / cfg 更新失敗)。押しても何も起きない。
pub const WM_PURPOSE_NOTIFY: u8 = 4;
/// Start → Settings... の設定ダイアログ (票 S4 §3)。OK は
/// [`crate::settings::on_ok`] へ渡すだけで、**DB には触らない**。
pub const WM_PURPOSE_SETTINGS: u8 = 5;
/// 設定レジストリの通知 (起動時 / 保存結果。票 S4 §2・§3)。
/// [`WM_PURPOSE_NOTIFY`] と同じく押しても何も起きないが、
/// `settings` の `notice` 経路から出たものだと分かるように別にしてある。
pub const WM_PURPOSE_CFG_NOTICE: u8 = 6;

/* ================================================================ */
/*  ダイアログ種別 / result (正典は sdk/include/os32/os32_gui_shared.h、  */
/*  その Rust 写しが os32api::gui::proto。ここでは再輸出だけする)         */
/* ================================================================ */
pub use os32api::gui::proto::{
    GUI_MODAL_FILE_OPEN, GUI_MODAL_INPUT, GUI_MODAL_OK, GUI_MODAL_OK_CANCEL,
    GUI_MODAL_RESULT_CANCEL as MODAL_RESULT_CANCEL, GUI_MODAL_RESULT_OK as MODAL_RESULT_OK,
    GUI_MODAL_YES_NO,
};

/* スキャンコード (drivers/kbd.h)。 */
const SC_ESC: u8 = 0x00;
const SC_BS: u8 = 0x0E;
const SC_TAB: u8 = 0x0F;
const SC_RETURN: u8 = 0x1C;
const SC_SPACE: u8 = 0x34;
const SC_ROLLUP: u8 = 0x36;
const SC_ROLLDOWN: u8 = 0x37;
const SC_DEL: u8 = 0x39;
const SC_UP: u8 = 0x3A;
const SC_LEFT: u8 = 0x3B;
const SC_RIGHT: u8 = 0x3C;
const SC_DOWN: u8 = 0x3D;
const SC_HOME: u8 = 0x3E;
/// PC-98 のキーボードに END は無い。HELP と SHIFT+HOME を END として扱う。
const SC_HELP: u8 = 0x3F;

/* 修飾ビット (drivers/kbd.h の SHIFT_*、input.rs の MOD_* と同値)。 */
const MOD_SHIFT: u32 = 0x01;

/* 見た目 (libos32filer の版面に合わせた寸法)。 */
const BTN_W: i32 = 72;
const BTN_H: i32 = 20;
const BTN_GAP: i32 = 12;
const PAD: i32 = 10;
const LINE_H: i32 = 18;
const TITLE_H: i32 = 18;
/// Input dialog の edit field の高さ。
const FIELD_H: i32 = 20;
/// Input dialog の最小幅 (画素)。
const FIELD_MIN_W: i32 = 320;

pub const MAX_ENTRIES: usize = 96;
pub const NAME_LEN: usize = 48;
pub const PATH_LEN: usize = 256;
const MSG_LEN: usize = 200;
/// ファイル選択の一覧に出す行数。
const LIST_ROWS: usize = 12;
/// `GuiString` に載せられる最大バイト数 (契約 M1)。**切り詰めない**。
pub const VALUE_MAX: usize = 255;
/// ダブルクリックとみなす間隔 (tick = 10ms)。
const DBLCLICK_TICKS: u32 = 40;

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
/*  completed result (契約 V12-M の M2、W4 §1)                       */
/* ================================================================ */

/// GUI スロット 1 本につき 1 件だけ持つ「完了したダイアログの結果」。
///
/// `used` (= 未 consume の結果がある) と `modal_event_pending` (= sticky な
/// `GUI_EV_MODAL` がまだリングへ入っていない) は**独立**に落ちる。consume が
/// 先に起きても pending は残り、空きができ次第 1 件だけ届く (W4 §8 の
/// 「古い通知として到着し得る」)。両方落ちたところで全体を捨てる。
#[derive(Clone, Copy)]
pub struct Completed {
    pub used: bool,
    /// このスロットを持っていた owner (スロット再利用の誤配送よけ)。
    pub owner: i32,
    pub dialog_id: u16,
    pub result: i16,
    pub value_len: usize,
    pub value: [u8; VALUE_MAX],
    pub modal_event_pending: bool,
    /// `GUI_EV_MODAL` の `window` に載せる親ウィンドウ。
    pub ev_window: u32,
}

impl Completed {
    pub const NEW: Completed = Completed {
        used: false,
        owner: 0,
        dialog_id: 0,
        result: 0,
        value_len: 0,
        value: [0; VALUE_MAX],
        modal_event_pending: false,
        ev_window: 0,
    };
}

struct CompletedCell(core::cell::UnsafeCell<[Completed; GUI_SLOT_MAX]>);
unsafe impl Sync for CompletedCell {}
static COMPLETED: CompletedCell =
    CompletedCell(core::cell::UnsafeCell::new([Completed::NEW; GUI_SLOT_MAX]));

#[inline]
fn completed(slot: usize) -> &'static mut Completed {
    unsafe { &mut (*COMPLETED.0.get())[slot] }
}

/// スロットに未 consume の結果があるか (契約 M2: ある間 `MODAL_OPEN` は
/// `OS32_ERR_FULL`)。
#[inline]
pub fn has_completed(slot: usize) -> bool {
    if slot >= GUI_SLOT_MAX {
        return false;
    }
    completed(slot).used
}

/// 結果と pending の両方が落ちたら枠ごと捨てる。
#[inline]
fn drop_if_idle(slot: usize) {
    let c = completed(slot);
    if !c.used && !c.modal_event_pending {
        *c = Completed::NEW;
    }
}

/// ダイアログ完了の (1)(2): 結果を保存し sticky pending を立てる。
fn store_completed(
    slot: usize,
    owner: i32,
    dialog: u16,
    result: i16,
    value: &[u8],
    value_len: usize,
    ev_window: u32,
) {
    let c = completed(slot);
    *c = Completed::NEW;
    c.used = true;
    c.owner = owner;
    c.dialog_id = dialog;
    c.result = result;
    let n = if value_len > VALUE_MAX { VALUE_MAX } else { value_len };
    let mut i = 0;
    while i < n {
        c.value[i] = value[i];
        i += 1;
    }
    c.value_len = n;
    c.modal_event_pending = true;
    c.ev_window = ev_window;
}

/// ダイアログ完了の (3)(4): sticky な `GUI_EV_MODAL` をリングへ積む。満杯なら
/// pending を残して戻る (**`dropped` には加算しない**。W4 §8)。
fn deliver_pending(st: &mut GuiState, slot: usize) {
    if slot >= GUI_SLOT_MAX {
        return;
    }
    let (pending, owner, win, dialog, result) = {
        let c = completed(slot);
        (c.modal_event_pending, c.owner, c.ev_window, c.dialog_id, c.result)
    };
    if !pending {
        return;
    }
    /* スロットが別の owner へ渡っていたら配らない (誤配送よけ)。 */
    if !st.slots[slot].used || st.slots[slot].owner != owner {
        return;
    }
    let ev = ring::ev_modal(win, dialog, result);
    if ring::append(st, slot, &ev) {
        completed(slot).modal_event_pending = false;
        drop_if_idle(slot);
    }
}

/// `OP_POLL` の返却準備で呼ぶ再試行 (W4 §1 の 4)。
#[inline]
pub fn retry_pending(st: &mut GuiState, slot: usize) {
    deliver_pending(st, slot);
}

/// X3 の周期ごとに呼ぶ再試行 (W4 §1 の 4)。
pub fn retry_pending_all(st: &mut GuiState) {
    let mut s = 0;
    while s < GUI_SLOT_MAX {
        deliver_pending(st, s);
        s += 1;
    }
}

/// X3 (`wm::wm_cycle` の Wait / Standalone) で 1 回だけ呼ぶ周期処理。
///
/// - 溜まっている VFS 走査 (`MODAL_OPEN` が立てた `reload_pending`) をここで実行する。
///   **X1 / X4 では走らせない** (契約 T8 / S8、W4 §5)。
/// - sticky な `GUI_EV_MODAL` の再配送 (W4 §1 の 4 / §8)。
pub fn x3_cycle(st: &mut GuiState) {
    if state().used && state().reload_pending {
        state().reload_pending = false;
        reload_dir();
        let r = state().rect;
        st.dirty_screen(r);
    }
    retry_pending_all(st);
}

/// `GUI_OP_MODAL_RESULT` の応答を**全部**書く (契約 M2: 書き切ってから consume)。
/// 戻り値 0 = 成功、`OS32_ERR_STALE` = ID 不一致 / 二重 consume。
pub fn fill_resp(slot: usize, dialog: u16, resp: &mut os32api::gui::proto::GuiRespModalResult) {
    resp.result = OS32_ERR_STALE as i16;
    resp.dialog = dialog;
    resp.value = GuiString { len: 0, s: [0; VALUE_MAX] };
    if slot >= GUI_SLOT_MAX {
        return;
    }
    let c = completed(slot);
    if !c.used || c.dialog_id != dialog {
        return;
    }
    resp.result = c.result;
    resp.dialog = c.dialog_id;
    resp.value.len = c.value_len as u8;
    let mut i = 0;
    while i < c.value_len {
        resp.value.s[i] = c.value[i];
        i += 1;
    }
}

/// 応答を書き切った後の consume (契約 M2)。以後の再取得は `OS32_ERR_STALE`。
pub fn consume_completed(slot: usize, dialog: u16) {
    if slot >= GUI_SLOT_MAX {
        return;
    }
    let c = completed(slot);
    if !c.used || c.dialog_id != dialog {
        return;
    }
    c.used = false;
    c.value_len = 0;
    drop_if_idle(slot);
}

/* ================================================================ */
/*  状態                                                             */
/* ================================================================ */
pub struct Modal {
    pub used: bool,
    /// DialogId (1 から。破棄で進む = 古い `Modal` を捨てられる)。**0 にしない**。
    pub id: u16,
    pub owner: i32,
    pub parent_win: u32,
    pub buttons: u16,
    pub rect: Rect,
    msg: [u8; MSG_LEN + 1],
    msg_len: usize,
    nbtn: usize,
    focus_btn: usize,
    /* ---- ファイル選択 ---- */
    names: [[u8; NAME_LEN]; MAX_ENTRIES],
    is_dir: [bool; MAX_ENTRIES],
    /// 名前が `NAME_LEN` に収まらず切り詰めた行。表示はするが**選べない**
    /// (契約 M1: 切り詰めた別値を返さない)。
    too_long: [bool; MAX_ENTRIES],
    nentries: usize,
    cursor: usize,
    scroll: usize,
    cwd: [u8; PATH_LEN],
    cwd_len: usize,
    /// 選ばれたフルパス (NUL 終端)。WM 自身が使う。
    pub sel: [u8; PATH_LEN],
    pub sel_len: usize,
    /// ダブルクリック判定 (行 index と直近の押下 tick)。
    last_click_row: i32,
    last_click_tick: u32,
    /// ディレクトリの読み直しが要る。**`sys_ls` は X3 でしか走らせない**
    /// (W4 §5 / 契約 S8)。`MODAL_OPEN` は X1 なのでフラグを立てるだけにし、
    /// 実際の走査は [`x3_cycle`] が行う。
    reload_pending: bool,
    /* ---- Input dialog (M4) ---- */
    text: [u8; VALUE_MAX + 1],
    text_len: usize,
    /// caret のバイト位置 (**常に UTF-8 コードポイント境界**)。
    caret: usize,
    /// 表示の左端バイト位置 (横スクロール。同じく境界)。
    view_off: usize,
    /// WM 自身が開いたダイアログ (アプリへ `Modal` を送らない)。
    wm_owned: bool,
    /// `wm_owned` のときの用途 (`WM_PURPOSE_*`)。W3 の Start / Run / 確認。
    wm_purpose: u8,
    /* ---- 設定ダイアログ (票 S4 §3) ---- */
    /// **編集中の値** (適用値でも再読込値でもない。票 §2 の 3 区別)。
    set_color: u8,
    set_clock: bool,
    /// 選択中の行 (0 = Desktop color、1 = Clock)。
    set_row: usize,
    /// 状態行 (開いた周回の `load()` から組んだ本文、NUL 終端)。
    /// **最大 3 行に分ける** (実装レビュー往復 1 の B3: 1 行に連結すると
    /// 78 文字 = 624px になり枠外へ描いてしまう)。
    set_status: [[u8; crate::settings::STATUS_LINE_CAP]; crate::settings::STATUS_LINES_MAX],
    set_status_len: [usize; crate::settings::STATUS_LINES_MAX],
    /// 実際に使っている状態行の本数 (高さと幅をここから出す)。
    set_status_n: usize,
}

impl Modal {
    pub const NEW: Modal = Modal {
        used: false,
        id: 0,
        owner: 0,
        parent_win: 0,
        buttons: 0,
        rect: Rect::EMPTY,
        msg: [0; MSG_LEN + 1],
        msg_len: 0,
        /* 0 初期化のまま (= 全体が .bss)。開くときに 1 か 2 を入れる。 */
        nbtn: 0,
        focus_btn: 0,
        names: [[0; NAME_LEN]; MAX_ENTRIES],
        is_dir: [false; MAX_ENTRIES],
        too_long: [false; MAX_ENTRIES],
        nentries: 0,
        cursor: 0,
        scroll: 0,
        cwd: [0; PATH_LEN],
        cwd_len: 0,
        sel: [0; PATH_LEN],
        sel_len: 0,
        last_click_row: -1,
        last_click_tick: 0,
        reload_pending: false,
        text: [0; VALUE_MAX + 1],
        text_len: 0,
        caret: 0,
        view_off: 0,
        wm_owned: false,
        wm_purpose: WM_PURPOSE_FILE_LAUNCH,
        set_color: 0,
        set_clock: true,
        set_row: 0,
        set_status: [[0; crate::settings::STATUS_LINE_CAP]; crate::settings::STATUS_LINES_MAX],
        set_status_len: [0; crate::settings::STATUS_LINES_MAX],
        set_status_n: 0,
    };
}

struct ModalCell(core::cell::UnsafeCell<Modal>);
unsafe impl Sync for ModalCell {}
static MODAL: ModalCell = ModalCell(core::cell::UnsafeCell::new(Modal::NEW));

#[inline]
pub fn state() -> &'static mut Modal {
    unsafe { &mut *MODAL.0.get() }
}

#[inline]
pub fn is_open() -> bool {
    state().used
}

/// ホスト TDD の初期化 (`mocks::init`)。モーダルは**プロセスに 1 枚**の
/// `static` なので、前の試験が開けたまま終わると次の試験の合成に写り込む。
pub fn reset() {
    *state() = Modal::NEW;
    let mut s = 0;
    while s < GUI_SLOT_MAX {
        *completed(s) = Completed::NEW;
        s += 1;
    }
}

/// いま開いているのが Input dialog か (input.rs が FEP へ通すかの判定)。
#[inline]
pub fn is_input() -> bool {
    let m = state();
    m.used && m.buttons == GUI_MODAL_INPUT
}

/// いま開いているのが設定ダイアログか (票 S4 §3)。
#[inline]
pub fn is_settings() -> bool {
    let m = state();
    m.used && m.wm_owned && m.wm_purpose == WM_PURPOSE_SETTINGS
}

/// いま出ているダイアログの本文 (NUL を含まない)。試験の観測点。
pub fn msg_bytes() -> &'static [u8] {
    let m = state();
    &m.msg[..m.msg_len]
}

/// 設定ダイアログの状態行の本数 (試験の観測点。実装レビュー往復 1 の B3)。
pub fn settings_status_lines() -> usize {
    state().set_status_n
}

/// 焦点表示が当たっているボタンの index (0 = 1 本目 = OK / Yes)。試験の観測点
/// — 「RETURN が何をするか」と「どのボタンが太枠で描かれるか」が食い違って
/// いないことを見るのに使う (実装レビュー往復 2 の non-blocker)。
#[inline]
pub fn focus_btn() -> usize {
    state().focus_btn
}

/// 設定ダイアログの当たり判定 (行 0 / 行 1 / OK / Cancel、画面座標)。
/// マウス操作の試験と、PM の台本が座標を出すための窓口 (票 S4 §5 の (15))。
pub fn settings_hit_rects() -> (Rect, Rect, Rect, Rect) {
    let m = state();
    (
        set_row_rect(m, 0),
        set_row_rect(m, 1),
        button_rect(m, 0),
        button_rect(m, 1),
    )
}

/// 設定ダイアログの編集値 (試験と `finish_wm` から見る)。閉じていても
/// **最後の値を持っている** — `finish` は `used` を落とすだけで消さないため。
#[inline]
pub fn settings_values() -> (u8, bool, usize) {
    let m = state();
    (m.set_color, m.set_clock, m.set_row)
}

/// ダイアログの外形 (画面座標)。閉じていれば空。
#[inline]
pub fn rect() -> Rect {
    let m = state();
    if m.used {
        m.rect
    } else {
        Rect::EMPTY
    }
}

/* ================================================================ */
/*  開く / 閉じる                                                    */
/* ================================================================ */

/// 作業領域 = 画面 − taskbar (契約 D1)。ダイアログはこの中央に置く (W4 §7)。
#[inline]
fn work_area(st: &GuiState) -> Rect {
    wm::work_area(st)
}

/// 次の DialogId。**0 を使わない** (W4 §2)。
fn next_dialog_id() -> u16 {
    let n = state().id.wrapping_add(1) & 0x7FFF;
    if n == 0 {
        1
    } else {
        n
    }
}

/// `MODAL_OPEN` (X1)。**描かない** — 矩形を決めて損傷に積むだけ。
///
/// 規則 (W4 §2): 呼び出し元スロットに未 consume の結果があれば `OS32_ERR_FULL`、
/// 既に active modal があれば `OS32_ERR_FULL`、種別が範囲外なら `OS32_ERR_INVAL`。
/// 親ウィンドウの検証は handler 側 (`op_modal_open`) で行う。
pub fn open(
    st: &mut GuiState,
    slot: usize,
    owner: i32,
    parent_win: u32,
    buttons: u16,
    msg: &[u8],
    msg_len: usize,
) -> i32 {
    /* M2: 未 consume の結果があるスロットは次のダイアログを開けない。 */
    if has_completed(slot) {
        return OS32_ERR_FULL;
    }
    if state().used {
        return OS32_ERR_FULL; /* WM 全体で active modal は 1 枚 */
    }
    if buttons > GUI_MODAL_INPUT {
        return OS32_ERR_INVAL;
    }
    let next_id = next_dialog_id();
    {
        let m = state();
        *m = Modal::NEW;
        m.used = true;
        m.id = next_id;
        m.owner = owner;
        m.parent_win = parent_win;
        m.buttons = buttons;
        m.wm_owned = false;
        let n = if msg_len > MSG_LEN { MSG_LEN } else { msg_len };
        let mut i = 0;
        while i < n {
            m.msg[i] = msg[i];
            i += 1;
        }
        m.msg[n] = 0;
        m.msg_len = n;
        m.nbtn = if buttons == GUI_MODAL_OK { 1 } else { 2 };
        m.focus_btn = 0;
    }
    if buttons == GUI_MODAL_FILE_OPEN {
        /* start directory は v1.2 では "/" のまま (W4 §5)。走査は X3 で。 */
        set_cwd(b"/");
        state().reload_pending = true;
    }
    layout(st);
    let r = state().rect;
    st.dirty_screen(r);
    visible::recompute_and_expose(st);
    if buttons == GUI_MODAL_INPUT {
        fep::mark_redraw(); /* 未確定行の原点が field の caret へ移る */
    }
    state().id as i32
}

/// WM 自身がファイル選択を開く (デスクトップからプログラムを起動する)。
/// v1.1 の F5 と同じ経路 — completed result は作らず `launch_path` を立てる。
pub fn open_wm_file(st: &mut GuiState, start_dir: &[u8]) {
    if !open_wm(st, GUI_MODAL_FILE_OPEN, b"\0", WM_PURPOSE_FILE_LAUNCH) {
        return;
    }
    set_cwd(start_dir);
    state().reload_pending = true;
}

/// WM 自身の MessageBox (契約 S6 の確認、起動失敗の通知)。
/// `buttons` は [`GUI_MODAL_OK`] / [`GUI_MODAL_YES_NO`] など。
///
/// **戻り値 `false` = 枠が塞がっていて開けなかった**。`settings` の通知経路は
/// これを必ず見て、開けなければ `notice` を保持したまま次の周回へ回す
/// (票 S4 §3 の R1)。
pub fn open_wm_message(st: &mut GuiState, buttons: u16, msg: &[u8], purpose: u8) -> bool {
    open_wm(st, buttons, msg, purpose)
}

/// Start → Settings... の設定ダイアログ (票 S4 §3)。開けたら `true`。
///
/// 既存の部品 (list band の 2 行 + OK / Cancel) だけで組む。新しいウィジェットは
/// 作らない。渡す `cfg` は**開ける周回で読み直した値** = 編集の起点。
pub fn open_wm_settings(st: &mut GuiState, cfg: &crate::settings::GuiCfg) -> bool {
    if state().used {
        return false;
    }
    if !open_wm(st, GUI_MODAL_OK_CANCEL, b"\0", WM_PURPOSE_SETTINGS) {
        return false;
    }
    /* `open_wm` が `Modal::NEW` で塗り直すので、編集値と状態行はその**後**に
     * 入れて、幅と高さを決め直す (状態行の本数と長さで版面が変わる)。 */
    let mut lines = [[0u8; crate::settings::STATUS_LINE_CAP]; crate::settings::STATUS_LINES_MAX];
    let n = crate::settings::status_lines(cfg, &mut lines);
    let old = state().rect;
    {
        let m = state();
        m.set_color = cfg.desktop_color;
        m.set_clock = cfg.clock_24h;
        m.set_row = 0;
        m.set_status = lines;
        m.set_status_n = n;
        let mut i = 0;
        while i < n {
            let mut k = 0;
            while k < crate::settings::STATUS_LINE_CAP && m.set_status[i][k] != 0 {
                k += 1;
            }
            m.set_status_len[i] = k;
            i += 1;
        }
    }
    layout(st);
    let r = state().rect;
    st.dirty_screen(old);
    st.dirty_screen(r);
    visible::recompute_and_expose(st);
    true
}

/// WM 自身の 1 行入力 (Start → Run...)。prompt は `msg`。
pub fn open_wm_input(st: &mut GuiState, prompt: &[u8], purpose: u8) {
    if open_wm(st, GUI_MODAL_INPUT, prompt, purpose) {
        fep::mark_redraw(); /* 未確定行の原点が field の caret へ移る */
    }
}

/// WM owned ダイアログの共通の開き方。既に 1 枚出ていれば開かない (false)。
fn open_wm(st: &mut GuiState, buttons: u16, msg: &[u8], purpose: u8) -> bool {
    if state().used {
        return false;
    }
    let next_id = next_dialog_id();
    {
        let m = state();
        *m = Modal::NEW;
        m.used = true;
        m.id = next_id;
        m.owner = 0;
        m.parent_win = 0;
        m.buttons = buttons;
        m.wm_owned = true;
        m.wm_purpose = purpose;
        let mut n = 0;
        while n < msg.len() && n < MSG_LEN && msg[n] != 0 {
            m.msg[n] = msg[n];
            n += 1;
        }
        m.msg[n] = 0;
        m.msg_len = n;
        m.nbtn = if buttons == GUI_MODAL_OK { 1 } else { 2 };
        m.focus_btn = 0;
    }
    layout(st);
    let r = state().rect;
    st.dirty_screen(r);
    visible::recompute_and_expose(st);
    true
}

/// 完了 (W4 §1 の順序)。結果を先にスロットへ保存し、その後 sticky な
/// `GUI_EV_MODAL` を積む。下地は損傷にして下のクライアントへ damage を戻す。
fn finish(st: &mut GuiState, result: i16) {
    let (id, owner, parent, wm_owned, r, buttons, purpose) = {
        let m = state();
        (m.id, m.owner, m.parent_win, m.wm_owned, m.rect, m.buttons, m.wm_purpose)
    };

    /* ---- 値 (契約 M1): File Open = 絶対パス、Input = UTF-8、その他は空 ---- */
    let mut value = [0u8; VALUE_MAX];
    let mut vlen = 0usize;
    if result == MODAL_RESULT_OK {
        let m = state();
        if buttons == GUI_MODAL_FILE_OPEN {
            /* sel_len は build_selection が 255B 以内を保証している。 */
            while vlen < m.sel_len && vlen < VALUE_MAX {
                value[vlen] = m.sel[vlen];
                vlen += 1;
            }
        } else if buttons == GUI_MODAL_INPUT {
            while vlen < m.text_len && vlen < VALUE_MAX {
                value[vlen] = m.text[vlen];
                vlen += 1;
            }
        }
    }

    /* **後始末より先に閉じる**: 用途によってはここから次のダイアログ
     * (エラー通知) を開くので、`used` を落としておかないと開けない (W3)。 */
    state().used = false;

    if wm_owned {
        finish_wm(st, purpose, result, &value, vlen);
    } else if let Some(slot) = st.slot_of_owner(owner) {
        store_completed(slot, owner, id, result, &value, vlen, parent);
        deliver_pending(st, slot);
    }

    /* 下に隠れていたものを描き直す (デスクトップ + クローム + アプリの Paint)。 */
    st.dirty_screen(r);
    let mut i = 0;
    while i < GUI_MAX_WINDOWS {
        if st.windows[i].used && st.windows[i].visible {
            let (ox, oy) = st.windows[i].client_origin();
            damage::add_dirty(&mut st.windows[i], r.translate(-ox, -oy));
        }
        i += 1;
    }
    visible::recompute_and_expose(st);
    /* FEP の未確定行 / 候補窓は field の caret に紐づいていた。原点を戻す。 */
    if buttons == GUI_MODAL_INPUT {
        fep::mark_redraw();
    }
}

/// WM owned ダイアログの後始末 (W3 §4)。呼ばれた時点でダイアログは閉じている。
///
/// - `FILE_LAUNCH` … v1.1 の F5。選んだ `.bin` を単独ループの起動予約に。
/// - `RUN`         … 絶対パスなら `LAUNCH` に。相対パスは受けずにエラー表示。
/// - `CONFIRM_*`   … **Yes のときだけ** SessionAction を立てる (契約 S6)。
/// - `NOTIFY`      … 何もしない。
fn finish_wm(st: &mut GuiState, purpose: u8, result: i16, value: &[u8], vlen: usize) {
    match purpose {
        WM_PURPOSE_FILE_LAUNCH => {
            if result == MODAL_RESULT_OK && vlen > 0 {
                st.set_launch_path(value, vlen);
                st.launch_pending = true;
            }
        }
        WM_PURPOSE_RUN => {
            if result != MODAL_RESULT_OK {
                return;
            }
            /* v1.2 の Run は絶対パスのみ (PATH 検索も引数列も対象外。契約 D2)。 */
            if vlen == 0 || value[0] != b'/' {
                open_wm_message(
                    st,
                    GUI_MODAL_OK,
                    b"Run: absolute path required (e.g. /usr/bin/filer.bin)\0",
                    WM_PURPOSE_NOTIFY,
                );
                return;
            }
            if session::set_wm(st, os32api::gui::proto::GUI_SESSION_LAUNCH, &value[..vlen]) < 0 {
                open_wm_message(st, GUI_MODAL_OK, b"Run: another action is pending\0", WM_PURPOSE_NOTIFY);
            }
        }
        WM_PURPOSE_CONFIRM_CUI => {
            if result == MODAL_RESULT_OK {
                let _ = session::set_wm(st, GUI_SESSION_SWITCH_CUI, b"\0");
            }
        }
        WM_PURPOSE_CONFIRM_HALT => {
            if result == MODAL_RESULT_OK {
                let _ = session::set_wm(st, GUI_SESSION_SHUTDOWN, b"\0");
            }
        }
        WM_PURPOSE_SETTINGS => {
            /* Cancel / ESC は何も書かない (票 S4 §3)。 */
            if result != MODAL_RESULT_OK {
                return;
            }
            /* **ここでは DB に触らない** — 判定と予約だけ (票 §3)。編集値は
             * `finish` が `used` を落としただけなので、まだ読める。 */
            let (color, clock, _row) = settings_values();
            crate::settings::on_ok(color, clock);
        }
        _ => {}
    }
}

/// アプリが落ちたら (`gui_owner_exit`) そのダイアログと結果を畳む (W4 §9)。
///
/// **別 owner の completed result は消さない。**
pub fn reclaim_owner(st: &mut GuiState, owner: i32) {
    /* (a) completed result / pending Modal event。 */
    let mut s = 0;
    while s < GUI_SLOT_MAX {
        let c = completed(s);
        if (c.used || c.modal_event_pending) && c.owner == owner {
            *c = Completed::NEW;
        }
        s += 1;
    }
    /* (b) active modal (+ modal FEP / caret 状態)。 */
    let (hit, r, was_input) = {
        let m = state();
        if !m.used || m.wm_owned || m.owner != owner {
            (false, Rect::EMPTY, false)
        } else {
            (true, m.rect, m.buttons == GUI_MODAL_INPUT)
        }
    };
    if !hit {
        return;
    }
    {
        let m = state();
        m.used = false;
        m.text_len = 0;
        m.caret = 0;
        m.view_off = 0;
    }
    st.dirty_screen(r);
    visible::recompute_and_expose(st);
    if was_input {
        fep::mark_redraw();
    }
}

/* ================================================================ */
/*  レイアウト                                                       */
/* ================================================================ */

fn layout(st: &GuiState) {
    let m = state();
    if m.wm_owned && m.wm_purpose == WM_PURPOSE_SETTINGS {
        layout_settings(st, m);
        return;
    }
    let (w, h) = match m.buttons {
        GUI_MODAL_FILE_OPEN => (
            /* タイトル帯 + パス行 + 一覧 LIST_ROWS 行 + ボタン。 */
            8 * 46 + PAD * 2,
            TITLE_H + PAD + LINE_H * (LIST_ROWS as i32 + 1) + PAD + BTN_H + PAD,
        ),
        GUI_MODAL_INPUT => {
            let text_w = msg_display_width(m);
            let mut w = text_w + PAD * 2 + 4;
            if w < FIELD_MIN_W + PAD * 2 {
                w = FIELD_MIN_W + PAD * 2;
            }
            if w > st.screen_w - 16 {
                w = st.screen_w - 16;
            }
            (w, TITLE_H + PAD + LINE_H + 4 + FIELD_H + PAD + BTN_H + PAD)
        }
        _ => {
            /* 日本語の確認文 (契約 S6) は 1 文字 3B / 16px なので、バイト数 × 8 で
             * 幅を出すと画面いっぱいに広がる。表示幅で測る (W3)。 */
            let text_w = msg_display_width(m);
            let mut w = text_w + PAD * 2 + 4;
            let min_w = (m.nbtn as i32) * BTN_W + ((m.nbtn as i32) - 1) * BTN_GAP + PAD * 2;
            if w < min_w {
                w = min_w;
            }
            if w > st.screen_w - 16 {
                w = st.screen_w - 16;
            }
            (w, TITLE_H + PAD * 2 + LINE_H + BTN_H + PAD)
        }
    };
    /* 作業領域の中央 (W4 §7)。v1.2 の taskbar は W3 が work_area へ入れる。 */
    let wa = work_area(st);
    let x = wa.x + (wa.w - w) / 2;
    let y = wa.y + (wa.h - h) / 2;
    m.rect = Rect::new(if x < 0 { 0 } else { x }, if y < 0 { 0 } else { y }, w, h);
}

/// メッセージ (prompt) の表示幅。ANK 8px / 全角 16px で数える。
fn msg_display_width(m: &Modal) -> i32 {
    text_width(&m.msg, m.msg_len, 0, m.msg_len)
}

/* ================================================================ */
/*  設定ダイアログ (票 S4 §3) の版面                                  */
/* ================================================================ */

/// 行数 (0 = Desktop color、1 = Clock)。
pub const SET_ROWS: usize = 2;
/// 色見本 (16px 角) の左端を置く桁 (ANK 8px)。行の文字より右。
const SET_SWATCH_COL: i32 = 8 * 24;
/// 色見本の 1 辺。
const SET_SWATCH: i32 = 16;
/// 版面の最小幅。
const SET_MIN_W: i32 = 360;
/// 行 / 状態行の文字の左端 (行矩形の中の相対位置)。
const SET_TEXT_X: i32 = 2;
/// 行の本文を組む一時バッファ。
const SET_ROW_CAP: usize = 64;
/// 幅を測るときの色の値 (**最大桁数**。開いたまま 9 → 10 でも伸びない)。
const COLOR_WIDEST: u8 = crate::settings::COLOR_MAX;

/// 本文 2 行 + 状態行 (1〜3 行) + ボタン。
///
/// **幅も高さも実表示幅から出す** (実装レビュー往復 1 の B2 / B3)。
/// `kcg_draw_utf8` にはクリップが無いので、版面が文字より狭いとそのまま枠外の
/// クライアント面を潰す (モーダルの遮蔽にも損傷にも入らない画素になる)。
///
/// 幅に数えるもの:
///
/// - 行 0 の **2 色モードの説明文**込みの幅 (`(preview off: palette leased)`
///   が付くと 48 文字 = 384px)。リースは**開いている間に付いたり外れたり
///   する**ので、常に長いほう (mono) で確保する。
/// - 行 0 の色の**最大桁数** (0〜15 なので 2 桁) — 開いたまま 9 → 10 に
///   進めても伸びない。
/// - 16 色モードの色見本の右端 (`SET_SWATCH_COL + SET_SWATCH`)。
/// - 状態行それぞれの幅。
///
/// それでも画面 (`screen_w - 16`) に収まらない場合は [`draw_settings`] が
/// 文字数で切る (切っても**枠の外へは出さない**)。
fn layout_settings(st: &GuiState, m: &mut Modal) {
    /* 行の実幅 — 色は最大桁 (15)、説明文は mono 版 (長いほう)。 */
    let mut body = 0i32;
    let mut row = 0usize;
    while row < SET_ROWS {
        let mut buf = [0u8; SET_ROW_CAP];
        let n = settings_row_text(COLOR_WIDEST, m.set_clock, row, true, &mut buf);
        let w = SET_TEXT_X + (n as i32) * 8;
        if w > body {
            body = w;
        }
        row += 1;
    }
    /* 16 色モードの見本 (数値の右に置く)。 */
    if SET_SWATCH_COL + SET_SWATCH > body {
        body = SET_SWATCH_COL + SET_SWATCH;
    }
    /* 状態行。 */
    let mut i = 0usize;
    while i < m.set_status_n {
        let w = SET_TEXT_X + (m.set_status_len[i] as i32) * 8;
        if w > body {
            body = w;
        }
        i += 1;
    }
    let mut w = body + PAD * 2 + 4;
    let min_w = 2 * BTN_W + BTN_GAP + PAD * 2;
    if w < SET_MIN_W {
        w = SET_MIN_W;
    }
    if w < min_w {
        w = min_w;
    }
    if w > st.screen_w - 16 {
        w = st.screen_w - 16;
    }
    let nstat = if m.set_status_n == 0 { 1 } else { m.set_status_n } as i32;
    let h = TITLE_H + PAD + LINE_H * (SET_ROWS as i32) + 4 + LINE_H * nstat + PAD + BTN_H + PAD;
    let wa = work_area(st);
    let x = wa.x + (wa.w - w) / 2;
    let y = wa.y + (wa.h - h) / 2;
    m.rect = Rect::new(if x < 0 { 0 } else { x }, if y < 0 { 0 } else { y }, w, h);
}

/// 設定ダイアログの行 `row` の矩形 (画面座標)。
fn set_row_rect(m: &Modal, row: usize) -> Rect {
    Rect::new(
        m.rect.x + PAD,
        m.rect.y + TITLE_H + PAD + (row as i32) * LINE_H,
        m.rect.w - PAD * 2,
        LINE_H,
    )
}

/// 2 行ぶんの帯 (損傷を絞る用)。
fn set_rows_band(m: &Modal) -> Rect {
    Rect::new(
        m.rect.x + 1,
        m.rect.y + TITLE_H + 1,
        m.rect.w - 2,
        PAD + LINE_H * (SET_ROWS as i32),
    )
}

/// 状態行 `i` の矩形。
fn set_status_rect(m: &Modal, i: usize) -> Rect {
    Rect::new(
        m.rect.x + PAD,
        m.rect.y + TITLE_H + PAD + LINE_H * (SET_ROWS as i32) + 4 + (i as i32) * LINE_H,
        m.rect.w - PAD * 2,
        LINE_H,
    )
}

/// ボタン i の矩形 (画面座標)。右詰め。
fn button_rect(m: &Modal, i: usize) -> Rect {
    let n = m.nbtn as i32;
    let total = n * BTN_W + (n - 1) * BTN_GAP;
    let x0 = m.rect.x + m.rect.w - PAD - total;
    let y = m.rect.y + m.rect.h - PAD - BTN_H;
    Rect::new(x0 + (i as i32) * (BTN_W + BTN_GAP), y, BTN_W, BTN_H)
}

/// ボタン帯 (損傷を絞るための外接矩形)。
fn buttons_band(m: &Modal) -> Rect {
    Rect::new(
        m.rect.x + 1,
        m.rect.y + m.rect.h - PAD - BTN_H - 2,
        m.rect.w - 2,
        BTN_H + PAD + 1,
    )
}

/// 一覧の 1 行の矩形 (ファイル選択、画面座標)。
/// 先頭にパス行 1 行分の隙間があるので `LINE_H` 下げる (v1.1 は `draw_list` 側で
/// 下げていて `on_button` の当たり判定と 1 行ずれていた — W4 で揃えた)。
fn row_rect(m: &Modal, row: usize) -> Rect {
    Rect::new(
        m.rect.x + PAD,
        m.rect.y + TITLE_H + PAD + ((row + 1) as i32) * LINE_H,
        m.rect.w - PAD * 2,
        LINE_H,
    )
}

/// ファイル選択の一覧の行 `row` (画面座標)。試験の観測点 (票 KBD_NAV K1 ④:
/// マウスキーの + がダブルクリックの判定に入るか)。
#[allow(dead_code)]
pub fn file_row_rect(row: usize) -> Rect {
    row_rect(state(), row)
}

/// 一覧 + パス行の帯 (損傷を絞る用)。
fn list_band(m: &Modal) -> Rect {
    Rect::new(
        m.rect.x + 1,
        m.rect.y + TITLE_H + 1,
        m.rect.w - 2,
        PAD + LINE_H * (LIST_ROWS as i32 + 1),
    )
}

/// Input dialog の edit field (画面座標)。
fn field_rect(m: &Modal) -> Rect {
    Rect::new(
        m.rect.x + PAD,
        m.rect.y + TITLE_H + PAD + LINE_H + 4,
        m.rect.w - PAD * 2,
        FIELD_H,
    )
}

/// field の中の文字を置ける矩形。
fn field_text_rect(m: &Modal) -> Rect {
    let f = field_rect(m);
    Rect::new(f.x + 3, f.y + 2, f.w - 6, 16)
}

fn button_label(buttons: u16, i: usize) -> &'static [u8] {
    match buttons {
        GUI_MODAL_OK => b"OK\0",
        GUI_MODAL_OK_CANCEL | GUI_MODAL_INPUT => {
            if i == 0 {
                b"OK\0"
            } else {
                b"Cancel\0"
            }
        }
        GUI_MODAL_YES_NO => {
            if i == 0 {
                b"Yes\0"
            } else {
                b"No\0"
            }
        }
        _ => {
            if i == 0 {
                b"Open\0"
            } else {
                b"Cancel\0"
            }
        }
    }
}

fn title_label(m: &Modal) -> &'static [u8] {
    if m.wm_owned && m.wm_purpose == WM_PURPOSE_SETTINGS {
        return b"Settings\0";
    }
    match m.buttons {
        GUI_MODAL_FILE_OPEN => b"Open file\0",
        GUI_MODAL_INPUT => b"Input\0",
        _ => b"Message\0",
    }
}

/* ================================================================ */
/*  UTF-8 の小道具 (caret はコードポイント境界でしか動かない)         */
/* ================================================================ */

/// UTF-8 先頭バイトから文字の長さ (壊れていれば 1)。
fn utf8_len(b: u8) -> usize {
    if b < 0x80 {
        1
    } else if b >= 0xF0 {
        4
    } else if b >= 0xE0 {
        3
    } else if b >= 0xC0 {
        2
    } else {
        1
    }
}

/// `i` の 1 つ前のコードポイント先頭 (継続バイト 0b10xxxxxx を飛ばす)。
fn prev_boundary(t: &[u8], i: usize) -> usize {
    let mut k = i;
    while k > 0 {
        k -= 1;
        if (t[k] & 0xC0) != 0x80 {
            return k;
        }
    }
    0
}

/// `i` の次のコードポイント先頭。
fn next_boundary(t: &[u8], len: usize, i: usize) -> usize {
    if i >= len {
        return len;
    }
    let n = utf8_len(t[i]);
    let k = i + n;
    if k > len {
        len
    } else {
        k
    }
}

/// 1 コードポイントの表示幅 (ANK 8px / それ以外は全角 16px)。
#[inline]
fn cp_width(b: u8) -> i32 {
    if b < 0x80 {
        8
    } else {
        16
    }
}

/// `from`〜`to` (バイト、境界前提) の表示幅。
fn text_width(t: &[u8], len: usize, from: usize, to: usize) -> i32 {
    let mut w = 0;
    let mut i = from;
    while i < to && i < len {
        w += cp_width(t[i]);
        i = next_boundary(t, len, i);
    }
    w
}

/* ================================================================ */
/*  入力 (X3 だけ。契約 T8)                                          */
/* ================================================================ */

/// キー 1 件。ダイアログが閉じたら true。
///
/// 呼び出し元は `input::capture_keyboard` の X3 経路だけ (`ctx.wm_ui()`)。
/// X4 (ポンプ) はモーダル中に打鍵を取り込まないので、ここから `sys_ls` などの
/// VFS 走査を呼んでも契約 T8 / W4 §5 に反しない。
pub fn on_key(st: &mut GuiState, scan: u8, ch: u8, mods: u32) -> bool {
    if !state().used {
        return false;
    }
    let buttons = state().buttons;
    if is_settings() {
        return settings_key(st, scan);
    }
    if buttons == GUI_MODAL_INPUT {
        return input_key(st, scan, ch, mods);
    }
    let file = buttons == GUI_MODAL_FILE_OPEN;
    match scan {
        SC_ESC => {
            /* W4 §4: OK / OK-Cancel / Yes-No のいずれも ESC は result=0。 */
            finish(st, MODAL_RESULT_CANCEL);
            return true;
        }
        SC_TAB | SC_RIGHT => {
            let m = state();
            m.focus_btn = (m.focus_btn + 1) % m.nbtn;
            let band = buttons_band(m);
            st.dirty_screen(band);
        }
        SC_LEFT => {
            let m = state();
            m.focus_btn = (m.focus_btn + m.nbtn - 1) % m.nbtn;
            let band = buttons_band(m);
            st.dirty_screen(band);
        }
        SC_UP if file => {
            move_cursor(st, -1);
        }
        SC_DOWN if file => {
            move_cursor(st, 1);
        }
        SC_ROLLUP if file => {
            move_cursor(st, -(LIST_ROWS as i32));
        }
        SC_ROLLDOWN if file => {
            move_cursor(st, LIST_ROWS as i32);
        }
        SC_HOME if file => {
            {
                let m = state();
                m.cursor = 0;
                m.scroll = 0;
            }
            let band = list_band(state());
            st.dirty_screen(band);
        }
        SC_RETURN | SC_SPACE => {
            return activate(st);
        }
        _ => {}
    }
    false
}

/* ---------------- 設定ダイアログのキー (票 S4 §3) ---------------- */

/// ↑ / ↓ で行移動、← / → / SPACE で値、RETURN = OK、ESC = Cancel、
/// **TAB は何もしない** (焦点表示を動かさない = 表示と RETURN を揃える)。
/// **入力欄は使わない** (数値入力の検証を持ち込まない)。
fn settings_key(st: &mut GuiState, scan: u8) -> bool {
    match scan {
        SC_ESC => {
            finish(st, MODAL_RESULT_CANCEL);
            true
        }
        SC_RETURN => {
            state().focus_btn = 0;
            finish(st, MODAL_RESULT_OK);
            true
        }
        SC_UP => {
            set_move_row(st, -1);
            false
        }
        SC_DOWN => {
            set_move_row(st, 1);
            false
        }
        SC_RIGHT | SC_SPACE => {
            set_bump(st, 1);
            false
        }
        SC_LEFT => {
            set_bump(st, -1);
            false
        }
        SC_TAB => {
            /* 設定ダイアログでも **TAB は焦点を動かさない** (S4 実装レビュー
             * 往復 2 の non-blocker)。動かすと「焦点表示は Cancel なのに
             * RETURN は OK」という食い違いが出る — ここの RETURN は上の枝で
             * 常に `MODAL_RESULT_OK` だから。Input ダイアログ (契約 M4) と
             * 同じ扱いにして、RETURN = OK / ESC = Cancel の契約と表示を
             * 揃える。ボタンはマウスで押せる。 */
            false
        }
        _ => false,
    }
}

fn set_move_row(st: &mut GuiState, delta: i32) {
    {
        let m = state();
        let mut r = m.set_row as i32 + delta;
        if r < 0 {
            r = 0;
        }
        if r >= SET_ROWS as i32 {
            r = SET_ROWS as i32 - 1;
        }
        m.set_row = r as usize;
    }
    let band = set_rows_band(state());
    st.dirty_screen(band);
}

/// 選択中の行の値を `delta` だけ進める (color は 0..15 を循環、clock はトグル)。
fn set_bump(st: &mut GuiState, delta: i32) {
    {
        let m = state();
        if m.set_row == 0 {
            let n = crate::settings::COLOR_MAX as i32 + 1;
            let v = (m.set_color as i32 + delta + n) % n;
            m.set_color = v as u8;
        } else {
            m.set_clock = !m.set_clock;
        }
    }
    let band = set_rows_band(state());
    st.dirty_screen(band);
}

/* ---------------- Input dialog のキー (契約 M4 / W4 §6) ---------------- */

fn input_key(st: &mut GuiState, scan: u8, ch: u8, mods: u32) -> bool {
    match scan {
        SC_ESC => {
            finish(st, MODAL_RESULT_CANCEL);
            return true;
        }
        SC_RETURN => {
            /* W4 §6: RETURN = OK。focus はボタンへ移らない (下の SC_TAB 参照)。 */
            state().focus_btn = 0;
            finish(st, MODAL_RESULT_OK);
            return true;
        }
        SC_TAB => {
            /* Input では TAB を focus 移動に使わない — RETURN が常に OK である
             * という契約 (M4) を素直に保つため。ボタンはマウスで押せる。 */
            return false;
        }
        SC_LEFT => {
            let m = state();
            if m.caret > 0 {
                m.caret = prev_boundary(&m.text, m.caret);
            }
            after_text_change(st, false);
            return false;
        }
        SC_RIGHT => {
            let m = state();
            if m.caret < m.text_len {
                m.caret = next_boundary(&m.text, m.text_len, m.caret);
            }
            after_text_change(st, false);
            return false;
        }
        SC_HOME => {
            let m = state();
            /* SHIFT+HOME は END 相当 (PC-98 に END キーが無いため)。 */
            m.caret = if (mods & MOD_SHIFT) != 0 { m.text_len } else { 0 };
            after_text_change(st, false);
            return false;
        }
        SC_HELP => {
            let m = state();
            m.caret = m.text_len;
            after_text_change(st, false);
            return false;
        }
        SC_BS => {
            let m = state();
            if m.caret > 0 {
                let from = prev_boundary(&m.text, m.caret);
                remove_range(m, from, m.caret);
                m.caret = from;
            }
            after_text_change(st, true);
            return false;
        }
        SC_DEL => {
            let m = state();
            if m.caret < m.text_len {
                let to = next_boundary(&m.text, m.text_len, m.caret);
                let c = m.caret;
                remove_range(m, c, to);
            }
            after_text_change(st, true);
            return false;
        }
        _ => {}
    }
    /* 印字可能 ANK の挿入 (UTF-8 の 1 バイト文字)。 */
    if ch >= 0x20 && ch <= 0x7E {
        let one = [ch];
        insert_bytes(st, &one);
    }
    false
}

/// FEP の確定文字列 (UTF-8) を field へ入れる (`fep::flush_text` から)。
pub fn insert_commit(st: &mut GuiState, bytes: &[u8]) {
    if !is_input() {
        return;
    }
    insert_bytes(st, bytes);
}

/// caret 位置へ UTF-8 バイト列を挿入する。**255B を超えるなら入れない**
/// (既存内容を保つ。W4 §6: 切り詰めない)。
fn insert_bytes(st: &mut GuiState, bytes: &[u8]) {
    {
        let m = state();
        let n = bytes.len();
        if n == 0 || m.text_len + n > VALUE_MAX {
            return; /* 超過分は丸ごと拒否 */
        }
        /* 後ろへずらす。 */
        let mut i = m.text_len;
        while i > m.caret {
            m.text[i + n - 1] = m.text[i - 1];
            i -= 1;
        }
        let mut k = 0;
        while k < n {
            m.text[m.caret + k] = bytes[k];
            k += 1;
        }
        m.text_len += n;
        m.caret += n;
        m.text[m.text_len] = 0;
    }
    after_text_change(st, true);
}

fn remove_range(m: &mut Modal, from: usize, to: usize) {
    if to <= from || to > m.text_len {
        return;
    }
    let n = to - from;
    let mut i = to;
    while i < m.text_len {
        m.text[i - n] = m.text[i];
        i += 1;
    }
    m.text_len -= n;
    m.text[m.text_len] = 0;
    if m.caret > m.text_len {
        m.caret = m.text_len;
    }
}

/// 文字列 / caret が動いた後の後始末: 横スクロールを詰め直し、field だけを
/// 損傷にし、FEP の未確定行を caret に追従させる。
fn after_text_change(st: &mut GuiState, _edited: bool) {
    {
        let m = state();
        let avail = field_text_rect(m).w;
        /* caret が左に出たら view を戻す。 */
        if m.caret < m.view_off {
            m.view_off = m.caret;
        }
        /* caret が右に出たら 1 コードポイントずつ view を進める。 */
        let mut guard = 0;
        while text_width(&m.text, m.text_len, m.view_off, m.caret) > avail - 8 {
            guard += 1;
            if guard > VALUE_MAX {
                break;
            }
            if m.view_off >= m.caret {
                break;
            }
            m.view_off = next_boundary(&m.text, m.text_len, m.view_off);
        }
    }
    let fr = field_rect(state());
    st.dirty_screen(fr);
    /* SET_TEXT_CURSOR 相当: FEP の未確定行 / 候補窓を caret へ移す。 */
    fep::mark_redraw();
}

/// FEP が未確定行 / 候補窓を出す原点 (画面座標)。Input dialog のときだけ。
/// [`crate::fep::anchor`] が窓の `SET_TEXT_CURSOR` より優先して使う (W4 §6)。
pub fn fep_caret() -> Option<(i32, i32)> {
    let m = state();
    if !m.used || m.buttons != GUI_MODAL_INPUT {
        return None;
    }
    let tr = field_text_rect(m);
    let cx = tr.x + text_width(&m.text, m.text_len, m.view_off, m.caret);
    Some((cx, tr.y + 16 + 1))
}

/* ---------------- マウス ---------------- */

/// クリック 1 件 (押下)。ダイアログが閉じたら true。
pub fn on_button(st: &mut GuiState, mx: i32, my: i32) -> bool {
    if !state().used {
        return false;
    }
    /* ボタン */
    let (nbtn, buttons) = {
        let m = state();
        (m.nbtn, m.buttons)
    };
    let mut i = 0;
    while i < nbtn {
        let br = button_rect(state(), i);
        if br.contains(mx, my) {
            state().focus_btn = i;
            return activate(st);
        }
        i += 1;
    }
    /* 設定: row クリックで選択、同じ row の再クリックで値を進める (票 S4 §3)。 */
    if is_settings() {
        let mut row = 0;
        while row < SET_ROWS {
            let rr = set_row_rect(state(), row);
            if rr.contains(mx, my) {
                if state().set_row == row {
                    set_bump(st, 1);
                } else {
                    state().set_row = row;
                    let band = set_rows_band(state());
                    st.dirty_screen(band);
                }
                return false;
            }
            row += 1;
        }
        return false;
    }
    /* Input: field をクリックしたら caret を移す。 */
    if buttons == GUI_MODAL_INPUT {
        let fr = field_rect(state());
        if fr.contains(mx, my) {
            caret_from_x(st, mx);
        }
        return false;
    }
    /* 一覧の行 (ダブルクリックで降りる / 確定 = W4 §5) */
    if buttons == GUI_MODAL_FILE_OPEN {
        let now = st.now;
        let mut row = 0;
        while row < LIST_ROWS {
            let rr = row_rect(state(), row);
            if rr.contains(mx, my) {
                let (idx, dbl) = {
                    let m = state();
                    let idx = m.scroll + row;
                    let dbl = m.last_click_row == idx as i32
                        && now.wrapping_sub(m.last_click_tick) < DBLCLICK_TICKS;
                    (idx, dbl)
                };
                if idx >= state().nentries {
                    return false;
                }
                {
                    let m = state();
                    m.cursor = idx;
                    m.last_click_row = idx as i32;
                    m.last_click_tick = now;
                }
                let band = list_band(state());
                st.dirty_screen(band);
                if dbl {
                    /* ダブルクリック = Open ボタン相当。 */
                    state().focus_btn = 0;
                    return activate(st);
                }
                return false;
            }
            row += 1;
        }
    }
    false
}

/// field のクリック位置から caret を決める (コードポイント境界へ丸める)。
fn caret_from_x(st: &mut GuiState, mx: i32) {
    {
        let m = state();
        let tr = field_text_rect(m);
        let mut i = m.view_off;
        let mut x = tr.x;
        while i < m.text_len {
            let w = cp_width(m.text[i]);
            if mx < x + w / 2 {
                break;
            }
            x += w;
            if x > tr.x + tr.w {
                break;
            }
            i = next_boundary(&m.text, m.text_len, i);
        }
        m.caret = i;
    }
    after_text_change(st, false);
}

/// 「決定」= フォーカス中のボタンを押す。閉じたら true。
fn activate(st: &mut GuiState) -> bool {
    let (buttons, focus) = {
        let m = state();
        (m.buttons, m.focus_btn)
    };
    if buttons == GUI_MODAL_FILE_OPEN && focus == 0 {
        /* Open: ディレクトリなら降りる、ファイルなら確定。 */
        let (isdir, has) = {
            let m = state();
            if m.cursor < m.nentries {
                (m.is_dir[m.cursor], true)
            } else {
                (false, false)
            }
        };
        if !has {
            return false;
        }
        if isdir {
            enter_dir(st);
            return false;
        }
        /* W4 §5: 255B を超える絶対パスの項目は Open できない (切り詰めない)。 */
        if !build_selection() {
            return false;
        }
        finish(st, MODAL_RESULT_OK);
        return true;
    }
    let result = if focus == 0 { MODAL_RESULT_OK } else { MODAL_RESULT_CANCEL };
    finish(st, result);
    true
}

fn move_cursor(st: &mut GuiState, delta: i32) {
    {
        let m = state();
        if m.nentries == 0 {
            return;
        }
        let mut c = m.cursor as i32 + delta;
        if c < 0 {
            c = 0;
        }
        if c >= m.nentries as i32 {
            c = m.nentries as i32 - 1;
        }
        m.cursor = c as usize;
        if m.cursor < m.scroll {
            m.scroll = m.cursor;
        } else if m.cursor >= m.scroll + LIST_ROWS {
            m.scroll = m.cursor + 1 - LIST_ROWS;
        }
    }
    let band = list_band(state());
    st.dirty_screen(band);
}

/// 中身が変わったので描き直す (X3 の合成に任せる)。矩形を絞れないときだけ使う。
fn invalidate(st: &mut GuiState) {
    let r = state().rect;
    st.dirty_screen(r);
}

/* ================================================================ */
/*  ファイル選択のディレクトリ走査 (X3 だけ。W4 §5)                  */
/* ================================================================ */

fn set_cwd(path: &[u8]) {
    let m = state();
    let mut n = 0;
    while n < path.len() && n < PATH_LEN - 2 && path[n] != 0 {
        m.cwd[n] = path[n];
        n += 1;
    }
    if n == 0 {
        m.cwd[0] = b'/';
        n = 1;
    }
    m.cwd[n] = 0;
    m.cwd_len = n;
}

extern "C" fn ls_cb(entry: *const DirEntryExt, _ctx: *mut u8) {
    let m = state();
    if m.nentries >= MAX_ENTRIES || entry.is_null() {
        return;
    }
    let e = unsafe { &*entry };
    /* "." は出さない (".." は上へ戻るのに使う)。 */
    if e.name[0] == b'.' && e.name[1] == 0 {
        return;
    }
    let mut i = 0;
    while i < NAME_LEN - 1 && e.name[i] != 0 {
        m.names[m.nentries][i] = e.name[i];
        i += 1;
    }
    /* 収まらなかった = 表示用に切り詰めた。この行は選択できない
     * (契約 M1「切り詰めて別値として返してはならない」)。 */
    let truncated = e.name[NAME_LEN - 1] != 0;
    if truncated {
        /* 表示が □ にならないよう、末尾の不完全な UTF-8 列を落とす (§4-27)。 */
        while i > 0 && (m.names[m.nentries][i - 1] & 0xC0) == 0x80 {
            i -= 1;
        }
        if i > 0 && (m.names[m.nentries][i - 1] & 0x80) != 0 {
            i -= 1;
        }
    }
    m.names[m.nentries][i] = 0;
    m.is_dir[m.nentries] = e.ftype == FILE_TYPE_DIR;
    m.too_long[m.nentries] = truncated;
    m.nentries += 1;
}

fn reload_dir() {
    {
        let m = state();
        m.nentries = 0;
        m.cursor = 0;
        m.scroll = 0;
        m.last_click_row = -1;
    }
    let path = {
        let m = state();
        let mut p = [0u8; PATH_LEN];
        let mut i = 0;
        while i < m.cwd_len {
            p[i] = m.cwd[i];
            i += 1;
        }
        p[i] = 0;
        p
    };
    unsafe {
        (os32api::api().sys_ls)(
            path.as_ptr(),
            ls_cb as *const () as *mut u8,
            core::ptr::null_mut(),
        );
    }
}

/// カーソル位置のエントリ名の長さ。
fn name_len(m: &Modal, idx: usize) -> usize {
    let mut i = 0;
    while i < NAME_LEN && m.names[idx][i] != 0 {
        i += 1;
    }
    i
}

/// `cwd` + "/" + `names[cursor]` の長さ (絶対パスのバイト数)。
fn selection_len(m: &Modal) -> usize {
    let mut n = m.cwd_len;
    if n > 0 && m.cwd[n - 1] != b'/' {
        n += 1;
    }
    n + name_len(m, m.cursor)
}

/// カーソル位置のディレクトリへ降りる / ".." で上がる。
fn enter_dir(st: &mut GuiState) {
    {
        let m = state();
        if m.cursor >= m.nentries {
            return;
        }
        if m.names[m.cursor][0] == b'.' && m.names[m.cursor][1] == b'.' && m.names[m.cursor][2] == 0
        {
            /* 親へ */
            let mut n = m.cwd_len;
            while n > 1 && m.cwd[n - 1] != b'/' {
                n -= 1;
            }
            if n > 1 {
                n -= 1;
            }
            if n == 0 {
                n = 1;
            }
            m.cwd[n] = 0;
            m.cwd_len = n;
        } else {
            /* W4 §5: 255B を超える path は作らない (作っても中の file が
             * Open できず、切り詰めた別 path を返すのは禁止)。
             * 名前自体が切り詰まっている行も同じ理由で降りられない。 */
            if m.too_long[m.cursor] || selection_len(m) > VALUE_MAX {
                return;
            }
            let mut n = m.cwd_len;
            if n > 0 && m.cwd[n - 1] != b'/' && n < PATH_LEN - 1 {
                m.cwd[n] = b'/';
                n += 1;
            }
            let mut i = 0;
            while i < NAME_LEN && m.names[m.cursor][i] != 0 && n < PATH_LEN - 1 {
                m.cwd[n] = m.names[m.cursor][i];
                n += 1;
                i += 1;
            }
            m.cwd[n] = 0;
            m.cwd_len = n;
        }
    }
    reload_dir();
    invalidate(st); /* パス行 + 一覧 + 選択が総取り替え */
}

/// カーソル位置のファイルのフルパスを `sel` に組む。
/// 255B を超えるなら**何も書かず false** (W4 §5: 切り詰めた別 path を返さない)。
fn build_selection() -> bool {
    let m = state();
    /* 名前が切り詰まっている行は、実在しない path か同じ接頭辞の別 file に
     * なるので返さない (契約 M1)。 */
    if m.cursor < MAX_ENTRIES && m.too_long[m.cursor] {
        m.sel_len = 0;
        m.sel[0] = 0;
        return false;
    }
    if selection_len(m) > VALUE_MAX {
        m.sel_len = 0;
        m.sel[0] = 0;
        return false;
    }
    let mut n = 0;
    while n < m.cwd_len && n < PATH_LEN - 1 {
        m.sel[n] = m.cwd[n];
        n += 1;
    }
    if n > 0 && m.sel[n - 1] != b'/' && n < PATH_LEN - 1 {
        m.sel[n] = b'/';
        n += 1;
    }
    let mut i = 0;
    while i < NAME_LEN && m.names[m.cursor][i] != 0 && n < PATH_LEN - 1 {
        m.sel[n] = m.names[m.cursor][i];
        n += 1;
        i += 1;
    }
    m.sel[n] = 0;
    m.sel_len = n;
    true
}

/* ================================================================ */
/*  描画 (X3。wm::composite_rect の最後に呼ばれる)                   */
/* ================================================================ */

/// `clip` に掛かるならダイアログ全体を描き直す (kcg にクリップが無いため)。
/// **present するのは損傷矩形だけ**なので、部分損傷でも転送量は増えない。
pub fn draw(st: &GuiState, clip: Rect) {
    let m = state();
    if !m.used || !m.rect.intersects(&clip) {
        return;
    }
    let mono = lease::mono(st);
    let r = m.rect;
    let face = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_FACE };

    unsafe {
        /* 面 + 立体枠 */
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

        /* タイトル帯 (常にアクティブ扱い) */
        let tb = Rect::new(r.x + 1, r.y + 1, r.w - 2, TITLE_H);
        let tcol = if mono { GUI_COLOR_TEXT } else { GUI_COLOR_TITLE_ACTIVE };
        let ttxt = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_TITLE_TEXT };
        gfx::gfx_fill_rect(tb.x, tb.y, tb.w, tb.h, tcol);
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(tb.x + 4, tb.y + 1, title_label(m).as_ptr(), ttxt, tcol);
    }

    if m.wm_owned && m.wm_purpose == WM_PURPOSE_SETTINGS {
        draw_settings(m, mono, face);
        let mut i = 0;
        while i < m.nbtn {
            draw_button(m, i, mono, i == m.focus_btn);
            i += 1;
        }
        return;
    }

    match m.buttons {
        GUI_MODAL_FILE_OPEN => draw_list(m, mono, face),
        GUI_MODAL_INPUT => draw_input(m, mono, face),
        _ => unsafe {
            gfx::kcg_draw_utf8(
                r.x + PAD,
                r.y + TITLE_H + PAD,
                m.msg.as_ptr(),
                GUI_COLOR_TEXT,
                face,
            );
        },
    }

    /* ボタン */
    let mut i = 0;
    while i < m.nbtn {
        draw_button(m, i, mono, i == m.focus_btn);
        i += 1;
    }
}

fn draw_list(m: &Modal, mono: bool, face: u8) {
    unsafe {
        gfx::kcg_set_scale(1);
    }
    /* パス表示 */
    let r = m.rect;
    unsafe {
        gfx::kcg_draw_utf8(r.x + PAD, r.y + TITLE_H + 2, m.cwd.as_ptr(), GUI_COLOR_TEXT, face);
    }
    let mut row = 0;
    while row < LIST_ROWS {
        let rr = row_rect(m, row);
        let idx = m.scroll + row;
        let sel = idx == m.cursor && idx < m.nentries;
        let bg = if !sel {
            face
        } else if mono {
            GUI_COLOR_TEXT
        } else {
            GUI_COLOR_SEL_BG
        };
        let fg = if !sel {
            GUI_COLOR_TEXT
        } else if mono {
            GUI_COLOR_WINDOW
        } else {
            GUI_COLOR_SEL_TEXT
        };
        unsafe {
            gfx::gfx_fill_rect(rr.x, rr.y, rr.w, rr.h, bg);
        }
        if idx < m.nentries {
            let mark: &[u8] = if m.is_dir[idx] { b"[D] \0" } else { b"    \0" };
            unsafe {
                gfx::kcg_draw_utf8(rr.x + 2, rr.y + 1, mark.as_ptr(), fg, bg);
                gfx::kcg_draw_utf8(rr.x + 2 + 32, rr.y + 1, m.names[idx].as_ptr(), fg, bg);
            }
        }
        row += 1;
    }
}

/// 設定ダイアログ: 2 行 + 状態行。
///
/// **16 色リース中 (契約 G8、`lease::mono`) は `draw_list` と同じ 2 色分岐**
/// (票 S4 §3 の R2): 選択行は `TEXT` / `WINDOW` の反転、色見本は描かず
/// `(preview off: palette leased)` と数値だけを出す。WM モーダルは前面アプリを
/// 入れ替えないので、借り手が変えた色がそのまま残っているため。
fn draw_settings(m: &Modal, mono: bool, face: u8) {
    unsafe {
        gfx::kcg_set_scale(1);
    }
    let mut row = 0;
    while row < SET_ROWS {
        let rr = set_row_rect(m, row);
        let sel = row == m.set_row;
        let bg = if !sel {
            face
        } else if mono {
            GUI_COLOR_TEXT
        } else {
            GUI_COLOR_SEL_BG
        };
        let fg = if !sel {
            GUI_COLOR_TEXT
        } else if mono {
            GUI_COLOR_WINDOW
        } else {
            GUI_COLOR_SEL_TEXT
        };
        unsafe {
            gfx::gfx_fill_rect(rr.x, rr.y, rr.w, rr.h, bg);
        }
        let mut buf = [0u8; SET_ROW_CAP];
        let n = settings_row_text(m.set_color, m.set_clock, row, mono, &mut buf);
        draw_clipped(rr, &mut buf, n, fg, bg);
        /* 色見本は 16 色が使えるときだけ (2 色モードでは数値表記に倒す)。 */
        if row == 0 && !mono && rr.x + SET_SWATCH_COL + SET_SWATCH <= rr.right() {
            let sx = rr.x + SET_SWATCH_COL;
            let sy = rr.y + (LINE_H - SET_SWATCH) / 2;
            unsafe {
                gfx::gfx_fill_rect(sx, sy, SET_SWATCH, SET_SWATCH, m.set_color);
                gfx::gfx_rect(sx, sy, SET_SWATCH, SET_SWATCH, GUI_COLOR_TEXT);
            }
        }
        row += 1;
    }
    /* 状態行 1〜3 本 (票 §3 / §2 の採取経路 (1))。 */
    let mut i = 0usize;
    while i < m.set_status_n {
        let sr = set_status_rect(m, i);
        unsafe {
            gfx::gfx_fill_rect(sr.x, sr.y, sr.w, sr.h, face);
        }
        let mut buf = [0u8; SET_ROW_CAP];
        let n = m.set_status_len[i];
        let mut k = 0;
        while k < n && k < SET_ROW_CAP - 1 {
            buf[k] = m.set_status[i][k];
            k += 1;
        }
        draw_clipped(sr, &mut buf, k, GUI_COLOR_TEXT, face);
        i += 1;
    }
}

/// 行矩形 `r` の中へ ANK 文字列を置く。**入り切らない分は切る**
/// (実装レビュー往復 1 の B2 / B3)。
///
/// `kcg_draw_utf8` にクリップは無く、描いた画素はモーダルの遮蔽にも損傷にも
/// 入らない (WM はクライアント面を持たないので消せない)。版面は
/// [`layout_settings`] が実表示幅から決めるので普通は切られないが、
/// 画面幅 (`screen_w - 16`) に収まらない極端な状態行でも**枠の外へは 1 画素も
/// 出さない**ための最後の砦。ここで扱うのは ANK だけ (1 文字 = 1 バイト = 8px)。
fn draw_clipped(r: Rect, buf: &mut [u8; SET_ROW_CAP], len: usize, fg: u8, bg: u8) {
    let avail = (r.w - SET_TEXT_X) / 8;
    if avail <= 0 {
        return;
    }
    let mut n = len;
    if n > avail as usize {
        n = avail as usize;
    }
    if n >= SET_ROW_CAP {
        n = SET_ROW_CAP - 1;
    }
    buf[n] = 0;
    if n == 0 {
        return;
    }
    unsafe {
        gfx::kcg_draw_utf8(r.x + SET_TEXT_X, r.y + 1, buf.as_ptr(), fg, bg);
    }
}

/// 行 `row` の本文 (NUL 終端)。戻り値は長さ。
///
/// 値を引数で受けるのは [`layout_settings`] が**最大桁の色**と**mono の
/// 説明文つき**で幅を測るため (表示中にリースが付いても枠外へ出さない)。
fn settings_row_text(
    color: u8,
    clock: bool,
    row: usize,
    mono: bool,
    buf: &mut [u8; SET_ROW_CAP],
) -> usize {
    let mut n = 0usize;
    fn put(buf: &mut [u8; SET_ROW_CAP], n: &mut usize, s: &[u8]) {
        let mut i = 0;
        while i < s.len() && s[i] != 0 && *n < SET_ROW_CAP - 1 {
            buf[*n] = s[i];
            *n += 1;
            i += 1;
        }
    }
    if row == 0 {
        put(buf, &mut n, b"Desktop color : ");
        if color >= 10 {
            let c = [b'0' + color / 10];
            put(buf, &mut n, &c);
        }
        let c = [b'0' + color % 10];
        put(buf, &mut n, &c);
        if mono {
            put(buf, &mut n, b" (preview off: palette leased)");
        }
    } else {
        put(buf, &mut n, b"Clock : ");
        put(buf, &mut n, if clock { b"24h" } else { b"12h" });
    }
    buf[n] = 0;
    n
}

/// Input dialog: prompt + 沈んだ edit field + caret。
fn draw_input(m: &Modal, mono: bool, face: u8) {
    let r = m.rect;
    unsafe {
        gfx::kcg_set_scale(1);
        /* prompt = GuiReqModal.message */
        gfx::kcg_draw_utf8(
            r.x + PAD,
            r.y + TITLE_H + PAD,
            m.msg.as_ptr(),
            GUI_COLOR_TEXT,
            face,
        );
    }
    let fr = field_rect(m);
    let tr = field_text_rect(m);
    unsafe {
        gfx::gfx_fill_rect(fr.x, fr.y, fr.w, fr.h, GUI_COLOR_WINDOW);
        gfx::gfx_rect(fr.x, fr.y, fr.w, fr.h, GUI_COLOR_TEXT);
        if !mono {
            /* 沈んだ枠 (上/左が影)。 */
            gfx::gfx_hline(fr.x + 1, fr.y + 1, fr.w - 2, GUI_COLOR_SHADOW);
            gfx::gfx_vline(fr.x + 1, fr.y + 1, fr.h - 2, GUI_COLOR_SHADOW);
        }
    }
    /* 見えている範囲だけを NUL 終端で切り出す (kcg にクリップが無い)。 */
    let mut buf = [0u8; VALUE_MAX + 1];
    let mut n = 0;
    let mut i = m.view_off;
    let mut w = 0;
    while i < m.text_len {
        let cw = cp_width(m.text[i]);
        if w + cw > tr.w {
            break;
        }
        let nb = next_boundary(&m.text, m.text_len, i);
        let mut k = i;
        while k < nb && n < VALUE_MAX {
            buf[n] = m.text[k];
            n += 1;
            k += 1;
        }
        w += cw;
        i = nb;
    }
    buf[n] = 0;
    if n > 0 {
        unsafe {
            gfx::kcg_draw_utf8(tr.x, tr.y, buf.as_ptr(), GUI_COLOR_TEXT, GUI_COLOR_WINDOW);
        }
    }
    /* caret (縦線)。 */
    let cx = tr.x + text_width(&m.text, m.text_len, m.view_off, m.caret);
    unsafe {
        gfx::gfx_vline(cx, tr.y, 16, GUI_COLOR_TEXT);
    }
}

fn draw_button(m: &Modal, i: usize, mono: bool, focused: bool) {
    let br = button_rect(m, i);
    let face = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_FACE };
    unsafe {
        gfx::gfx_fill_rect(br.x, br.y, br.w, br.h, face);
        gfx::gfx_rect(br.x, br.y, br.w, br.h, GUI_COLOR_TEXT);
        if !mono {
            gfx::gfx_hline(br.x + 1, br.y + 1, br.w - 2, GUI_COLOR_LIGHT);
            gfx::gfx_hline(br.x + 1, br.y + br.h - 2, br.w - 2, GUI_COLOR_SHADOW);
        }
        let label = button_label(m.buttons, i);
        let lw = label_width(label);
        gfx::kcg_draw_utf8(
            br.x + (br.w - lw) / 2,
            br.y + 2,
            label.as_ptr(),
            GUI_COLOR_TEXT,
            face,
        );
    }
    if focused {
        /* フォーカスは点線矩形 (契約 G6 の DOTTED)。 */
        let fr = Rect::new(br.x + 3, br.y + 3, br.w - 6, br.h - 6);
        let c = if mono { GUI_COLOR_TEXT } else { GUI_COLOR_HIGHLIGHT };
        chrome::dotted_rect(fr.x, fr.y, fr.w, fr.h, c);
    }
}

fn label_width(label: &[u8]) -> i32 {
    let mut n = 0;
    while n < label.len() && label[n] != 0 {
        n += 1;
    }
    (n as i32) * 8
}

/* ================================================================ */
/*  周期の補助                                                       */
/* ================================================================ */

/// アプリの COMMIT (X2) がダイアログに掛かったら描き直す。
pub fn refresh_if_hit(st: &GuiState, r: Rect) -> bool {
    let m = state();
    if !m.used || !m.rect.intersects(&r) {
        return false;
    }
    draw(st, m.rect);
    true
}

/// ダイアログを閉じた後に全面が要るときの目安 (デバッグ用)。
pub fn present_rect(st: &GuiState) {
    let m = state();
    if m.used {
        wm::queue_present(st, m.rect);
    }
}
