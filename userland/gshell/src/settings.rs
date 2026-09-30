//! settings.rs — 設定レジストリ (`/etc/settings.db`) の gshell 側 (票 S4 §2・§3)。
//!
//! gshell は設定レジストリの**最初の消費者**で、読むのは `gshell` scope の 2 キー
//! だけ (票 §1):
//!
//! | key | 意味 | 既定 |
//! |---|---|---|
//! | `desktop/color` | デスクトップ背景のシステム色 index (0〜15) | 12 (`GUI_COLOR_DESKTOP`) |
//! | `taskbar/clock_24h` | 1 = `HH:MM`、0 = `h:MM AM/PM` | 1 |
//!
//! ## DB に触れる場所は 1 か所だけ
//!
//! 決裁 (S0_PLAN §4) は「設定の読み書きは OS 経由だけ、接続は同時 1 本、
//! open 〜 close の間に yield しない」。gshell では **top-level (owner 1、
//! `standalone_loop`)** がその唯一の場所で、X4 / IRQ / 描画 callback /
//! `handler` (契約 S8) / `wm_cycle(Ctx::Wait)` からは 1 本も呼ばない。
//!
//! Start → Settings... は「アプリ 1 本が `OP_WAIT` 中なら `handler` の文脈」で
//! 起きるので、そこでは**予約を立てるだけ** ([`request_open`])。予約がある間は
//! [`pending`] が真になり、[`crate::multiapp::should_park`] の (a) と
//! `pick_poll` の門がそれを見て走っているアプリに譲らせる (T9 の
//! `launch_work_pending` と同じ扱い)。top-level へ戻った周回で [`consume`] が
//! 実際の `cfg_open` 〜 `cfg_close` を行う。
//!
//! ```text
//!   handler / X3       request_open()      予約だけ (DB に触らない)
//!      ↓ should_park (a) が真 → park
//!   standalone_loop    consume(st)         load() / 保存 / 通知
//! ```
//!
//! ## モーダル枠の競合 (票 §3 の R1)
//!
//! 予約から消費までの間にアプリが自分のモーダル (X3 `MODAL_OPEN`) を開くことが
//! ある。WM のモーダル枠は 1 つで `open_wm` は既存モーダルがあると拒むので、
//! **開けなかったものは捨てず保持して再試行する**:
//!
//! - `Req::Open` と [`Notice`] は `modal::is_open()` が偽の周回でだけ消費する。
//!   予約が残る限り [`pending`] は真 = 毎周回 top-level に戻れる。
//! - `Req::Save` は枠と無関係に**その周回で** DB 操作と適用まで済ませ、結果の
//!   通知だけを `notice` に置く。
//! - `open_wm_message` の戻り値 (`open_wm` の bool) は**必ず見る**。
//!
//! ## 3 種の値を区別する (票 §2)
//!
//! | | 持ち主 | 何 |
//! |---|---|---|
//! | 適用値 | [`crate::wm::GuiState::cfg`] | 画面が実際に使っている値 |
//! | 再読込値 | このモジュールの私有状態 (`base_*`) | ダイアログを開いた時点の `load()` = 編集の起点 |
//! | 編集中の値 | `modal.rs` の私有状態 | ↑↓←→ で動かしている途中の値 |
//!
//! OK の「変更なし」判定は**編集値と再読込値**の比較、画面への反映は**保存が
//! 成功してから**適用値へ写す。

#![allow(dead_code)]

use crate::wm::{GuiState, Rect};
use crate::{modal, taskbar};
use os32api::cfg::{
    cfg_begin, cfg_close, cfg_commit, cfg_get_int, cfg_last_close_error, cfg_last_sqlite, cfg_open,
    cfg_schema_version, cfg_set_int, cfg_status, CfgDb, CFG_CORRUPT, CFG_ERROR, CFG_MISSING,
    CFG_OK, CFG_VERSION,
};
use os32api::gui::proto::{GUI_COLOR_DESKTOP, GUI_MODAL_OK};

/* ================================================================ */
/*  キーと既定値 ([C4]: 正典は assets/settings/defaults.tsv)          */
/* ================================================================ */

/// 読む scope (NUL 終端)。
pub static SCOPE: &[u8] = b"gshell\0";
/// デスクトップ背景色の key (NUL 終端)。
pub static KEY_COLOR: &[u8] = b"desktop/color\0";
/// 時計の 24 時間表記の key (NUL 終端)。
pub static KEY_CLOCK: &[u8] = b"taskbar/clock_24h\0";

/// `desktop/color` の既定 = 現行の `GUI_COLOR_DESKTOP` (既定で見た目が変わらない)。
pub const DEFAULT_DESKTOP_COLOR: u8 = GUI_COLOR_DESKTOP;
/// `taskbar/clock_24h` の既定 = 24 時間表記 (現行の見た目)。
pub const DEFAULT_CLOCK_24H: bool = true;
/// システム色の index の上限 (G6 の 16 色)。
pub const COLOR_MAX: u8 = 15;

/// `cfg_open` が負 (引数 / メモリ) だったときの [`GuiCfg::status`]。
/// `CFG_*` はどれも 0 以上なので衝突しない。
pub const STATUS_OPEN_FAILED: i32 = -1;

/* 保存の段 ([`Notice::SaveFailed`])。 */
pub const STAGE_OPEN: u8 = 0;
pub const STAGE_BEGIN: u8 = 1;
pub const STAGE_SET: u8 = 2;
pub const STAGE_COMMIT: u8 = 3;

/* ================================================================ */
/*  適用値 (GuiState.cfg)                                            */
/* ================================================================ */

/// 画面に**適用済み**の設定と、その出どころの診断 (票 §2)。
#[derive(Clone, Copy)]
pub struct GuiCfg {
    /// デスクトップ背景のシステム色 index (0〜15)。
    pub desktop_color: u8,
    /// 時計を 24 時間表記にするか。
    pub clock_24h: bool,
    /// `CFG_*` / [`STATUS_OPEN_FAILED`]。
    pub status: i32,
    /// `cfg_schema_version` の実値 (VERSION の表示用)。
    pub schema_version: i32,
    /// `cfg_last_sqlite` (`CFG_ERROR` の詳細)。
    pub sqlite: i32,
    /// 直近の close 失敗 (0 = 無し)。**再 load が成功しても消さない**
    /// (利用者が見るまで残す。票 §2)。
    pub close_error: i32,
    /// [`load`] に掛かった tick (S5 の材料、受入 G7)。
    pub load_ticks: u32,
    /// 直近の保存に掛かった tick。**再 Open の `load()` で消さない**。
    pub save_ticks: u32,
}

impl GuiCfg {
    /// 既定値 (DB を 1 度も読んでいない状態)。`GuiState::NEW` が使う。
    pub const NEW: GuiCfg = GuiCfg {
        desktop_color: DEFAULT_DESKTOP_COLOR,
        clock_24h: DEFAULT_CLOCK_24H,
        status: CFG_OK,
        schema_version: 0,
        sqlite: 0,
        close_error: 0,
        load_ticks: 0,
        save_ticks: 0,
    };

    /// 起動時通知を出すべきか (票 §2: status が OK 以外か close 失敗)。
    #[inline]
    pub fn needs_notice(&self) -> bool {
        self.status != CFG_OK || self.close_error != 0
    }
}

/* ================================================================ */
/*  予約と通知                                                       */
/* ================================================================ */

/// top-level に頼む仕事。**同時に 1 本だけ** (`Option`)。
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Req {
    /// 現在値を読み直して設定ダイアログを開く。
    Open,
    /// 編集値を書き戻す (変わった key だけ。マスクは私有状態)。
    Save { color: u8, clock_24h: bool },
}

/// 枠が空いた周回で 1 回だけ出すメッセージ。**同時に 1 本だけ**。
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Notice {
    /// 起動時 (status が OK 以外 / close 失敗)。
    Startup {
        status: i32,
        schema: i32,
        sqlite: i32,
        close_error: i32,
    },
    /// 再読込が OK でなかったので保存しなかった。
    CannotSave { status: i32 },
    /// begin / set / commit の失敗 (**適用値は変えていない**)。
    SaveFailed { stage: u8, code: i32 },
    /// commit は成功して `cfg_close` だけ失敗 (**適用値は更新した**)。
    SavedCloseFailed { code: i32 },
}

/* ================================================================ */
/*  私有状態 (.bss。モーダルが閉じても失われない)                     */
/* ================================================================ */

struct State {
    req: Option<Req>,
    notice: Option<Notice>,
    /* ---- 再読込値 = 編集の起点 (ダイアログを開いた周回の `load()`) ---- */
    base_color: u8,
    base_clock: bool,
    base_status: i32,
    /* ---- 変更マスク (OK で決めて、保存の周回まで持ち越す) ---- */
    mask_color: bool,
    mask_clock: bool,
    /* ---- 計測 (再 Open の `load()` で消さない) ---- */
    save_ticks: u32,
}

impl State {
    const NEW: State = State {
        req: None,
        notice: None,
        base_color: DEFAULT_DESKTOP_COLOR,
        base_clock: DEFAULT_CLOCK_24H,
        base_status: CFG_OK,
        mask_color: false,
        mask_clock: false,
        save_ticks: 0,
    };
}

struct StateCell(core::cell::UnsafeCell<State>);
unsafe impl Sync for StateCell {}
static STATE: StateCell = StateCell(core::cell::UnsafeCell::new(State::NEW));

#[inline]
fn s() -> &'static mut State {
    unsafe { &mut *STATE.0.get() }
}

/// 試験用の初期化 (ホスト TDD が試験ごとに呼ぶ)。
pub fn reset() {
    *s() = State::NEW;
}

/* ================================================================ */
/*  予約の入口 (handler / X3 の文脈から呼んでよい唯一の面)            */
/* ================================================================ */

/// **top-level に仕事が残っているか** (票 §3 の non-blocker 2)。
///
/// `req` だけでなく `notice` も含める — `req = None, notice = Some` の状態で
/// アプリ 1 本が `OP_WAIT` に戻っても [`crate::multiapp::should_park`] が真に
/// なり、top-level の周回で通知を再試行できる。
#[inline]
pub fn pending() -> bool {
    let st = s();
    st.req.is_some() || st.notice.is_some()
}

/// Start → Settings...。**DB には触らない** — 予約を立てるだけ (票 §3 の B1)。
pub fn request_open() {
    /* 二重予約は後勝ちにしない。Open 中に Save は来ない (モーダルが 1 つ)。 */
    if s().req.is_none() {
        s().req = Some(Req::Open);
    }
}

/// 起動時通知を仕込む (票 §2 の B3)。`main` が**最初の合成の後**に 1 回呼ぶ。
/// 実際にダイアログが出るのは top-level の最初の周回 ([`consume`])。
pub fn startup_notice(cfg: &GuiCfg) {
    log_startup(cfg);
    if !cfg.needs_notice() {
        return;
    }
    s().notice = Some(Notice::Startup {
        status: cfg.status,
        schema: cfg.schema_version,
        sqlite: cfg.sqlite,
        close_error: cfg.close_error,
    });
}

/// 設定ダイアログの **OK** (票 §3 の書き戻し点)。`modal::finish_wm` から呼ばれる
/// = ダイアログは既に閉じている。**ここでも DB には触らない**。
///
/// 判定順:
/// 1. 編集値が再読込値と同じ → 何もしない (`gshell: cfg write skipped`)。
/// 2. 再読込が `CFG_OK` でない → 保存せず `cannot save: <status>`。
/// 3. それ以外 → 変更マスクを立てて `Req::Save` を予約する。
pub fn on_ok(color: u8, clock_24h: bool) {
    let st = s();
    let changed_color = color != st.base_color;
    let changed_clock = clock_24h != st.base_clock;
    if !changed_color && !changed_clock {
        kprint(b"gshell: cfg write skipped\n\0");
        return;
    }
    if st.base_status != CFG_OK {
        st.notice = Some(Notice::CannotSave { status: st.base_status });
        return;
    }
    st.mask_color = changed_color;
    st.mask_clock = changed_clock;
    st.req = Some(Req::Save { color, clock_24h });
}

/* ================================================================ */
/*  読み込み (票 §2)                                                 */
/* ================================================================ */

/// 現在 tick。
#[inline]
fn tick() -> u32 {
    /* SAFETY: KAPI の関数表は `os32_init` が入口で据えた有効なポインタ。
     * `get_tick` は引数なし・戻りは値で、誰でも呼べる。 */
    unsafe { (os32api::api().get_tick)() }
}

/// 0〜15 に収まらない値は既定へ落とす (票 §1)。
#[inline]
pub fn clamp_color(v: i32) -> u8 {
    if v >= 0 && v <= COLOR_MAX as i32 {
        v as u8
    } else {
        DEFAULT_DESKTOP_COLOR
    }
}

/// 0 / 1 以外は既定 (= 24h) へ落とす (票 §1)。
#[inline]
pub fn clamp_clock(v: i32) -> bool {
    match v {
        0 => false,
        1 => true,
        _ => DEFAULT_CLOCK_24H,
    }
}

/// `cfg_open(&db, 0)` → `cfg_get_int` ×2 → `cfg_close`。**全経路で既定値を埋める**
/// ので、失敗しても起動は止まらない (DESIGN §5 の fallback)。
///
/// `prev_close_error` は「まだ利用者に見せていない close 失敗」。再 load が
/// 成功しても消さない (票 §2)。
pub fn load(prev_close_error: i32) -> GuiCfg {
    let t0 = tick();
    let mut c = GuiCfg::NEW;
    c.close_error = prev_close_error;
    c.save_ticks = s().save_ticks;

    let mut db: *mut CfgDb = core::ptr::null_mut();
    /* SAFETY: `db` はこのフレームの 1 語。`cfg_open` は 0 以上を返したときだけ
     * そこへ有効な接続を置く (置かずに 0 を返すのは契約違反なので NULL も見る)。 */
    let orc = unsafe { cfg_open(&mut db, 0) };
    if orc < 0 || db.is_null() {
        c.status = STATUS_OPEN_FAILED;
        c.sqlite = orc;
        c.load_ticks = tick().wrapping_sub(t0);
        return c;
    }
    /* SAFETY: `db` は直前の `cfg_open` が返した接続。scope / key は NUL 終端の
     * 静的バイト列。`cfg_get_int` は失敗も未設定も `def` を返す。 */
    let (color, clock) = unsafe {
        (
            cfg_get_int(db, SCOPE.as_ptr(), KEY_COLOR.as_ptr(), DEFAULT_DESKTOP_COLOR as i32),
            cfg_get_int(
                db,
                SCOPE.as_ptr(),
                KEY_CLOCK.as_ptr(),
                DEFAULT_CLOCK_24H as i32,
            ),
        )
    };
    /* 状態は **get の後**に採る (票 §5 の (18)): 読んでいる途中の I/O 失敗で
     * `cfg_status` が `CFG_ERROR` に変わるので、open 直後の値では取り逃がす。 */
    /* SAFETY: 同上。3 本とも接続を読むだけ。 */
    unsafe {
        c.status = cfg_status(db);
        c.sqlite = cfg_last_sqlite(db);
        c.schema_version = cfg_schema_version(db);
    }
    /* SAFETY: 同上。close の後も `cfg_last_close_error` は読める (静的 1 本)。 */
    let crc = unsafe { cfg_close(db) };
    if crc < 0 {
        let e = unsafe { cfg_last_close_error() };
        /* rollback 失敗も含むので「隔離 slot」とは断定しない (票 §2)。 */
        c.close_error = if e != 0 { e } else { crc };
    }
    /* **取得値を採るのは status が OK / VERSION のときだけ** (実装レビュー
     * 往復 1 の B1)。`cfg_get_int` は失敗も未設定も `def` を返すので、
     * 「1 本目が 3 を返し、2 本目の prepare / step が I/O で落ちた」経路では
     * 先に読めた 3 だけが本物になる。そのまま採ると
     * 「ERROR・defaults in use と通知しながら背景は 3」という食い違いが出る。
     * status は get の後に採ってあるので、ここで一括して既定へ倒せばよい。
     * VERSION は「読める値をそのまま使う」(票 §2) ので採る。
     * **診断 (`status` / `sqlite` / `schema_version`) と `close_error` は保つ** —
     * 通知と状態行はそれを見る。 */
    if c.status == CFG_OK || c.status == CFG_VERSION {
        c.desktop_color = clamp_color(color);
        c.clock_24h = clamp_clock(clock);
    }
    c.load_ticks = tick().wrapping_sub(t0);
    c
}

/* ================================================================ */
/*  top-level の消費 (standalone_loop から 1 周 1 回)                 */
/* ================================================================ */

/// 予約と通知を 1 周ぶん捌く。**呼べるのは gshell の top-level だけ**
/// (`standalone_loop`、owner 1)。戻り値 `true` = 何かした。
///
/// 順は固定:
///
/// 1. `Req::Save` … モーダル枠と無関係に**この周回で** DB 操作と適用まで行う。
/// 2. `Notice`    … 枠が空いている周回でだけ出す (`open_wm` の戻りを見る)。
/// 3. `Req::Open` … 枠が空いている周回でだけ `load()` して開く。
///
/// 2 が 3 より先なので、通知が出るまで Settings は開かない (票 §3)。
pub fn consume(st: &mut GuiState) -> bool {
    let mut did = false;

    /* (1) 保存。枠が塞がっていても実行する (票 §3 の R1)。 */
    if let Some(Req::Save { color, clock_24h }) = s().req {
        s().req = None;
        save(st, color, clock_24h);
        did = true;
    }

    /* (2) 通知。開けなければ保持したまま次の周回へ。 */
    if s().notice.is_some() && !modal::is_open() {
        let n = s().notice.unwrap();
        let mut buf = [0u8; MSG_MAX];
        let len = notice_text(&n, &mut buf);
        if modal::open_wm_message(
            st,
            GUI_MODAL_OK,
            &buf[..=len],
            modal::WM_PURPOSE_CFG_NOTICE,
        ) {
            s().notice = None;
            did = true;
        }
    }

    /* (3) Open。`load()` は**開ける周回で**呼ぶ (古い値を持ち越さない)。 */
    if s().req == Some(Req::Open) && !modal::is_open() {
        s().req = None;
        let cfg = load(st.cfg.close_error);
        /* close 失敗は握りつぶさない (適用値の診断へ持ち上げる)。 */
        st.cfg.close_error = cfg.close_error;
        st.cfg.status = cfg.status;
        st.cfg.schema_version = cfg.schema_version;
        st.cfg.sqlite = cfg.sqlite;
        st.cfg.load_ticks = cfg.load_ticks;
        /* 再読込値 = 編集の起点。**適用値はまだ動かさない**。 */
        {
            let ss = s();
            ss.base_color = cfg.desktop_color;
            ss.base_clock = cfg.clock_24h;
            ss.base_status = cfg.status;
            ss.mask_color = false;
            ss.mask_clock = false;
        }
        /* **戻り値を見る** (実装レビュー往復 1 の non-blocker)。いまの
         * `open_wm_settings` は直前の `is_open()` から呼び出しまでに枠を塞ぐ
         * 処理も yield も無いので偽にはならないが、契約
         * 「開けなかったものは捨てず保持して再試行する」(票 §3 の R1) を
         * 通知経路と同じ形で書いておく — 将来この間に何か挟まっても、
         * 予約が残る限り `pending()` が真 = 次の周回で開き直せる。 */
        if !modal::open_wm_settings(st, &cfg) {
            s().req = Some(Req::Open);
        }
        did = true;
    }
    did
}

/// `Req::Save` の実体。**1 つの周回の中で** `cfg_open(&db, 1)` → `cfg_begin` →
/// `cfg_set_int` (変わった key だけ) → `cfg_commit` → `cfg_close` を完結させる
/// (間に yield / 合成 / 他イベント処理を挟まない)。
fn save(st: &mut GuiState, color: u8, clock_24h: bool) {
    let t0 = tick();
    let (mask_color, mask_clock) = (s().mask_color, s().mask_clock);

    let mut db: *mut CfgDb = core::ptr::null_mut();
    /* SAFETY: [`load`] と同じ。書きたいので writable = 1。 */
    let orc = unsafe { cfg_open(&mut db, 1) };
    if orc < 0 || db.is_null() {
        s().save_ticks = tick().wrapping_sub(t0);
        st.cfg.save_ticks = s().save_ticks;
        s().notice = Some(Notice::SaveFailed { stage: STAGE_OPEN, code: orc });
        return;
    }
    /* 予約から消費までの間に DB が差し替わっている可能性がある (CUI の
     * `cfg init` / `rm`)。書ける状態でなければ 1 バイトも書かない。 */
    /* SAFETY: `db` は直前の `cfg_open` が返した接続。 */
    let status = unsafe { cfg_status(db) };
    if status != CFG_OK {
        let crc = unsafe { cfg_close(db) };
        note_close(st, crc);
        s().save_ticks = tick().wrapping_sub(t0);
        st.cfg.save_ticks = s().save_ticks;
        s().notice = Some(Notice::CannotSave { status });
        return;
    }

    let mut stage = STAGE_BEGIN;
    /* SAFETY: 以降はすべて同じ接続への呼び出し。scope / key は NUL 終端の静的
     * バイト列で、値は i32。 */
    let mut work = unsafe { cfg_begin(db) };
    if work >= 0 && mask_color {
        stage = STAGE_SET;
        work = unsafe { cfg_set_int(db, SCOPE.as_ptr(), KEY_COLOR.as_ptr(), color as i32) };
    }
    if work >= 0 && mask_clock {
        stage = STAGE_SET;
        work = unsafe { cfg_set_int(db, SCOPE.as_ptr(), KEY_CLOCK.as_ptr(), clock_24h as i32) };
    }
    if work >= 0 {
        stage = STAGE_COMMIT;
        work = unsafe { cfg_commit(db) };
    }
    /* rollback は `cfg_close` が行う (票 §3)。ここでは呼ばない。 */
    let crc = unsafe { cfg_close(db) };
    let cerr = if crc < 0 {
        let e = unsafe { cfg_last_close_error() };
        if e != 0 {
            e
        } else {
            crc
        }
    } else {
        0
    };
    s().save_ticks = tick().wrapping_sub(t0);
    st.cfg.save_ticks = s().save_ticks;
    if cerr != 0 {
        st.cfg.close_error = cerr;
    }

    if work < 0 {
        /* 保存失敗: **適用値は変えない** (票 §3)。 */
        s().notice = Some(Notice::SaveFailed { stage, code: work });
        return;
    }
    /* commit 済みの値が正。**適用値を更新して画面に反映してから**、close の
     * 失敗を別のメッセージで伝える (DB と画面をずらさない。票 §3 の B2)。 */
    apply(st, color, clock_24h);
    if cerr != 0 {
        s().notice = Some(Notice::SavedCloseFailed { code: cerr });
    }
}

/// `cfg_close` の戻りを診断へ積む (書かずに閉じた経路用)。
fn note_close(st: &mut GuiState, crc: i32) {
    if crc >= 0 {
        return;
    }
    let e = unsafe { cfg_last_close_error() };
    st.cfg.close_error = if e != 0 { e } else { crc };
}

/// 適用値を更新して画面に反映する (保存が成功したときだけ)。
fn apply(st: &mut GuiState, color: u8, clock_24h: bool) {
    let color_changed = st.cfg.desktop_color != color;
    let clock_changed = st.cfg.clock_24h != clock_24h;
    st.cfg.desktop_color = color;
    st.cfg.clock_24h = clock_24h;
    /* 再読込値も新しい値に合わせる (もう一度 OK を押しても「変更なし」)。 */
    {
        let ss = s();
        ss.base_color = color;
        ss.base_clock = clock_24h;
    }
    if color_changed {
        /* デスクトップ全体。窓のクライアント面は WM が持たないので触らない
         * (合成が背景とクロームだけを描き直す)。 */
        let full = Rect::new(0, 0, st.screen_w, st.screen_h);
        st.dirty_screen(full);
    }
    if clock_changed {
        /* 幅が変わるので旧 ∪ 新 + 窓ボタン帯 (票 §1 の B4)。整形と損傷は
         * `taskbar` の持ち物なので、次の tick で作り直させる。 */
        taskbar::invalidate_clock(st);
    }
}

/* ================================================================ */
/*  文言                                                             */
/* ================================================================ */

/// 通知を組む一時バッファの大きさ。
pub const MSG_MAX: usize = 128;

/* ---- 状態行 (実装レビュー往復 1 の B3) ----
 *
 * 1 行に連結すると `settings.db: MISSING - run 'cfg init' in CUI close failed
 * (5)  load 0t save 0t` = 78 文字 = 624px になり、640px 画面のダイアログ
 * (上限 624px) の枠外へ描いてしまう (`kcg_draw_utf8` にクリップは無い)。
 * **状態 / close 診断 / 計測を別の行に分け**、版面の幅と高さを実表示幅から
 * 出す。 */

/// 状態行 1 本の最大バイト数 (NUL 込み)。最長は
/// `settings.db: MISSING - run 'cfg init' in CUI` = 44 文字。
pub const STATUS_LINE_CAP: usize = 64;
/// 状態行の最大本数 (状態 / close 診断 / 計測)。
pub const STATUS_LINES_MAX: usize = 3;

/// `CFG_*` → 表示名。
pub fn status_word(status: i32) -> &'static [u8] {
    match status {
        CFG_OK => b"OK",
        CFG_MISSING => b"MISSING",
        CFG_CORRUPT => b"CORRUPT",
        CFG_VERSION => b"VERSION",
        CFG_ERROR => b"ERROR",
        _ => b"open failed",
    }
}

fn stage_word(stage: u8) -> &'static [u8] {
    match stage {
        STAGE_OPEN => b"open",
        STAGE_BEGIN => b"begin",
        STAGE_SET => b"set",
        _ => b"commit",
    }
}

fn put(buf: &mut [u8; MSG_MAX], n: &mut usize, s: &[u8]) {
    let mut i = 0;
    while i < s.len() && s[i] != 0 && *n < MSG_MAX - 1 {
        buf[*n] = s[i];
        *n += 1;
        i += 1;
    }
}

fn put_i32(buf: &mut [u8; MSG_MAX], n: &mut usize, v: i32) {
    if v < 0 {
        put(buf, n, b"-");
    }
    /* -2147483648 の絶対値は i32 に収まらないので u32 で数える。 */
    let mut m = if v < 0 { (v as i64).unsigned_abs() as u32 } else { v as u32 };
    let mut d = [0u8; 10];
    let mut k = 0;
    loop {
        d[k] = b'0' + (m % 10) as u8;
        m /= 10;
        k += 1;
        if m == 0 {
            break;
        }
    }
    while k > 0 {
        k -= 1;
        let c = [d[k]];
        put(buf, n, &c);
    }
}

/// 通知の本文を組む (NUL 終端は呼び出し側が `buf[len]` を使う)。戻り値は長さ。
pub fn notice_text(n: &Notice, buf: &mut [u8; MSG_MAX]) -> usize {
    let mut len = 0usize;
    match *n {
        Notice::Startup {
            status,
            schema,
            sqlite,
            close_error,
        } => {
            match status {
                CFG_OK => {}
                CFG_MISSING => put(
                    buf,
                    &mut len,
                    b"Settings: MISSING - defaults in use. Run 'cfg init' in CUI.",
                ),
                CFG_CORRUPT => put(buf, &mut len, b"Settings: CORRUPT - defaults in use"),
                CFG_VERSION => {
                    put(buf, &mut len, b"Settings: VERSION ");
                    put_i32(buf, &mut len, schema);
                    put(buf, &mut len, b" (read only)");
                }
                CFG_ERROR => {
                    put(buf, &mut len, b"Settings: ERROR sqlite=");
                    put_i32(buf, &mut len, sqlite);
                    put(buf, &mut len, b" - defaults in use");
                }
                _ => {
                    /* `cfg_open` が負。票の 5 文言には無いので専用の 1 行
                     * (sqlite コードではないものを sqlite= とは書かない)。 */
                    put(buf, &mut len, b"Settings: ERROR open=");
                    put_i32(buf, &mut len, sqlite);
                    put(buf, &mut len, b" - defaults in use");
                }
            }
            if close_error != 0 {
                if len > 0 {
                    put(buf, &mut len, b"; ");
                } else {
                    put(buf, &mut len, b"Settings: ");
                }
                put(buf, &mut len, b"close failed (");
                put_i32(buf, &mut len, close_error);
                put(buf, &mut len, b")");
            }
        }
        Notice::CannotSave { status } => {
            put(buf, &mut len, b"cannot save: ");
            put(buf, &mut len, status_word(status));
        }
        Notice::SaveFailed { stage, code } => {
            put(buf, &mut len, b"save failed: ");
            put(buf, &mut len, stage_word(stage));
            put(buf, &mut len, b" (");
            put_i32(buf, &mut len, code);
            put(buf, &mut len, b")");
        }
        Notice::SavedCloseFailed { code } => {
            put(buf, &mut len, b"saved, but close failed (");
            put_i32(buf, &mut len, code);
            put(buf, &mut len, b")");
        }
    }
    buf[len] = 0;
    len
}

/// 設定ダイアログの状態行 (票 §3)。**最大 3 行**に分けて `out` へ NUL 終端で
/// 書き、実際に書いた本数を返す (実装レビュー往復 1 の B3)。
///
/// | 行 | 中身 | いつ |
/// |---|---|---|
/// | 0 | `settings.db: <status>` | 常に |
/// | 1 | `close failed (<code>)` | `close_error != 0` のときだけ |
/// | 2 | `load <n>t save <n>t` | 常に (受入 G7 の採取経路) |
pub fn status_lines(
    cfg: &GuiCfg,
    out: &mut [[u8; STATUS_LINE_CAP]; STATUS_LINES_MAX],
) -> usize {
    let mut n = 0usize;

    /* 1 行目: 状態そのもの。 */
    {
        let mut len = 0usize;
        let b = &mut out[n];
        putn(b, &mut len, b"settings.db: ");
        match cfg.status {
            CFG_OK => putn(b, &mut len, b"OK"),
            CFG_MISSING => putn(b, &mut len, b"MISSING - run 'cfg init' in CUI"),
            CFG_CORRUPT => putn(b, &mut len, b"CORRUPT"),
            CFG_VERSION => {
                putn(b, &mut len, b"VERSION ");
                putn_i32(b, &mut len, cfg.schema_version);
                putn(b, &mut len, b" (read only)");
            }
            CFG_ERROR => {
                putn(b, &mut len, b"ERROR sqlite=");
                putn_i32(b, &mut len, cfg.sqlite);
            }
            _ => {
                putn(b, &mut len, b"ERROR open=");
                putn_i32(b, &mut len, cfg.sqlite);
            }
        }
        b[len] = 0;
        n += 1;
    }

    /* 2 行目: close の診断 (あるときだけ)。 */
    if cfg.close_error != 0 {
        let mut len = 0usize;
        let b = &mut out[n];
        putn(b, &mut len, b"close failed (");
        putn_i32(b, &mut len, cfg.close_error);
        putn(b, &mut len, b")");
        b[len] = 0;
        n += 1;
    }

    /* 3 行目: 計測 (S5 の材料、受入 G7)。 */
    {
        let mut len = 0usize;
        let b = &mut out[n];
        putn(b, &mut len, b"load ");
        putn_i32(b, &mut len, cfg.load_ticks as i32);
        putn(b, &mut len, b"t save ");
        putn_i32(b, &mut len, cfg.save_ticks as i32);
        putn(b, &mut len, b"t");
        b[len] = 0;
        n += 1;
    }
    n
}

/* ---- 状態行用の put (幅の違う buf を扱うので MSG_MAX 版と別にする) ---- */

fn putn(buf: &mut [u8; STATUS_LINE_CAP], n: &mut usize, s: &[u8]) {
    let mut i = 0;
    while i < s.len() && s[i] != 0 && *n < STATUS_LINE_CAP - 1 {
        buf[*n] = s[i];
        *n += 1;
        i += 1;
    }
}

fn putn_i32(buf: &mut [u8; STATUS_LINE_CAP], n: &mut usize, v: i32) {
    let mut tmp = [0u8; MSG_MAX];
    let mut k = 0usize;
    put_i32(&mut tmp, &mut k, v);
    putn(buf, n, &tmp[..k]);
}

/* ================================================================ */
/*  kprintf (con_sink に入るので端末アプリで読める。票 §2 の B3)      */
/* ================================================================ */

fn kprint(line: &[u8]) {
    /* SAFETY: KAPI の関数表は `os32_init` が据えたもの。`kprintf` は可変長
     * だが、渡すのは `"%s"` と NUL 終端の 1 本だけ。 */
    unsafe {
        (os32api::api().kprintf)(os32api::ATTR_WHITE, b"%s\0".as_ptr(), line.as_ptr());
    }
}

/// `gshell: cfg <status> color=<n> clock24=<0/1> load=<n>t` を 1 行。
fn log_startup(cfg: &GuiCfg) {
    let mut buf = [0u8; MSG_MAX];
    let mut len = 0usize;
    put(&mut buf, &mut len, b"gshell: cfg ");
    put(&mut buf, &mut len, status_word(cfg.status));
    put(&mut buf, &mut len, b" color=");
    put_i32(&mut buf, &mut len, cfg.desktop_color as i32);
    put(&mut buf, &mut len, b" clock24=");
    put_i32(&mut buf, &mut len, cfg.clock_24h as i32);
    put(&mut buf, &mut len, b" load=");
    put_i32(&mut buf, &mut len, cfg.load_ticks as i32);
    put(&mut buf, &mut len, b"t\n");
    buf[len] = 0;
    kprint(&buf[..=len]);
}

#[cfg(test)]
#[path = "../host/settings_tests.rs"]
mod tests;
