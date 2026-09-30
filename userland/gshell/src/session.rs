//! session.rs — SessionAction とトップレベル handoff (契約 V12-S、票 W3 §5〜§7)。
//!
//! **WM の文脈 (X1 / X3 / X4) から `exec_run()` を絶対に呼ばない**のがこの票の
//! 最重要条件。別アプリ起動 / CUI 復帰 / system halt の要求はここに 1 件だけ
//! 溜め、現アプリを `Quit` で終わらせ、`exec_run()` が gshell の top-level へ
//! 戻ってから ([`crate::lib`] の単独ループ) 実行する。
//!
//! ```text
//!   要求        Start メニュー (set_wm_*) / アプリの op 66 (request)
//!    ↓ X1・X3   action を私有状態へ (VFS も exec も cfg 更新もしない)
//!    ↓          現 owner のスロットへ sticky な Quit{reason} を配送
//!   アプリ終了  exec_run が返る → top-level
//!    ↓          LAUNCH → exec_run / SWITCH_CUI → cfg + switch_shell /
//!               SHUTDOWN → halt ループ
//! ```
//!
//! 状態を `GuiState` ではなく**このモジュールの static** に置くのは、
//! `GuiState::reclaim_owner` (owner の窓 / タイマ / スロットの回収) が
//! SessionAction を絶対に消さないことを構造で保証するため (契約 S2 の
//! 「正常 exit / CTRL+STOP / fault kill のいずれでも pending を失わない」)。
//!
//! Quit は制御イベントなのでリング満杯でも捨てない (契約 S5)。スロットごとに
//! `quit_pending` を持ち、`OP_POLL` の返却準備と毎 X3 で空きができるまで
//! 再配送する。**`dropped` には加算しない** (W4 の Modal と同じ仕掛け)。

use crate::ring;
use crate::wm::GuiState;
use os32api::gui::proto::{
    GUI_EV_QUIT, GUI_QUIT_REASON_REPLACE_APP, GUI_QUIT_REASON_SHUTDOWN,
    GUI_QUIT_REASON_SWITCH_CUI, GUI_SESSION_LAUNCH, GUI_SESSION_SHUTDOWN, GUI_SESSION_SWITCH_CUI,
    GUI_SLOT_MAX, OS32_ERR_FULL, OS32_ERR_INVAL,
};

/// LAUNCH の絶対パスに載る最大バイト数 (契約 S4 の `GuiString`)。
pub const PATH_MAX: usize = 255;

/// `Quit` を配ってから応答しないアプリを打ち切るまでの **WM の周回数**
/// (決裁 A3、2026-09-11)。
///
/// 根拠: 待っている間の WM の 1 周は `op_wait` の待ちループも単独ループも
/// 「`wm_cycle` → `sys_halt`」で、`sys_halt` は PIT (100Hz = 10ms) で必ず
/// 起きる。つまり 1 周 = 10ms なので **300 周 ≒ 3 秒**。アプリが走り続けて
/// いる間は `sys_halt` を通らないぶん 1 周が短くなるだけなので、3 秒は
/// この待ちの**上界** (最悪でも 3 秒で畳む) になる。
///
/// 確認ダイアログが既に「保存していない内容は失われます」と警告しているので、
/// 打ち切りに追加の UI は要らない。
pub const QUIT_GRACE_CYCLES: u32 = 300;

/* ================================================================ */
/*  状態                                                             */
/* ================================================================ */

/// スロット 1 本ぶんの sticky な Quit 配送。
#[derive(Clone, Copy)]
struct QuitPend {
    pending: bool,
    /// 立てた時点の owner (スロット再利用の誤配送よけ)。
    owner: i32,
    reason: u8,
    /// `GUI_EV_QUIT` の `window` に載せる窓 (無ければ 0)。
    window: u32,
}

impl QuitPend {
    const NEW: QuitPend = QuitPend { pending: false, owner: 0, reason: 0, window: 0 };
}

pub struct Session {
    /// `GUI_SESSION_*`。0 = 要求なし (契約 S3)。
    action: u8,
    path: [u8; PATH_MAX + 1],
    path_len: usize,
    quit: [QuitPend; GUI_SLOT_MAX],
    /// `Quit` を配ってから打ち切るまでに残っている WM の周回数
    /// (0 = 猶予を数えていない)。[`QUIT_GRACE_CYCLES`] から減る。
    quit_grace: u32,
}

impl Session {
    const NEW: Session = Session {
        action: 0,
        path: [0; PATH_MAX + 1],
        path_len: 0,
        quit: [QuitPend::NEW; GUI_SLOT_MAX],
        quit_grace: 0,
    };
}

struct SessionCell(core::cell::UnsafeCell<Session>);
unsafe impl Sync for SessionCell {}
static SESSION: SessionCell = SessionCell(core::cell::UnsafeCell::new(Session::NEW));

#[inline]
fn s() -> &'static mut Session {
    unsafe { &mut *SESSION.0.get() }
}

/// 保留中の action (`GUI_SESSION_*`、0 = なし)。
#[inline]
pub fn pending_action() -> u8 {
    s().action
}

/// action と path を捨てる (実行した / 実行しないと決めた後)。
pub fn clear() {
    let st = s();
    st.action = 0;
    st.path_len = 0;
    st.path[0] = 0;
    st.quit_grace = 0;
}

/// 打ち切りまでに残っている周回数 (試験と診断用。0 = 数えていない)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn quit_grace_left() -> u32 {
    s().quit_grace
}

/// 保留中の LAUNCH パスを NUL 終端で `out` へ写す。戻り値はバイト数
/// (NUL を含まない)。**action は消さない** — 呼ぶ側が `clear()` する。
pub fn copy_path(out: &mut [u8; PATH_MAX + 1]) -> usize {
    let st = s();
    let mut i = 0;
    while i < st.path_len {
        out[i] = st.path[i];
        i += 1;
    }
    out[i] = 0;
    st.path_len
}

/* ================================================================ */
/*  要求の受理 (X1 / X3)                                             */
/* ================================================================ */

/// `GUI_OP_SESSION_REQUEST` (op 66) を X1 で受ける (契約 S4)。
///
/// **ここでは VFS / exec / cfg を一切触らない。** 検証して私有バッファへ写し、
/// 現 owner へ sticky な `Quit` を積むだけ。戻り値 0 は「受理」であって完了ではない。
pub fn request(st: &mut GuiState, _owner: i32, action: u8, flags: u8, value: &[u8], len: usize) -> i32 {
    if flags != 0 {
        return OS32_ERR_INVAL; /* v1.2 の flags は 0 のみ */
    }
    match action {
        GUI_SESSION_LAUNCH => {
            /* 1〜255B の絶対パスだけ (契約 S4)。 */
            if len == 0 || len > PATH_MAX || value.len() < len || value[0] != b'/' {
                return OS32_ERR_INVAL;
            }
        }
        GUI_SESSION_SWITCH_CUI | GUI_SESSION_SHUTDOWN => {
            if len != 0 {
                return OS32_ERR_INVAL;
            }
        }
        _ => return OS32_ERR_INVAL,
    }
    if s().action != 0 {
        return OS32_ERR_FULL; /* 既存 pending は上書きしない (契約 S3) */
    }
    set_action(action, value, len);
    /* K5b-W: `LAUNCH` は増やすだけ。畳むのは GUI そのものを畳む 2 つだけ。 */
    if action != GUI_SESSION_LAUNCH {
        arm_quit(st, reason_of(action));
    }
    0
}

/// Start メニュー / WM 内蔵ダイアログからの設定 (契約 S3 の 5.1)。
/// 戻り値は `request` と同じ (0 / `OS32_ERR_FULL` / `OS32_ERR_INVAL`)。
pub fn set_wm(st: &mut GuiState, action: u8, value: &[u8]) -> i32 {
    let mut len = 0;
    while len < value.len() && value[len] != 0 {
        len += 1;
    }
    request(st, 0, action, 0, value, len)
}

/// `/usr/bin/...` を起動要求にする近道 (Start の Programs / File Manager / Run...)。
#[inline]
pub fn set_wm_launch(st: &mut GuiState, path: &[u8]) -> i32 {
    set_wm(st, GUI_SESSION_LAUNCH, path)
}

fn set_action(action: u8, value: &[u8], len: usize) {
    let st = s();
    st.action = action;
    let n = if len > PATH_MAX { PATH_MAX } else { len };
    let mut i = 0;
    while i < n {
        st.path[i] = value[i];
        i += 1;
    }
    st.path[n] = 0;
    st.path_len = n;
}

#[inline]
fn reason_of(action: u8) -> u8 {
    match action {
        GUI_SESSION_LAUNCH => GUI_QUIT_REASON_REPLACE_APP,
        GUI_SESSION_SWITCH_CUI => GUI_QUIT_REASON_SWITCH_CUI,
        _ => GUI_QUIT_REASON_SHUTDOWN,
    }
}

/* ================================================================ */
/*  sticky な Quit の配送 (契約 S5)                                  */
/* ================================================================ */

/// 使用中の全スロット (= 生きている GUI アプリ。v1.3 は最大 4 本) へ
/// `quit_pending` を立て、すぐ 1 回配送を試みる。
///
/// **v1.3 (K5b-W) の読み替え**: 呼ぶのは `SWITCH_CUI` / `SHUTDOWN` だけ。
/// `LAUNCH` は「アプリを 1 本増やす」意味になったので、誰も終わらせない
/// (S2 の手順 2 「現 owner へ `Quit{REPLACE_APP}`」は 4 本前提では要らない)。
fn arm_quit(st: &mut GuiState, reason: u8) {
    let mut i = 0;
    while i < GUI_SLOT_MAX {
        if st.slots[i].used {
            let owner = st.slots[i].owner;
            let q = &mut s().quit[i];
            q.pending = true;
            q.owner = owner;
            q.reason = reason;
            q.window = front_window_of(st, owner);
        }
        i += 1;
    }
    /* 票 K7 受入 I3: スロットを持たない被追跡アプリ (端末から起動した CUI
     * プログラム。`kbd_getchar` で `WAIT_KEY` に park 中) には `Quit` を積む
     * 先が無いので、上の輪は 1 本も触れていない。待っても自分から終われない
     * ので**猶予を待たずに** `exec_kill` を予約する (実行は top-level の
     * `multiapp::drain_top_level`)。これが無いとスロットと per-app の物理
     * ページを漏らしたまま CUI へ戻る。 */
    crate::multiapp::request_kill_slotless(st);
    /* 決裁 A3: ここから猶予を数え始める。順序は
     * 「全アプリへ Quit → 待ち → 残りを kill → 全回収 → SessionAction 実行」。
     * 最後の 2 つは `ready_to_run` (全回収の確認) と top-level の
     * `multiapp::resume_one` (予約の実行) が担う。 */
    s().quit_grace = QUIT_GRACE_CYCLES;
    retry_all(st);
}

/// 猶予を 1 周ぶん進める (決裁 A3)。切れたら生きているアプリ全部に
/// `exec_kill` を予約する — 実行は top-level (`multiapp::resume_one`)。
fn tick_quit_grace(st: &GuiState) {
    if s().quit_grace == 0 {
        return;
    }
    /* action を実行した / 全員が Quit に応じて回収された。数える必要はない。 */
    if s().action == 0 || !owner_active(st) {
        s().quit_grace = 0;
        return;
    }
    s().quit_grace -= 1;
    if s().quit_grace == 0 {
        crate::multiapp::request_kill_all();
    }
}

/// この owner が持つ最前面の窓 id (無ければ 0)。
fn front_window_of(st: &GuiState, owner: i32) -> u32 {
    let mut z = st.z_count;
    while z > 0 {
        z -= 1;
        let i = st.z_at(z);
        if st.windows[i].used && st.windows[i].owner == owner {
            return st.windows[i].id(i);
        }
    }
    0
}

/// 1 スロットぶんの再配送。満杯なら pending を残す (**`dropped` に加算しない**)。
fn deliver(st: &mut GuiState, slot_no: usize) {
    if slot_no >= GUI_SLOT_MAX {
        return;
    }
    let (pending, owner, reason, window) = {
        let q = &s().quit[slot_no];
        (q.pending, q.owner, q.reason, q.window)
    };
    if !pending {
        return;
    }
    /* スロットが別の owner へ渡っていたら配らない (誤配送よけ)。 */
    if !st.slots[slot_no].used || st.slots[slot_no].owner != owner {
        s().quit[slot_no] = QuitPend::NEW;
        return;
    }
    let ev = ring::ev_simple(GUI_EV_QUIT, reason, window);
    if ring::append(st, slot_no, &ev) {
        s().quit[slot_no] = QuitPend::NEW;
    }
}

/// `OP_POLL` の返却準備で呼ぶ再試行 (契約 S5)。
#[inline]
pub fn retry_pending(st: &mut GuiState, slot_no: usize) {
    deliver(st, slot_no);
}

/// 全スロットの再試行。
fn retry_all(st: &mut GuiState) {
    let mut i = 0;
    while i < GUI_SLOT_MAX {
        deliver(st, i);
        i += 1;
    }
}

/// X3 の周期ごとに呼ぶ再試行 (契約 S5) と、`Quit` の猶予の歩進 (決裁 A3)。
pub fn x3_cycle(st: &mut GuiState) {
    retry_all(st);
    /* 票 K7 受入 I3: 畳む action が保留の間は、スロット無しの被追跡アプリの
     * kill 予約を毎周入れ直す (予約済みなら何もしない)。`drain_top_level` は
     * `exec_kill` が失敗しても予約を落とすので、入れ直さないと `owner_active`
     * が永久に真のまま = CUI へ戻れない、になりうる。 */
    match s().action {
        GUI_SESSION_SWITCH_CUI | GUI_SESSION_SHUTDOWN => {
            crate::multiapp::request_kill_slotless(st);
        }
        _ => {}
    }
    tick_quit_grace(st);
}

/// owner が回収された (正常終了 / CTRL+STOP / fault kill)。
/// **SessionAction は消さない** — 消すのは top-level が実行した後だけ (契約 S2)。
pub fn reclaim_owner(owner: i32) {
    let st = s();
    let mut i = 0;
    while i < GUI_SLOT_MAX {
        if st.quit[i].pending && st.quit[i].owner == owner {
            st.quit[i] = QuitPend::NEW;
        }
        i += 1;
    }
}

/* ================================================================ */
/*  top-level の判定                                                 */
/* ================================================================ */

/// スロット `slot_no` に配れていない sticky Quit が残っているか
/// (D11-1 の入力群の 2 つ目。`crate::multiapp::input_ready` が使う)。
pub fn quit_pending(slot_no: usize) -> bool {
    slot_no < GUI_SLOT_MAX && s().quit[slot_no].pending
}

/// いま top-level でこの action を実行してよいか (契約 S2 の 4 本前提の
/// 読み替え、K5b-W)。
///
/// - `LAUNCH` … 「1 本増やす」なので他のアプリが生きていても実行してよい。
///   `exec_start` は塞がないので、増えた 1 本は最初の `OP_WAIT` で park する。
/// - `SWITCH_CUI` / `SHUTDOWN` … GUI そのものを畳むので、全アプリの回収を待つ
///   (armed した `Quit` で 1 本ずつ終わる)。
pub fn ready_to_run(st: &GuiState) -> bool {
    match pending_action() {
        0 => false,
        GUI_SESSION_LAUNCH => true,
        _ => !owner_active(st),
    }
}

/// まだ外部アプリが生きているか (窓・スロット・**スロット無しの被追跡アプリ**)。
/// SessionAction の実行は「回収済み」を確認してからでなければならない (§7.1)。
///
/// 票 K7 受入 I3: 窓もスロットも持たない CUI プログラム (端末から起動して
/// `kbd_getchar` で `WAIT_KEY` に park 中) は `multiapp` の表にしか現れない。
/// ここで数えないと、その 1 本を残したまま `SWITCH_CUI` / `SHUTDOWN` が成立し、
/// AppSlot と per-app の物理ページが漏れる (次に GUI へ入ると 3 本しか立たない)。
pub fn owner_active(st: &GuiState) -> bool {
    let mut i = 0;
    while i < GUI_SLOT_MAX {
        if st.slots[i].used {
            return true;
        }
        i += 1;
    }
    let mut w = 0;
    while w < st.windows.len() {
        if st.windows[w].used {
            return true;
        }
        w += 1;
    }
    crate::multiapp::slotless_live(st)
}
