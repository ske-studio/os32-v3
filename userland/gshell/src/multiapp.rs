//! multiapp.rs — GUI アプリ 4 本の同時実行、WM (gshell) 側 (票 K5b-W)。
//!
//! カーネル (K5b-K、KAPI v44) が持つのは**機構だけ** — `exec_start` /
//! `exec_park` / `exec_resume` / `exec_kill` と「park は `OP_WAIT` 由来の
//! フレームからだけ、resume は印のあるフレームだけ」というゲート。
//! **誰を次に起こすかを決めるのはここ** (設計 D11-5: カーネルは順番を決めない)。
//!
//! ```text
//!   top-level (lib.rs の単独ループ、owner 1)
//!     ├ exec_start(path)  ── アプリが最初に park するまで戻らない
//!     └ resume_one()      ── pick() で 1 本選んで exec_resume(id, wait_ret)
//!
//!   アプリの gui_call(OP_WAIT) の中 (handler.rs の op_wait)
//!     └ maybe_park()      ── should_park() が真なら exec_park() (戻らない)
//! ```
//!
//! `exec_park` を呼ぶ点は [`maybe_park`] の 1 か所だけで、そこは
//! `op_wait` のループ先頭 = WM の 1 周が終わって状態が整っている地点。
//! ここから longjmp するので、書きかけの WM 状態があると宙に浮く。
//!
//! 規則 (D11-3) は `tools/tests/multiapp_model_host.c` の `ma_should_park` /
//! `ma_pick` と 1 対 1 に対応させてある:
//!
//! | 模型 | ここ |
//! |---|---|
//! | `MA_INPUT_STREAK_MAX` | [`INPUT_STREAK_MAX`] |
//! | `MA_STARVE_BOUND` | [`STARVE_BOUND`] |
//! | `ma_should_park` | [`should_park`] |
//! | `ma_pick` / `ma_pick_group` / `ma_round_remaining` | [`pick`] / [`pick_group`] / [`round_remaining`] |
//! | `ma_resume` の `turn_used` / `last_run` / `input_streak` | [`mark_resumed`] |
//! | `ma_launch` の同上 | [`on_start`] |
//!
//! `input_ready` / `derived_ready` は模型では試験が直接立てていたが、ここは
//! **実物の WM 状態から算出する** (D11-1 の棚卸しをそのまま写した)。
//!
//! 票 K7 (KAPI v47) で park 点が 2 つになった。`kbd_getchar` で止まった
//! CUI プログラムは `APP_STATE_WAIT_KEY` で、起床の理由は注入リングの
//! 未読だけ ([`key_ready`] / 模型の `ma_key_ready`)。規則そのもの (D11-3)
//! と上界 (30) は 1 つも変えていない。
//!
//! 票 T8 §7 D8 で park 点が 3 つになった。`kbd_trygetchar` をポーリングする
//! 全画面 GFX プログラムは `APP_STATE_WAIT_POLL` で、**常に ready だが
//! 優先度は最下位** ([`poll_ready`] / [`pick_poll`] / 模型の `ma_pick_poll`)。
//! 入力群も導出群も空の周にしか選ばないので、ここでも D11-3 の規則と
//! 上界 (30) は動かない (最下位 = 他が ready な周は候補にならない)。
//!
//! 票 T9 D3 / D5 / D8 で 3 か所が変わった。どれも**この WM の領分**で、
//! カーネル (KAPI v49 の要求表) は順を決めない:
//!
//! - [`should_park`] の (a) と [`pick_poll`] の門に `launch_pending()` を足した
//!   (`launch_take` も owner 1 専用なので、譲らないと取りに行けない)。
//! - `WAIT_POLL` 群は **巡回 + 同じ tick に 1 回** ([`pick_poll`])。模型
//!   `ma_pick_poll` (`tools/tests/multiapp_model_host.c`) はまだ T8 の
//!   「ID 昇順」のままで、この 1 点だけ 1 対 1 ではない — 巡回と tick の
//!   検査は `userland/gshell/host/wm_tests.rs` の T9-W 検査 6 / 7 が持つ。
//! - CTRL+STOP の宛先は要求表の連鎖の**末尾** ([`abort_target`] / [`chain_tail`])。

use crate::wm::GuiState;
use crate::{damage, ring, session, timer};
use os32api::gui::proto::{GUI_MAX_WINDOWS, OS32_ERR_AGAIN};

/* ================================================================ */
/*  定数 (カーネル exec/appslot.h と同じ値)                          */
/* ================================================================ */

/// シェル帯 (CUI シェル / gshell)。`kernel/gui.h` の `GUI_SHELL_OWNER` と同値。
pub const APP_ID_SHELL: i32 = 1;
/// 同時に生きる非シェル ID の本数 (`APP_MAX_APPS`)。
pub const MAX_APPS: usize = 4;
pub const APP_ID_MIN: i32 = 2;
pub const APP_ID_MAX: i32 = APP_ID_MIN + MAX_APPS as i32 - 1;

/// `exec_app_state` が返す「止めてある」(`APP_STATE_PARKED`)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub const APP_STATE_PARKED: i32 = 2;
/// `exec_app_state` が返す「kbd 待ち」(`APP_STATE_WAIT_KEY`、KAPI v47)。
/// カーネル `exec/appslot.h` と同じ値で、**既存の 0/1/2 の意味は動かない**
/// (票 K7 §5 指摘 C: 値の追加は互換)。GUI 中に `kbd_getchar` を呼んだ
/// CUI プログラムはここで止まる — 第 2 の park 点 (票 K7 D1)。
pub const APP_STATE_WAIT_KEY: i32 = 3;
/// `exec_app_state` が返す「ポーリングの協調 yield で止めてある」
/// (`APP_STATE_WAIT_POLL`、票 T8 §7 D8)。GUI 中に `kbd_trygetchar` /
/// `kbd_trygetkey` / `kbd_has_key` を回す全画面 GFX プログラムを、カーネルが
/// tick に 1 回だけ止めて WM に譲らせる第 3 の park 点。
///
/// **カーネル側 (`exec/appslot.h` の定義と park / resume) は別票 T8-3 K** で、
/// そこに入るまでヘッダに無いのでここはリテラル 4 を持つ (入った後も同値。
/// 既存の 0〜3 の意味は動かないので値の追加は互換 — 票 K7 §5 指摘 C と同じ)。
pub const APP_STATE_WAIT_POLL: i32 = 4;
/// `exec_app_state` が返す「居ない」(`APP_STATE_FREE`)。`exec_kill` が連鎖の
/// 子孫ごと畳んだ後、どの ID が空いたかを数えるのに使う (票 T9 D8)。
pub const APP_STATE_FREE: i32 = 0;

/* ---- 起動要求表 (KAPI v49、票 T9 D3) ---------------------------------- */
/* ワイヤ側の正典は `sdk/include/os32/os32_kapi_shared.h` ([C4] の 3 層管理)。
 * gshell は no_std の Rust なので、`os32x.rs` の `FLAG_CUI_ONLY` と同じく
 * ここへ**同値で**写す (生成器の対象外)。 */
/// `launch_take` の `kind` = 外部プログラムの起動 (`buf` に cmdline)。
pub const LAUNCH_KIND_LAUNCH: i32 = 1;
/// `launch_take` の `kind` = 畳む要求 (`arg` = 畳む ID)。
pub const LAUNCH_KIND_KILL: i32 = 2;
/// `launch_take` に渡すバッファの下限 (`cap < 256` は `OS32_ERR_INVAL`)。
pub const LAUNCH_CMDLINE_MAX: usize = 256;
/// 要求者が退場した後の印 (孤児回収)。`kind` は必ず KILL だが、票 D3 (4) の
/// 「孤児回収も KILL として同じに扱う」を読めるようにここでも見る。
pub const LAUNCH_REQ_ORPHAN: i32 = -1;

/// 走っているアプリが「自分に入力がある」を理由に turn を据え置ける**連続**
/// `OP_WAIT` 回数 (D11-3)。値を `MAX_APPS` に合わせたのは「アプリの数だけは
/// 続けて持てる」という説明できる線を引くためで、それ以上の意味は無い
/// (D11-7: 打鍵の連続を取りこぼさない最小値は未実測。G9 で決め直してよい)。
pub const INPUT_STREAK_MAX: u32 = MAX_APPS as u32;

/// ready なアプリが park した `OP_WAIT` から再開までに待つ最悪回数
/// (D11-3a、`MA_STARVE_BOUND` と同じ式)。前提 3 つ付きの上界で、
/// ケース 15 (`N`=2 → 10) と 16 (`N`=4 → 30) がちょうど届く。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub const STARVE_BOUND: u32 = (2 * MAX_APPS as u32 - 2) * (INPUT_STREAK_MAX + 1);

/* ================================================================ */
/*  状態 (WM の私有。カーネルは 1 つも持たない — D11-5)              */
/* ================================================================ */

#[derive(Clone, Copy)]
struct App {
    /// この ID を `exec_start` で立てて、まだ `gui_owner_exit` を受けていない。
    alive: bool,
    /// このラウンドで turn を 1 回使った (D11-3 の有界性)。
    turn_used: bool,
    /// park した `OP_WAIT` の期限 (D11-1 の「`OP_WAIT` の期限が来た」)。
    /// カーネルも他のアプリも持てないので WM が控える。resume で消す。
    has_deadline: bool,
    deadline: u32,
    /// top-level で `exec_kill` する予約 (止めてあるアプリを畳む、D4)。
    kill_req: bool,
    /// **この 1 本を最後にポーリング群から起こした tick** (票 T9 D5、
    /// 実装レビュー 1 の blocker 2)。`pick` の時刻ではなく
    /// [`mark_resumed`] が `get_tick` を読んだ時刻 = **実際に再開した tick**
    /// を持つ。選んでから再開するまでに PIT が進んでも、記録はその周に
    /// 紐づくので、同じ tick の 2 回目が通らない。
    poll_tick: u32,
    /// [`App::poll_tick`] が有効か (tick 0 と「まだ起こしていない」を分ける)。
    poll_tick_valid: bool,
}

impl App {
    const NEW: App = App {
        alive: false,
        turn_used: false,
        has_deadline: false,
        deadline: 0,
        kill_req: false,
        poll_tick: 0,
        poll_tick_valid: false,
    };
}

pub struct Multi {
    apps: [App; MAX_APPS],
    /// 直前に走ったアプリ ID (巡回の起点)。0 = 無し。**resume 側で進める**
    /// (D11-7 の最後: 模型が resume 側なので実装も揃える)。
    last_run: i32,
    /// 入力優先で turn を据え置いた連続回数。
    input_streak: u32,
    /// いま走っているアプリ ID (0 = WM top-level)。`pick` はこれが 0 の
    /// ときしか選ばない — 走っている本人を「起こす」ことはできない。
    running: i32,
    /// 直前に `snd_focus` へ渡した owner。同じ値なら呼ばない (G10 の「1 回だけ」)。
    snd_owner: i32,
    /// `exec_start` を呼んでから戻るまで。**起動したてのアプリはまだ表に
    /// 居ない** (表に載る id は `exec_start` の戻り値そのもの) ので、この間だけ
    /// 走っている ID を表へ迎える ([`should_park`] の adopt)。
    pending_start: bool,
    /// top-level で `exec_abort_clear` する予約 (決裁 A1)。CTRL+STOP の宛先が
    /// フォーカス窓の**別の**アプリだったとき、走っている本人が負っている
    /// 要求を降ろす。KAPI は owner 1 からしか通らない (K5c) ので予約にする。
    abort_clear_req: bool,
    /* ---- ポーリング群の公平性 (票 T9 D5) ---------------------------- *
     * 票は「状態は `GuiState` に持つ」と書いているが、譲り合いの控え
     * (`last_run` / `input_streak` / `turn_used`) は全部この私有の表にある。
     * `GuiState` は窓とスロットの表で、`mocks::init` → `multiapp::reset()` で
     * 一緒に洗われるのもこちら — 同じ種類の控えを 2 か所に散らさない。 */
    /// ポーリング群の巡回の起点 = 前回 `pick_poll` で起こした ID (0 = 無し)。
    /// `last_run` とは**別に持つ**: ポーリング群はラウンドの turn を数えない
    /// (最下位なので他が ready な周は候補にならない) ので、入力群 / 導出群の
    /// 巡回と起点を共有すると互いの順を乱す。
    poll_last: i32,
    /// 直前の [`pick`] が**ポーリング群から**選んだ ID (0 = それ以外)。
    /// [`mark_resumed`] が「この起床を tick の数えに載せるか」を決める材料で、
    /// [`pick`] の入口で必ず 0 に戻すので前の周の残骸は効かない。
    poll_choice: i32,
}

impl Multi {
    const NEW: Multi = Multi {
        apps: [App::NEW; MAX_APPS],
        last_run: 0,
        input_streak: 0,
        running: 0,
        snd_owner: APP_ID_SHELL,
        pending_start: false,
        abort_clear_req: false,
        poll_last: 0,
        poll_choice: 0,
    };
}

struct MultiCell(core::cell::UnsafeCell<Multi>);
unsafe impl Sync for MultiCell {}
static MULTI: MultiCell = MultiCell(core::cell::UnsafeCell::new(Multi::NEW));

#[inline]
fn m() -> &'static mut Multi {
    unsafe { &mut *MULTI.0.get() }
}

/// ID → 表の添字。シェル帯 / 範囲外は `None`。
#[inline]
fn idx(id: i32) -> Option<usize> {
    if id < APP_ID_MIN || id > APP_ID_MAX {
        None
    } else {
        Some((id - APP_ID_MIN) as usize)
    }
}

/// この ID を WM が握っているか (= `exec_start` で立てた GUI アプリ)。
/// CUI の入れ子の子 (`exec_run` で立った) はここに載らない — park できない
/// ので譲り合いの対象にしない (カーネルの `appslot_park_check` と同じ線)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn is_tracked(id: i32) -> bool {
    match idx(id) {
        Some(i) => m().apps[i].alive,
        None => false,
    }
}

/// 生きているアプリの本数。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn live_count() -> usize {
    let mm = m();
    let mut n = 0;
    let mut i = 0;
    while i < MAX_APPS {
        if mm.apps[i].alive {
            n += 1;
        }
        i += 1;
    }
    n
}

/// 全部忘れる (ホスト試験の前処理。ゲストでは使わない)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn reset() {
    *m() = Multi::NEW;
}

/* ================================================================ */
/*  生存の出入り                                                     */
/* ================================================================ */

/// `exec_start` が `>0` を返した (アプリが最初の `OP_WAIT` で park した)。
/// 模型 `ma_launch` と同じく turn を 1 つ使った状態で表に載せる — 立った
/// 直後に走っているので、そのラウンドの追加 turn にはならない (D11-3a)。
pub fn on_start(id: i32) {
    adopt_running(id);
    m().running = 0; /* もう park 済み (exec_start が戻ったのがその証拠) */
}

/// 起動したてのアプリを表に載せる (まだ `exec_start` は戻っていない = 走っている)。
/// 模型 `ma_launch` と同じく turn を 1 つ使った状態にする — 立った直後に走って
/// いるので、そのラウンドの追加 turn にはならない (D11-3a)。
pub fn adopt_running(id: i32) {
    let i = match idx(id) {
        Some(i) => i,
        None => return,
    };
    let mm = m();
    mm.apps[i] = App::NEW;
    mm.apps[i].alive = true;
    mm.apps[i].turn_used = true;
    mm.last_run = id;
    mm.input_streak = 0;
    mm.running = id;
}

/// `exec_start` を呼ぶ**直前**に立てる。`exec_start` は「アプリが最初に park
/// する」まで戻らない (決裁 D9-5) ので、その間に走っている ID は表に載っていない。
/// この印が無いと [`should_park`] が「未追跡だから譲らない」で弾いてしまい、
/// **2 本目以降が永久に park できず `exec_start` が戻らない** (= 1 本目が二度と
/// 起こされない / top-level へ戻れないので次の `LAUNCH` も実行できない)。
///
/// 限界: この間に「起動したアプリの CUI 入れ子の子」が `OP_INIT` まで済ませて
/// `OP_WAIT` に入ると、そちらを迎えてしまう。カーネルは `!a->gui` で park を
/// 拒む (`appslot_park_check`) ので実害は `ring3_park_reject_count` が 1 増える
/// ことだけで、その ID は `gui_owner_exit` で表から落ちる。
pub fn begin_start() {
    m().pending_start = true;
}

/// `exec_start` が戻った。`rc > 0` = park 済みの app_id、`0` = park 前に終了、
/// `< 0` = 起動しなかった。
pub fn end_start(rc: i32) {
    let mm = m();
    mm.pending_start = false;
    if rc > 0 {
        /* 普通は `should_park` の adopt で既に載っている。載っていない
         * (park が WM 以外の理由で成立した) ときのための保険。 */
        if idx(rc).map_or(false, |i| !mm.apps[i].alive) {
            on_start(rc);
        }
        m().running = 0; /* park 済み = 走っていない */
    } else {
        /* 0 = 終了済み (回収は `gui_owner_exit` 経由で済んでいる)、
         * 負 = 何も立っていない。adopt の残骸があれば落とす。 */
        mm.running = 0;
    }
}

/// `GUI_OP_OWNER_EXIT` (正常終了 / fault / CTRL+STOP / `exec_kill` のどれでも
/// カーネルはここを通る)。**その ID の 1 本分だけ**を表から落とす。
pub fn on_owner_exit(id: i32) {
    let i = match idx(id) {
        Some(i) => i,
        None => return,
    };
    let mm = m();
    mm.apps[i] = App::NEW;
    if mm.running == id {
        mm.running = 0;
    }
    if mm.last_run == id {
        /* 巡回の起点は残しておいてよい (ID は空きになるので飛ばされる)。
         * ただし表を消したあとに同じ ID が再利用されたとき、直前に走った
         * 扱いになると 1 個ずれるので落とす。 */
        mm.last_run = 0;
    }
    /* ポーリング群の控えも同じ理由で落とす (票 T9 D5)。「この tick で
     * 起こし済み」を残すと、再利用された同じ ID が 1 tick 飛ばされる。 */
    if mm.poll_last == id {
        mm.poll_last = 0;
    }
    if mm.poll_choice == id {
        mm.poll_choice = 0;
    }
    /* 「最後に起こした tick」は `App::NEW` で一緒に落ちている (上の 1 行)。
     * 再利用された同じ ID が 1 tick 飛ばされないための後始末。 */
}

/// 止めてあるアプリを top-level で畳む予約 (D4 の「止めてあるアプリの Quit」)。
pub fn request_kill(id: i32) {
    if let Some(i) = idx(id) {
        if m().apps[i].alive {
            m().apps[i].kill_req = true;
        }
    }
}

/// この添字の 1 本が「生きているのにスロットを持たない」か。
///
/// **KAPI (`exec_app_state`) を見ない**。この判定の呼び出し元には X1 が
/// 含まれる (`session::request` = op 66) ので、契約 T8 で KAPI を呼べない。
/// 被追跡かつスロット無しなら状態は `WAIT_KEY` / `PARKED` / 起動途中の
/// どれかで、**どれも `Quit` を積む先 (スロットのリング) が無い**点は同じ。
#[inline]
fn is_slotless(st: &GuiState, i: usize) -> bool {
    m().apps[i].alive && st.slot_of_owner(APP_ID_MIN + i as i32).is_none()
}

/// スロットを持たない被追跡アプリが 1 本でも生きているか (票 K7 受入 I3)。
///
/// 端末から起動した CUI プログラム (`kbd_getchar` で `APP_STATE_WAIT_KEY` に
/// park) は `OP_INIT` を通らないのでスロットも窓も持たない。
/// [`session::owner_active`] がスロットと窓しか見ないと、この 1 本を残したまま
/// `SWITCH_CUI` / `SHUTDOWN` が成立し、AppSlot と per-app の物理ページが漏れる。
pub fn slotless_live(st: &GuiState) -> bool {
    let mut i = 0;
    while i < MAX_APPS {
        if is_slotless(st, i) {
            return true;
        }
        i += 1;
    }
    false
}

/// スロットを持たない被追跡アプリ全部に `exec_kill` を予約する (票 K7 受入 I3)。
/// 戻り値は新しく予約した本数。
///
/// 決裁 A3 の「`Quit` を配ってから `QUIT_GRACE_CYCLES` 周待って kill」は
/// **配れる相手** (= スロットのある GUI アプリ) の話。スロットが無い 1 本には
/// `Quit` を積む先が無いので待っても何も起きない — 猶予を待たずに畳む。
/// 実行は top-level の [`drain_top_level`] で、成功した ID はそこで
/// [`forget`] される。
pub fn request_kill_slotless(st: &GuiState) -> usize {
    let mut n = 0;
    let mut i = 0;
    while i < MAX_APPS {
        if is_slotless(st, i) && !m().apps[i].kill_req {
            m().apps[i].kill_req = true;
            n += 1;
        }
        i += 1;
    }
    n
}

/// 生きているアプリ全部に `exec_kill` を予約する (決裁 A3 の「残りを kill」)。
/// 戻り値は予約した本数。Quit に応答した本は `gui_owner_exit` で表から落ちて
/// いるので、ここには残らない = 畳まれない。
pub fn request_kill_all() -> usize {
    let mm = m();
    let mut n = 0;
    let mut i = 0;
    while i < MAX_APPS {
        if mm.apps[i].alive {
            mm.apps[i].kill_req = true;
            n += 1;
        }
        i += 1;
    }
    n
}

/// top-level (owner 1) でしか実行できない予約が溜まっているか。
///
/// `exec_abort_clear` も `exec_kill` も owner 1 からしか通らない (K5c の注記。
/// `op_wait` の中は owner = アプリ ID なので `OS32_ERR_INVAL`)。予約が溜まって
/// いる間は走っているアプリに譲らせて top-level へ戻す — [`should_park`] の (a)。
/// ゲストの判断は [`should_park`] の中で `has_top_level_work` を直に見るので、
/// この公開版を呼ぶのは試験だけ。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn pending_top_level_work() -> bool {
    has_top_level_work(m())
}

fn has_top_level_work(mm: &Multi) -> bool {
    if mm.abort_clear_req {
        return true;
    }
    let mut i = 0;
    while i < MAX_APPS {
        if mm.apps[i].alive && mm.apps[i].kill_req {
            return true;
        }
        i += 1;
    }
    false
}

/// CTRL+STOP の宛先がフォーカス窓の**別の**アプリだったとき (契約 T6、決裁 A1)。
///
/// カーネルは IRQ1 で「いま走っているアプリ」にしか要求を立てられないので、
/// そのままにすると**意図しない 1 本が死ぬ**。走っている本人の要求を降ろし
/// (`exec_abort_clear`)、フォーカス窓の owner を畳む (`exec_kill`)。どちらも
/// owner 1 からしか呼べないので、ここでは**予約するだけ**。
/// [`should_park`] が予約を見て譲らせ、top-level の [`drain_top_level`] が実行する。
/// top-level (owner 1) で受けた CTRL+STOP を**その場で実行する**
/// (票 T9 D8、実機受入 S6 の修正 2026-09-13)。呼ぶのは単独ループの
/// [`crate::top_level_abort`] だけ。戻り値は畳んだ ID (0 = 何も畳まなかった)。
///
/// `redirect_abort` (予約) ではなく直接実行にしてあるのは 2 つの理由:
///
/// - 予約 (`request_kill`) は **WM の表に載っている ID** にしか積めない。連鎖の
///   末尾が何かの拍子に表から落ちていると、CTRL+STOP が黙って消える。
/// - ここは既に owner 1 の文脈なので、`exec_abort_clear` も `exec_kill` も
///   その場で通る (`drain_top_level` と同じ地点)。
///
/// `exec_abort_clear()` を**必ず先に**呼ぶ (決裁 A1 と同じ順序)。IRQ1 は宛先を
/// 選べないので、走っていた別の 1 本に要求が載っていることがある — 降ろさずに
/// 畳むと、その 1 本が次の syscall の出口で巻き添えで死ぬ。
pub fn abort_at_top_level(st: &GuiState) -> i32 {
    /* `op_wait` の中で見た周は `handler.rs` が [`redirect_abort`] で予約を
     * 積んでいて、同じ 1 周の [`drain_top_level`] が実行する。ここで重ねると
     * 連鎖の末尾を 2 本ぶん畳む。 */
    if m().abort_clear_req {
        return 0;
    }
    let target = abort_target(st);
    /* SAFETY: KAPI の関数表は `os32_init` が据えた有効なポインタ。
     * `exec_abort_clear` は引数なし・戻りは値で、owner 1 から呼んでいる。 */
    unsafe {
        (os32api::api().exec_abort_clear)();
    }
    if target == 0 || target == APP_ID_SHELL {
        /* 宛先が居ない (窓もスロットも無い) / シェル帯。要求を降ろしただけ。 */
        return 0;
    }
    kill_for_request(target);
    target
}

/// CTRL+STOP の宛先が**走っている本人** (または宛先なし) だったときの予約
/// (票 T9 受入 S6 の 3 回目、実機 2026-09-13)。
///
/// 以前は `handler.rs` がこの枝で `break` し、アプリを syscall の出口へ戻して
/// カーネルの `abort_req` に畳ませていた。ところが IRQ1 の着地点で経路が
/// 分かれる:
///
/// | IRQ1 が着地した時点 | カーネルの `abort_req` | 結果 |
/// |---|---|---|
/// | アプリが `op_wait` の中 | 立つ | 出口で畳まれる (成功) |
/// | アプリが自分の処理を CPL=3 で走らせている最中 | **立たない** (暴走ではない) | 出口に要求が無く**消える** |
///
/// 実機は 3 回中 2 回が下の行に当たった。**カーネルの要求に頼らず**、
/// 別アプリ宛て ([`redirect_abort`]) と同じ予約に統一して、top-level の
/// [`drain_top_level`] が `exec_abort_clear` → `exec_kill` を行う。
/// `exec_abort_clear` が先なので、上の行で立っていた要求も巻き添えにならない
/// (決裁 A1 の順序)。`WAIT` で park した GUI アプリを `exec_kill` で畳めるのは
/// Start → CUI の [`request_kill_all`] と同じ。
///
/// 宛先が無い周 (窓が 1 枚も無い = `abort_target` が 0) は**取り消しだけ**
/// 予約する — 誰も死なないが、カーネルに載ったかもしれない要求は降ろす。
pub fn reserve_abort_self(st: &GuiState, cur: i32) {
    m().abort_clear_req = true;
    if abort_target(st) == cur {
        request_kill(cur);
    }
}

pub fn redirect_abort(st: &GuiState, cur: i32) {
    let f = abort_target(st);
    if f == 0 || f == cur {
        /* 呼ぶ側 (`abort_targets_current`) が [`reserve_abort_self`] へ
         * 振り分けている経路。全画面の top-level (`cur` = 0) から来て
         * `f == 0` のときだけここに落ちる = 宛先も本人も居ない。 */
        return;
    }
    m().abort_clear_req = true;
    /* フォーカス窓の owner が WM の握るアプリでなければ (シェル帯の窓など)
     * 畳む相手は居ない。取り消しだけ行う = 誰も死なない。 */
    request_kill(f);
}

/* ================================================================ */
/*  起床条件 (D11-1 の棚卸しを実物から算出)                          */
/* ================================================================ */

/// 注入リング (`kbd_inject`) の未読バイト数 (KAPI v47、誰でも呼べる)。
#[inline]
fn kbd_pending() -> u32 {
    unsafe { (os32api::api().kbd_inject_pending)() }
}

/// カーネルが持つこの ID の状態 (`exec_app_state`)。
#[inline]
fn app_state(id: i32) -> i32 {
    unsafe { (os32api::api().exec_app_state)(id) }
}

/// 起動要求表 (KAPI v49) に取りに来てほしい要求が積まれているか (票 T9 D3 (2))。
///
/// `launch_take` は **owner 1 (WM top-level) 専用**なので、走っているアプリが
/// park しない限り取りに行く道が無い。`launch_pending` は誰でも呼べるので、
/// [`should_park`] の (a) と [`pick_poll`] の門で直に見る。
#[inline]
fn launch_work_pending() -> bool {
    /* SAFETY: KAPI の関数表は `os32_init` が入口で据えた有効なポインタ。
     * 引数も戻りもポインタを持たず、`launch_pending` は誰でも呼べる。 */
    unsafe { (os32api::api().launch_pending)() > 0 }
}

/// いまの tick (`get_tick`)。ポーリング群の「同じ tick に 2 回起こさない」
/// (票 T9 D5) の基準にだけ使う — 待ちは従来どおり `sys_halt` で、
/// ここで回して待つことはしない ([V3] の get_tick スピン禁止と同じ線)。
#[inline]
fn tick_now() -> u32 {
    /* SAFETY: 同上。`get_tick` は引数なし・戻りは値。 */
    unsafe { (os32api::api().get_tick)() }
}

/// 鍵待ち群: `kbd_getchar` で止まっていて (`WAIT_KEY`)、注入リングに
/// 未読がある (票 K7 §5 指摘 C の `ready_to_run`)。
///
/// ここで止まるのは**端末から起動した CUI プログラム**で、`OP_INIT` を
/// 通らないのでスロットも窓も持たない — [`input_ready`] / [`derived_ready`]
/// の材料が 1 つも無く、注入リングの未読だけが唯一の起床の理由になる。
///
/// **`kbd_inject_pending` を先に見る**。空 (= ふだん) なら KAPI 1 本で
/// 終わり、`exec_app_state` は文字があるときしか呼ばない。
pub fn key_ready(id: i32) -> bool {
    if idx(id).is_none() {
        return false;
    }
    if kbd_pending() == 0 {
        return false;
    }
    app_state(id) == APP_STATE_WAIT_KEY
}

/// 入力群: 未読の待ち行列型がリングにある、sticky Quit が積めずに残って
/// いる (契約 T3 / S5)、または鍵待ちに注入が届いている (票 K7 D3)。
///
/// 打鍵は入力そのものなので鍵待ちも**入力群**に入れる。選択規則 (D11-3:
/// ID 昇順の巡回、入力優先の据え置き上限 4、ラウンドの turn) は不変。
pub fn input_ready(st: &GuiState, id: i32) -> bool {
    if key_ready(id) {
        return true;
    }
    match st.slot_of_owner(id) {
        Some(s) => ring::pending(st, s) > 0 || session::quit_pending(s),
        None => false,
    }
}

/// 導出群: `ready` かつ入力群でない (期限切れ Timer / `Configure` 未通知 /
/// 配送できる `Paint` / park した `OP_WAIT` の期限)。
pub fn derived_ready(st: &GuiState, id: i32) -> bool {
    if idx(id).is_none() {
        return false;
    }
    if input_ready(st, id) {
        return false;
    }
    if timer::has_expired(st, id, st.now) {
        return true;
    }
    /* park した時点の `OP_WAIT` の期限。カーネルは持っていない (park は
     * syscall フレームだけを保存する) ので、WM が控えて自分で見る。 */
    if let Some(i) = idx(id) {
        let a = &m().apps[i];
        if a.has_deadline && st.now.wrapping_sub(a.deadline) < 0x8000_0000 {
            return true;
        }
    }
    let mut w = 0;
    while w < GUI_MAX_WINDOWS {
        let win = &st.windows[w];
        if win.used && win.owner == id {
            if win.configure_pending {
                return true;
            }
            if damage::has_deliverable_paint(win) {
                return true;
            }
        }
        w += 1;
    }
    false
}

/// ポーリング群 (票 T8 §7 D8): 協調 yield で止まっている (`WAIT_POLL`)。
///
/// 起床の理由は要らない — **常に ready** で、譲った 1 周が終われば必ず戻す
/// (戻さないと全画面 GFX プログラムが二度と進まない)。そのかわり優先度は
/// **最下位**で、[`pick`] は入力群も導出群も空の周にしか [`pick_poll`] を
/// 呼ばない。だから [`ready`] には**入れない** — 入れると走っているアプリが
/// 「他に ready が居る」で譲り続け、D11-3a の上界 (30) の前提が変わる。
pub fn poll_ready(id: i32) -> bool {
    if idx(id).is_none() {
        return false;
    }
    app_state(id) == APP_STATE_WAIT_POLL
}

#[inline]
fn ready(st: &GuiState, id: i32) -> bool {
    input_ready(st, id) || derived_ready(st, id)
}

/// 自分以外にポーリングで譲った 1 本 (`WAIT_POLL`) が居るか (票 T8 §7 D8)。
///
/// [`ready`] とは別に持つ: **順**を決めるのは [`pick`] (poll は最下位) で、
/// こちらが決めるのは「走っているアプリが top-level へ戻る道を開けるか」
/// だけ。`op_wait` の中は `wm_cycle` + `sys_halt` を回るだけで
/// [`pick_poll`] を呼ぶ点 (= WM top-level) へ行けないので、これが無いと
/// ポーリングの 1 本は永久に起きない (PM 実測 2026-09-12、受入 F8 不合格)。
fn poll_live(cur: i32) -> bool {
    let mm = m();
    let mut i = 0;
    while i < MAX_APPS {
        let id = APP_ID_MIN + i as i32;
        if mm.apps[i].alive && id != cur && poll_ready(id) {
            return true;
        }
        i += 1;
    }
    false
}

/// 全画面の後始末が top-level 待ちか (票 T8-3、PM 実測 2026-09-12)。
///
/// 所有者が WM (`APP_ID_SHELL`) に戻っているのに WM がまだ全画面モードの
/// まま = 復帰 (`gfx_init` → 全面再合成) が要る。`exec_kill` / fault で
/// 畳まれた経路は `exec_start` / `exec_resume` の直後を通らないので、
/// `crate::after_exec` の問い合わせが 1 度も走らない。
///
/// **復帰は top-level でしかできない**: `op_wait` の中は `res_owner_get()` が
/// アプリ ID なので、そこで `gfx_init` を呼ぶと `appslot_gfx_claim_check` が
/// 「宣言の無いアプリの要求」と見て**その端末を畳む**。だから park して戻す。
/// 全画面中は入力もタイマも実質止まり誰も ready にならないので、これが
/// 無いと `sys_halt` で永久に待つ (画面は凍ったまま)。
///
/// KAPI は全画面中だけ 1 本 (`fullscreen::active()` で短絡する)。
fn fullscreen_restore_pending() -> bool {
    if !crate::fullscreen::active() {
        return false;
    }
    unsafe { (os32api::api().gfx_screen_owner)() == APP_ID_SHELL }
}

/* ================================================================ */
/*  D11-3 (2) — 次に起こす 1 本                                      */
/* ================================================================ */

/// park 中で、このラウンドの turn を使っていない ready の本数。
fn round_remaining(st: &GuiState) -> usize {
    let mm = m();
    let mut n = 0;
    let mut i = 0;
    while i < MAX_APPS {
        let id = APP_ID_MIN + i as i32;
        if mm.apps[i].alive && !mm.apps[i].turn_used && id != mm.running && ready(st, id) {
            n += 1;
        }
        i += 1;
    }
    n
}

/// 群の中を `last_run` の次から ID 昇順に巡り、最初の 1 本 (0 = 無し)。
fn pick_group(st: &GuiState, want_input: bool) -> i32 {
    let mm = m();
    let start = if mm.last_run >= APP_ID_MIN && mm.last_run <= APP_ID_MAX {
        (mm.last_run - APP_ID_MIN + 1) as usize
    } else {
        0
    };
    let mut n = 0;
    while n < MAX_APPS {
        let i = (start + n) % MAX_APPS;
        let id = APP_ID_MIN + i as i32;
        n += 1;
        if !mm.apps[i].alive || mm.apps[i].turn_used || id == mm.running {
            continue;
        }
        if want_input {
            if input_ready(st, id) {
                return id;
            }
        } else if derived_ready(st, id) {
            return id;
        }
    }
    0
}

/// ポーリング群の 1 本 (0 = 無し、票 T8 §7 D8)。**最下位** — 呼ぶのは
/// [`pick`] の中の 1 か所、入力群も導出群も空の周だけ。
///
/// top-level にしか出来ない仕事 (`LAUNCH` 保留 / 実行できる `SessionAction` /
/// `exec_kill` の予約 / **積まれている起動要求** / **設定レジストリの予約と
/// 通知**) がある周は WM の番なので譲らない — [`should_park`] の (a) と同じ。
///
/// **順は票 T9 D5 で 2 つ変わった** (ID 昇順の固定では、sh の `sys_yield` と
/// 子の `kbd_trygetchar` が同時に `WAIT_POLL` のとき若い方だけが走り続ける):
///
/// 1. **巡回**: 前回起こした ID ([`Multi::poll_last`]) の次から探す。
/// 2. **同じ tick に同じアプリを 2 回起こさない**: 1 本ごとに
///    [`App::poll_tick`] (最後に起こした tick) を持ち、それが今の tick と
///    同じ相手は飛ばす。起こせる相手が全員起こし済みなら 0 を返し、呼ぶ側
///    (単独ループ) が `sys_halt` で次の tick を待つ。
///
/// 印を付けるのは [`mark_resumed`] (実際に `exec_resume` する直前) なので、
/// `pick` を 2 度呼んでも答えは変わらず、**選んでから再開するまでに PIT が
/// 進んでも**記録は実際に走った tick に付く (実装レビュー 1 の blocker 2)。
fn pick_poll(st: &GuiState) -> i32 {
    let mm = m();
    if session::ready_to_run(st)
        || st.launch_pending
        || has_top_level_work(mm)
        || launch_work_pending()
        || crate::settings::pending()
    {
        return 0;
    }
    /* 「この tick で起こし済み」は **1 本ごと**に持つ (実装レビュー 1 の
     * blocker 2)。集合を一括で捨てる形だと、選んだ tick と実際に再開した
     * tick がずれた周で消去が 1 回よけいに走り、同じ 1 本が同じ tick に
     * 2 回走れてしまう。 */
    let now = tick_now();
    let start = if mm.poll_last >= APP_ID_MIN && mm.poll_last <= APP_ID_MAX {
        (mm.poll_last - APP_ID_MIN + 1) as usize
    } else {
        0
    };
    let mut n = 0;
    while n < MAX_APPS {
        let i = (start + n) % MAX_APPS;
        let id = APP_ID_MIN + i as i32;
        n += 1;
        if !mm.apps[i].alive || id == mm.running {
            continue;
        }
        if mm.apps[i].poll_tick_valid && mm.apps[i].poll_tick == now {
            continue; /* この tick では走り終えている */
        }
        if poll_ready(id) {
            mm.poll_choice = id;
            return id;
        }
    }
    0
}

/// 次に起こす 1 本 (0 = 誰も起こさない)。**WM top-level が使う。**
///
/// ラウンドの turn が尽きたら全員ぶんを配り直す (= 新しいラウンド)。
/// フォーカスの近道も `!turn_used` を条件にしているので、ラウンド内の**順**が
/// 変わるだけで turn の**数**は変わらない (D11-3a)。
///
/// 全画面モード中 (`fullscreen::active()`) も判断は 1 つも変わらない — 譲って
/// くるのは所有者の全画面プログラム本人で、WM は描かないだけ (票 T8 D4)。
pub fn pick(st: &GuiState) -> i32 {
    /* この周の答えがポーリング群から出たかを [`mark_resumed`] へ渡す印
     * (票 T9 D5)。入口で必ず落とすので前の周の残骸は効かない。 */
    m().poll_choice = 0;
    if round_remaining(st) == 0 {
        let mm = m();
        let mut i = 0;
        while i < MAX_APPS {
            mm.apps[i].turn_used = false;
            i += 1;
        }
        if round_remaining(st) == 0 {
            /* ready が 1 本も無い周。**ここだけ**がポーリング群へ降りる口で
             * (D8 の「最下位」)、ラウンドにも上界 (30) にも数えない。 */
            return pick_poll(st);
        }
    }
    /* (1) 入力群にフォーカス窓の owner が居れば最優先 (応答性)。 */
    let f = st.front_owner();
    if let Some(i) = idx(f) {
        let mm = m();
        if mm.apps[i].alive && !mm.apps[i].turn_used && f != mm.running && input_ready(st, f) {
            return f;
        }
    }
    /* (2) 入力群を巡回 → (3) 空なら導出群を巡回。 */
    let k = pick_group(st, true);
    if k != 0 {
        return k;
    }
    pick_group(st, false)
}

/// 選んだ 1 本を「走り出した」ことにする (模型 `ma_resume` と同じ 3 つ)。
pub fn mark_resumed(id: i32) {
    let i = match idx(id) {
        Some(i) => i,
        None => return,
    };
    let mm = m();
    mm.apps[i].turn_used = true;
    mm.apps[i].has_deadline = false;
    mm.last_run = id;
    mm.input_streak = 0;
    mm.running = id;
    /* ポーリング群から選ばれた 1 本だけを巡回と tick の数えに載せる
     * (票 T9 D5)。入力群 / 導出群の起床は従来どおり `last_run` の巡回。 */
    if mm.poll_choice == id {
        mm.poll_choice = 0;
        mm.poll_last = id;
        /* **ここで** tick を読む (実装レビュー 1 の blocker 2)。`pick` の
         * 時点ではなく、実際に `exec_resume` する直前の tick に紐づける。 */
        mm.apps[i].poll_tick = tick_now();
        mm.apps[i].poll_tick_valid = true;
    }
}

/// [`resume_one`] が `OS32_ERR_AGAIN` で巻き戻すための控え
/// (この 1 本のラウンド状態 + 巡回の起点 + 据え置きの数え)。
fn save_turn(id: i32) -> Option<(App, i32, u32)> {
    let i = idx(id)?;
    let mm = m();
    Some((mm.apps[i], mm.last_run, mm.input_streak))
}

/// [`save_turn`] の控えを戻す (`running` は top-level = 0 のまま)。
fn restore_turn(id: i32, saved: Option<(App, i32, u32)>) {
    let (app, last_run, streak) = match saved {
        Some(v) => v,
        None => return,
    };
    let i = match idx(id) {
        Some(i) => i,
        None => return,
    };
    let mm = m();
    mm.apps[i] = app;
    mm.last_run = last_run;
    mm.input_streak = streak;
    mm.running = 0;
}

/* ================================================================ */
/*  D11-3 (1) — 走っているアプリが譲るか                             */
/* ================================================================ */

/// `op_wait` の中で park すべきか。**判定の順序が肝**で、「他に ready が
/// 居るか」を先に見る (1 本しか居なければ `input_streak` は伸びず park も
/// 起きない = 回帰ゼロ)。
///
/// 模型 `ma_should_park` に 1 行だけ足してある: **top-level にしか出来ない
/// 仕事 (`LAUNCH` / デバッグ経路の起動予約) があるときも譲る**。
/// `exec_start` は WM top-level からしか呼べず (契約 S2、カーネルの
/// `appslot_start_admit`)、走っているアプリが park しない限り top-level へ
/// 戻る道が無いので、これが無いと 2 本目が永久に起動できない。
/// 起動要求は有限個の事象なので D11-3a の上界は変わらない。
///
/// 票 T8-3 で譲る理由が 2 つ増えた。どちらも「top-level へ戻る道」の話で、
/// 起こす**順** (D11-3 の (2)) は 1 つも動かない:
/// [`poll_live`] (ポーリングで譲った 1 本を [`pick_poll`] に起こさせる) と
/// [`fullscreen_restore_pending`] (全画面の復帰は top-level の仕事)。
///
/// 票 T9 D3 (2) で 3 つ目 ([`launch_work_pending`])。`launch_take` も owner 1
/// 専用なので、これが無いと `sh` の起動要求は永久に取りに行かれない。
/// 起動要求は有限個の事象なので D11-3a の上界は変わらない。
///
/// 票 S4 §3 で 4 つ目 ([`crate::settings::pending`])。設定の読み書きは
/// top-level だけの仕事 (決裁: OS 経由・同時 1 本・yield なし) で、
/// **予約 (`req`) だけでなく通知 (`notice`) も含める** — 通知はモーダル枠が
/// 空くまで出せないので、枠が塞がっている間も top-level へ戻る道が要る。
/// 設定の予約と通知も有限個の事象なので上界は変わらない。
pub fn should_park(st: &GuiState, cur: i32) -> bool {
    let i = match idx(cur) {
        Some(i) => i,
        None => return false, /* シェル帯 / 範囲外 */
    };
    let mm = m();
    if !mm.apps[i].alive {
        /* 起動が進行中なら、これは `exec_start` で立てたばかりのアプリ。
         * 表に載せて譲り合いの対象にする (載せないと park できず
         * `exec_start` が永久に戻らない)。それ以外の未追跡 ID
         * (CUI の入れ子の子) は譲らない — カーネルも `!a->gui` で拒む。 */
        if !mm.pending_start {
            return false;
        }
        adopt_running(cur);
    }
    /* (a) top-level の仕事 (契約 S2 の 4 本前提の読み替え)。
     * `exec_abort_clear` / `exec_kill` の予約 (決裁 A1 / A3) も owner 1 から
     * しか実行できないので、ここに含める。含めないと**アプリが 1 本のとき**
     * (誰も ready でないので下の (b) が偽) に top-level へ戻る道が無く、
     * CTRL+STOP の付け替えも Quit 無視の打ち切りも永久に実行されない。 */
    if session::ready_to_run(st)
        || st.launch_pending
        || has_top_level_work(mm)
        || fullscreen_restore_pending()
        || launch_work_pending()
        || crate::settings::pending()
    {
        return true;
    }
    /* (b) D11-3 (1) の 3 行。 */
    let mut other_ready = false;
    let mut k = 0;
    while k < MAX_APPS {
        let id = APP_ID_MIN + k as i32;
        if mm.apps[k].alive && id != cur && ready(st, id) {
            other_ready = true;
        }
        k += 1;
    }
    /* 票 T8 §7 D8 (PM 実測 2026-09-12、受入 F8): ready が 1 本も無くても、
     * ポーリングで譲った 1 本が居るなら譲る。**「最下位」は [`pick`] の側で
     * 決まる**ので、ここに足しても起こす順は変わらない — 変わるのは
     * 「top-level へ戻る道が開くか」だけ。据え置き (`input_streak`) は
     * ready のときと同じに効かせる (自分の打鍵を取りこぼさない)。
     * 譲りが増えるだけなので D11-3a の上界 (30) は伸びない。 */
    if !other_ready && !poll_live(cur) {
        return false;
    }
    if input_ready(st, cur) && mm.input_streak < INPUT_STREAK_MAX {
        mm.input_streak += 1;
        return false;
    }
    true
}

/// **`exec_park` を呼ぶ唯一の点** (`handler::op_wait` のループ先頭から)。
///
/// 成立すればこの関数も `op_wait` も戻らず、`exec_start` / `exec_resume` の
/// 復帰点 = WM top-level へ抜ける。戻ってきたのは park できない文脈
/// (`OP_WAIT` 以外の op / CUI の入れ子の子 / CPL=0 の子) で、カーネルが
/// `ring3_park_reject_count` を上げて `OS32_ERR_INVAL` を返した場合だけ。
/// そのときは普通の待ちを続ける。
pub fn maybe_park(st: &mut GuiState, cur: i32, deadline: Option<u32>) {
    if !should_park(st, cur) {
        return;
    }
    note_parked(cur, deadline);
    /* KAPI v45 (K5c) で生成器の TYPE_MAP に "i32" が入ったので、束縛はもう
     * `i32` を返す。u32 経由の往復は要らない。 */
    let rc = unsafe { (os32api::api().exec_park)() };
    /* ここへ来たのは park が成立しなかったとき (`OS32_ERR_INVAL`)。 */
    undo_park(cur);
    let _ = rc;
}

/// park したことにする (WM 側の状態遷移だけ)。`exec_park` を呼ぶ**直前**に
/// 通る点で、[`maybe_park`] とホスト試験の周回ドライバが共有する。
pub fn note_parked(cur: i32, deadline: Option<u32>) {
    let i = match idx(cur) {
        Some(i) => i,
        None => return,
    };
    let mm = m();
    /* park 中の `OP_WAIT` の期限は WM が持つ (D11-1)。カーネルは syscall
     * フレームしか保存しないので、これを控えないと timeout 付きで待って
     * いたアプリが二度と起きない。 */
    mm.apps[i].has_deadline = deadline.is_some();
    mm.apps[i].deadline = match deadline {
        Some(d) => d,
        None => 0,
    };
    mm.running = 0;
}

/// park が成立しなかったときに [`note_parked`] を巻き戻す。
fn undo_park(cur: i32) {
    let i = match idx(cur) {
        Some(i) => i,
        None => return,
    };
    let mm = m();
    mm.apps[i].has_deadline = false;
    mm.running = cur;
}

/* ================================================================ */
/*  top-level (lib.rs の単独ループ)                                  */
/* ================================================================ */

/// 止めてあるアプリを 1 本だけ起こす。戻り値 `true` = 何かした
/// (= `sys_halt` せずに次の周へ)。
///
/// **`exec_resume` を呼ぶ唯一の点**。呼べるのは WM top-level だけで
/// (カーネルの `appslot_resume_check`)、印の無いフレームは `OS32_ERR_STALE`。
pub fn resume_one(st: &mut GuiState) -> bool {
    if drain_top_level() {
        /* 票 T8-3 (PM 実測 2026-09-12): 畳んだ相手が全画面の所有者だったかも
         * しれない。`exec_kill` は `exec_start` / `exec_resume` の復帰点を
         * 通らないので、ここで問い合わせないと WM が全画面モードのまま残り、
         * 画面が凍ったまま `sys_halt` で待ち続ける。ここは top-level =
         * owner 1 なので `gfx_init` を呼んでよい ([`fullscreen_restore_pending`]
         * の註)。全画面中だけ (KAPI 1 本)。 */
        if crate::fullscreen::active() {
            crate::after_exec(st);
        }
        return true;
    }
    let k = pick(st);
    if k == 0 {
        return false;
    }
    /* 契約 T3: `OP_WAIT` の戻り値は未読件数。 */
    let wait_ret = match st.slot_of_owner(k) {
        Some(s) => ring::pending(st, s) as i32,
        None => {
            /* スロットが無くても、鍵待ち (`WAIT_KEY`) なら起こす相手
             * (票 K7 §5 指摘 A)。端末から起動した CUI プログラムは
             * `OP_INIT` を通らないのでスロットを持たず、ここで `forget`
             * すると打鍵待ちのまま二度と起こされない。
             * `wait_ret` はカーネルが注入リングの 1 バイトで上書きする
             * (指摘 B: 取り出し用の KAPI は作らない) ので 0 を渡す。 */
            let s = app_state(k);
            if s != APP_STATE_WAIT_KEY && s != APP_STATE_WAIT_POLL {
                /* `OP_INIT` 前か回収済み。起こす相手ではない。 */
                forget(k);
                return true;
            }
            /* 票 T8 §7 D8: ポーリングで譲った全画面 GFX プログラムも同じ形
             * (端末から起動するのでスロットを持たない)。`wait_ret` はカーネル
             * が印を見て上書きする — 注入リングに文字があれば 1 バイト、
             * 無ければ -1 (キー無し)。だから鍵待ちと違って空でも拒まれない。 */
            0
        }
    };
    let saved = save_turn(k);
    mark_resumed(k);
    let rc = unsafe { (os32api::api().exec_resume)(k, wait_ret) };
    m().running = 0;
    /* 所有者の問い合わせ (票 T8 D3/D4)。全画面 GFX プログラムが `gfx_init` を
     * 呼んだ / 抜けたのは `exec_resume` から戻った直後にしか分からない。
     * 戻っていれば `after_exec` が復帰 (`gfx_init` → パレット → 全面再合成) まで
     * 済ませる。全画面中は WM が描かないだけで、譲り合いの判断は変わらない。 */
    crate::after_exec(st);
    if rc == OS32_ERR_AGAIN {
        /* 注入リングが空だった (指摘 B: カーネルは印を残したまま拒む)。
         * 畳まずに**その周は譲る** — turn も巡回の起点も据え置きの数えも
         * 動かさないので、D11 のラウンドと上界 (30) は変わらない。
         * 次の周は `key_ready` が偽になるので選び直しは空振りしない。 */
        restore_turn(k, saved);
        return true;
    }
    if rc < 0 {
        /* 起こせない (印無し / 状態違い)。放置すると永久に固まるので、
         * 「止めてあるアプリを畳む」口 (D4) をそのまま使って畳む。 */
        request_kill(k);
        drain_top_level();
        forget(k);
    }
    /* rc == 0 は終了 (回収は `gui_owner_exit` 経由で済んでいる)、
     * rc > 0 はまた park した。どちらも次の周で選び直す。 */
    true
}

/// top-level (owner 1) でしか実行できない予約を片付ける (`true` = 何かした)。
///
/// 順序が肝: **CTRL+STOP の取り消しを畳むより先に**行う (決裁 A1)。逆にすると
/// `exec_kill` の間に走っている本人が syscall の出口を通って巻き添えで死ぬ。
/// `exec_kill` は 1 周に 1 本だけ — 呼ぶ側 (`resume_one`) が次の周でまた来る。
fn drain_top_level() -> bool {
    let mm = m();
    let mut did = false;
    if mm.abort_clear_req {
        mm.abort_clear_req = false;
        unsafe {
            (os32api::api().exec_abort_clear)();
        }
        did = true;
    }
    let mut i = 0;
    while i < MAX_APPS {
        if mm.apps[i].alive && mm.apps[i].kill_req {
            let id = APP_ID_MIN + i as i32;
            mm.apps[i].kill_req = false;
            let rc = unsafe { (os32api::api().exec_kill)(id) };
            if rc < 0 {
                /* 走っている本人 (`OS32_ERR_STALE`) など。予約は落としたまま。 */
                return true;
            }
            forget(id);
            /* 票 T9 D8: `exec_kill` は要求表の連鎖の**子孫ごと**畳む。畳まれた
             * 子孫まで表から落とさないと、その ID が再利用されたときに
             * 「生きている別人」として起こしにいく。 */
            forget_freed();
            return true;
        }
        i += 1;
    }
    did
}

/// 表から 1 本落とす (`gui_owner_exit` が来なかった経路の保険)。
fn forget(id: i32) {
    on_owner_exit(id);
}

/// `exec_app_state` が `FREE` を返す被追跡 ID を**全部**表から落とす (票 T9 D8)。
///
/// `exec_kill(id)` は要求表の連鎖 (`launch_child`) を末尾まで辿って子孫ごと
/// 畳む (K の実装メモ)。畳まれるのは 1 本とは限らないので、`gui_owner_exit` が
/// 届かなかったぶんをここで拾う。KAPI は生きている本数ぶん (最大 4 本) で、
/// 呼ぶのは kill の直後だけ。
pub fn forget_freed() {
    let mut i = 0;
    while i < MAX_APPS {
        let id = APP_ID_MIN + i as i32;
        if m().apps[i].alive && app_state(id) == APP_STATE_FREE {
            forget(id);
        }
        i += 1;
    }
}

/// 起動要求表の KILL を実行する (票 T9 D3 (4))。**top-level 専用** —
/// `exec_kill` は owner 1 からしか通らない (K5c)。
///
/// 畳んだ後に `FREE` になった ID を全部表から落とす (D8)。戻り値は
/// `exec_kill` の rc (KILL の `launch_report` は rc を見ないので参考値)。
pub fn kill_for_request(id: i32) -> i32 {
    /* SAFETY: 同上。`exec_kill` は ID (値) だけを取る。呼べるのは owner 1 =
     * WM top-level で、そうでなければカーネルが `OS32_ERR_INVAL` で断る。 */
    let rc = unsafe { (os32api::api().exec_kill)(id) };
    forget_freed();
    rc
}

/* ================================================================ */
/*  音の排他 (決裁 D9-4、受入 G10)                                   */
/* ================================================================ */

/// フォーカス (= 最前面の可視窓の owner) が変わっていたら `snd_focus` を
/// 1 回だけ呼ぶ。**KAPI を呼ぶので X1 からは呼ばない** (契約 T8) —
/// 呼ぶのは `wm_cycle` の末尾 (X3 と単独ループ) だけ。
pub fn sync_snd_focus(st: &GuiState) {
    let f = st.front_owner();
    /* 窓が 1 枚も無ければ音の所有権はシェル帯へ戻す。 */
    let target = if f == 0 { APP_ID_SHELL } else { f };
    let mm = m();
    if target == mm.snd_owner {
        return;
    }
    mm.snd_owner = target;
    unsafe {
        (os32api::api().snd_focus)(target);
    }
}

/// いま `snd_focus` に渡してある owner (試験と診断用)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn snd_owner() -> i32 {
    m().snd_owner
}

/* ================================================================ */
/*  CTRL+STOP の宛先 (契約 T6 / D4)                                  */
/* ================================================================ */

/// CTRL+STOP を**いま走っているアプリ**へ効かせてよいか。
///
/// T6 の宛先はフォーカス窓のアプリ。カーネルは IRQ1 の時点で「走っている
/// アプリ」にしか要求を立てられないので (`appslot_abort_request`)、
/// フォーカスが別のアプリなら WM は待ちを抜けない = 走っている側を畳ませない。
pub fn abort_targets_current(st: &GuiState, cur: i32) -> bool {
    let f = abort_target(st);
    /* 票 T9 D8: 宛先が**連鎖の末尾**になったので、端末 (cur) が子を持つなら
     * ここは偽 — 端末自身の syscall 出口では畳まれず、`redirect_abort` が
     * 末尾を予約して top-level の `exec_kill` が 1 本だけ畳む。
     *
     * `f == 0` (窓もスロットも全画面所有者も無い) だけは従来どおり真のまま:
     * 宛先が 1 つも無い周で偽にすると `redirect_abort` も早々に戻るので、
     * CTRL+STOP がどこにも届かなくなる (K5b 以来の保険)。 */
    f == 0 || f == cur
}

/// CTRL+STOP の宛先 (0 = 宛先なし)。
///
/// 頭は**フォーカス窓のアプリ** (契約 T6)。**全画面 GFX 中だけは画面の
/// 所有者** (票 T8 D4d) — 画面を持っているプログラムは窓を持たないので、
/// フォーカス窓 (端末) を畳んでしまうと「見えている方」が残ってしまう。
///
/// 票 T9 D8 で、そこから **起動要求表の連鎖を末尾まで辿る**ようになった。
/// 端末 → `sh` → 子 と積み上がっているとき、頭 (端末) を畳むと `sh` と子が
/// 孤児になる。畳むのは末尾 1 本で、`sh` はその `DONE` を受けて `sh> ` に戻る。
pub fn abort_target(st: &GuiState) -> i32 {
    let owner = crate::fullscreen::owner();
    let head = if owner != 0 { owner } else { st.front_owner() };
    if head == 0 {
        return 0;
    }
    chain_tail(head)
}

/// `launch_child` を末尾まで辿る (票 T9 D8)。**同じ ID が 2 度出たら止める** —
/// 壊れた表が環を作っても戻ってくる (カーネルの `launch_chain` と同じ線)。
fn chain_tail(head: i32) -> i32 {
    let mut seen = [0i32; MAX_APPS];
    let mut n = 0;
    let mut cur = head;
    while n < MAX_APPS {
        let mut k = 0;
        while k < n {
            if seen[k] == cur {
                return cur; /* 環 */
            }
            k += 1;
        }
        seen[n] = cur;
        n += 1;
        /* SAFETY: 同上。`launch_child` は ID (値) を取り ID を返すだけで、
         * 不正な ID には 0 を返す (§1a)。 */
        let next = unsafe { (os32api::api().launch_child)(cur) };
        if next == 0 || next == cur {
            return cur;
        }
        cur = next;
    }
    cur
}

/* ================================================================ */
/*  試験と診断のための覗き口                                         */
/*                                                                  */
/*  規則そのものは 1 つも持たない。模型 (`multiapp_model_host.c`) の  */
/*  試験が `MaState` の欄を直接読み書きしていたのと同じ役割で、      */
/*  ホスト試験が同じ検査を実物の状態に対して書けるようにするだけ。   */
/* ================================================================ */

/// 巡回の起点 (`last_run`)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn last_run() -> i32 {
    m().last_run
}

/// **試験専用**: 巡回の起点を直接置く (模型の `st.last_run = n` と同じ)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn set_last_run(id: i32) {
    m().last_run = id;
}

/// このラウンドで turn を使ったか。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn turn_used(id: i32) -> bool {
    match idx(id) {
        Some(i) => m().apps[i].turn_used,
        None => false,
    }
}

/// 入力優先で turn を据え置いた連続回数。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn input_streak() -> u32 {
    m().input_streak
}

/// いま走っているアプリ ID (0 = WM top-level)。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn running() -> i32 {
    m().running
}
