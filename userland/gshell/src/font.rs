//! font.rs — gshell の字形 engine の持ち主 (T7b-b3 S3)。
//!
//! 設計: `~/os32-tmp/run/t7bb34/DESIGN.final.md` §2-1〜§2-5、b1/b2 は
//! `~/os32-tmp/run/t7bb12/DESIGN.final.md` §3。
//!
//! | 何 | どこで | 形 |
//! |---|---|---|
//! | Engine 本体 (Face×2・raster scratch・cache の静的部) | .bss (`FONT`) | `Engine<ShellSource>` |
//! | cache の arena (`ARENA_BYTES_SHELL` 128KiB) | .bss (`ARENA`、TASK_FONTPERF F2) | 返さない (trim に結ばない) |
//! | lease の open / close | top-level (段 B の open 文脈、WM 外・guard 外) | [`open_at_safe_point`] / [`close_all`] |
//! | `Face::open` の読込み、miss の raster + commit | `font_guard_run` の callback の中 | [`open_cb`] / [`raster_cb`] |
//! | 描画 (hit の借用) | guard の外 (x87 不要) | `text.rs` → [`glyph`] |
//!
//! callback の中では service の `read_at` 以外を呼ばない (kernel/font_guard.h)。
//! `Engine::open` が失敗して Face を閉じると `ShellSource` が drop されるが、
//! そのときの `font_source_close` は guard を抜けるまで保留する ([`close_lease`])。
//!
//! service 表の関数は戻り番地で呼び手を確かめる (段 A) ので、呼ぶのは戻り値を
//! 局所に受けて使ってから返す薄い `#[inline(never)]` 関数だけにする
//! (尾呼出しにしない、b12 設計 §3-2 の注記)。
//!
//! 設定 (`gshell` scope の `font_set`) が変わったら [`reconfigure`]: close →
//! open (Engine が invalidate_all → set_slot+1) → tracked app 全部に
//! `GUI_EV_FONT` → 全クライアントと WM の chrome を描き直す。open の失敗は
//! `KcgOnly(..)` にして、次の `settings::consume` で 1 回だけ開き直す
//! ([`consume_retry`])。

use core::cell::UnsafeCell;

use os32_font::cache::Stats;
use os32_font::engine::{Cell, Engine, EngineError, Glyph, Reason, Resolved, State};
use os32_font::limits::{layout, ARENA_BYTES_SHELL, FONT_SET_KCG_ONLY, N32};
use os32_font::paths::{FONT_JA_C, FONT_LATIN_C};
use os32_font::raster::PixelSize;
use os32_font::reader::{Error as FontError, Result as FontResult, Source};
use os32api::gui::proto::GUI_EV_FONT;

use crate::ffi::{self, ShellService, ShellServiceState, OS32_ERR_BUSY, OS32_ERR_NOSPC, OS32_ERR_NOSYS};
use crate::wm::{GuiState, Rect};
use crate::{damage, gserial, multiapp, ring, text};

/* ================================================================ */
/*  service の薄い呼び手                                              */
/* ================================================================ */

#[inline(never)]
fn svc_open(t: &ShellService, path: &[u8], id: &mut u32, size: &mut u32) -> i32 {
    let f = match t.font_source_open { Some(f) => f, None => return OS32_ERR_NOSYS };
    let r = unsafe { f(path.as_ptr(), id as *mut u32, size as *mut u32) };
    core::hint::black_box(r)
}

#[inline(never)]
fn svc_read_at(t: &ShellService, id: u32, offset: u32, dst: &mut [u8]) -> i32 {
    let f = match t.font_source_read_at { Some(f) => f, None => return OS32_ERR_NOSYS };
    let r = unsafe { f(id, offset, dst.as_mut_ptr(), dst.len() as u32) };
    core::hint::black_box(r)
}

#[inline(never)]
fn svc_validate(t: &ShellService, id: u32) -> i32 {
    let f = match t.font_source_validate { Some(f) => f, None => return OS32_ERR_NOSYS };
    let r = unsafe { f(id) };
    core::hint::black_box(r)
}

#[inline(never)]
fn svc_close(t: &ShellService, id: u32) -> i32 {
    let f = match t.font_source_close { Some(f) => f, None => return OS32_ERR_NOSYS };
    let r = unsafe { f(id) };
    core::hint::black_box(r)
}

type GuardCall = unsafe extern "C" fn(ctx: *mut u8) -> i32;

#[inline(never)]
fn svc_guard_run(t: &ShellService, call: GuardCall, ctx: *mut u8) -> i32 {
    let f = match t.font_guard_run { Some(f) => f, None => return OS32_ERR_NOSYS };
    let r = unsafe { f(Some(call), ctx) };
    core::hint::black_box(r)
}

/* ================================================================ */
/*  Source (shell lease)                                             */
/* ================================================================ */

/// shell lease 1 本 (service `font_source_*`)。drop で閉じる。
pub struct ShellSource {
    id: u32,
    size: u32,
}

impl Source for ShellSource {
    fn file_size(&self) -> u64 {
        u64::from(self.size)
    }
    /// service は u32 の offset。超えるのは Bounds、負の戻りは Io、短読はそのまま。
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> FontResult<usize> {
        if offset > u64::from(u32::MAX) || dst.len() > u32::MAX as usize {
            return Err(FontError::Bounds);
        }
        let t = match ffi::shell_service() { Some(t) => t, None => return Err(FontError::Io) };
        let r = svc_read_at(t, self.id, offset as u32, dst);
        if r < 0 {
            return Err(FontError::Io);
        }
        Ok((r as usize).min(dst.len()))
    }
    /// 入口 1 回の検証 (owner・FD 世代・STALE) は service の validate で。
    fn validate(&mut self) -> FontResult<()> {
        let t = match ffi::shell_service() { Some(t) => t, None => return Err(FontError::Io) };
        if svc_validate(t, self.id) < 0 {
            return Err(FontError::Io);
        }
        Ok(())
    }
}

impl Drop for ShellSource {
    fn drop(&mut self) {
        close_lease(self.id);
    }
}

/* ================================================================ */
/*  観測点 (ゲストから名前で読む)                                     */
/* ================================================================ */

/// `gshell_font_stats.state` の値。
pub const ST_CLOSED: u32 = 0;
pub const ST_READY: u32 = 1;
pub const ST_KCG_ONLY: u32 = 2;

/// gshell の font の状態と数え上げ (ゲストの `emu_read_mem` 用)。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct FontStats {
    /// `ST_*`。
    pub state: u32,
    /// `KcgOnly` の理由 (`Reason as u32`、Ready/Closed は 0)。
    pub reason: u32,
    /// 開いている設定 (`font_set`)。
    pub font_set: u32,
    /// Engine の `set_slot` (open ごとに +1)。
    pub set_slot: u32,
    pub opens: u32,
    pub open_fail: u32,
    /// 直近の open の戻り (0 = OK)。
    pub last_rc: i32,
    /// `font_source_close` を呼んだ回数 (lease の後始末)。
    pub closes: u32,
    pub guard_runs: u32,
    /// 描画段の peek が Miss だった字 (slot に入らない字・guard BUSY 後) → KCG。
    pub over_slot: u32,
    /// `text.rs` の描画呼出し。
    pub texts: u32,
    pub ev_font_sent: u32,
    /// ring 満杯で `GUI_EV_FONT` を置けなかった app (font/gen で拾う)。
    pub ev_font_full: u32,
    pub n_valid: u32,
    pub retries: u32,
    pub cache: Stats,
}

impl FontStats {
    const ZERO: FontStats = unsafe { core::mem::zeroed() };
}

#[no_mangle]
pub static mut gshell_font_stats: FontStats = FontStats::ZERO;

/// 開いた直後の見本 1 字 ('亜' U+4E9C、Full、16px) の寸法と bitmap。b3⑦ で
/// font の差し替えが次の起動で効いたかを比べる (`gshell_font_stats` の隣)。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct FontSample {
    pub cp: u32,
    /// 0 = 無し、1 = Bitmap、他は fallback。
    pub kind: u32,
    pub left: i16,
    pub top: i16,
    pub width: u16,
    pub height: u16,
    pub pitch: u16,
    pub pad: u16,
    pub bits: [u8; SAMPLE_BYTES],
}
const SAMPLE_BYTES: usize = 64;
const SAMPLE_CP: u32 = 0x4E9C;

#[no_mangle]
pub static mut gshell_font_sample: FontSample = unsafe { core::mem::zeroed() };

fn stats() -> &'static mut FontStats {
    unsafe { &mut *core::ptr::addr_of_mut!(gshell_font_stats) }
}

/* ================================================================ */
/*  私有状態                                                         */
/* ================================================================ */

const DEFER_MAX: usize = 4;

struct Font {
    engine: Engine<ShellSource>,
    /// `font_guard_run` の callback の中 (close を保留する)。
    in_guard: bool,
    deferred: [u32; DEFER_MAX],
    n_deferred: usize,
    /// arena を渡した (attach が断っても 2 度目はしない — uncached = KCG)。
    arena_tried: bool,
    /// 次の `consume` で 1 回だけ開き直す。
    retry: bool,
    font_set: u8,
}

impl Font {
    const NEW: Font = Font {
        engine: Engine::new(),
        in_guard: false,
        deferred: [0; DEFER_MAX],
        n_deferred: 0,
        arena_tried: false,
        retry: false,
        font_set: 0,
    };
}

struct FontCell(UnsafeCell<Font>);
// OS32 はシングルタスク・協調切替。描画・open の途中で park / yield しない。
unsafe impl Sync for FontCell {}
static FONT: FontCell = FontCell(UnsafeCell::new(Font::NEW));

/// cache の arena (fontperf 設計 §5): exec heap の peak を増やさないよう像の .bss に置く。
/// 渡すのは `arena_once` の 1 回だけ (試験の `reset` は古い Engine を捨ててから)。
struct ArenaCell(UnsafeCell<[u8; ARENA_BYTES_SHELL]>);
unsafe impl Sync for ArenaCell {}
static ARENA: ArenaCell = ArenaCell(UnsafeCell::new([0; ARENA_BYTES_SHELL]));

#[inline]
fn f() -> &'static mut Font {
    unsafe { &mut *FONT.0.get() }
}

/// 試験用: Engine を作り直す (lease は drop で閉じる)。
#[cfg(test)]
pub fn reset() {
    f().in_guard = false;
    f().n_deferred = 0;
    /* 古い Engine の drop が lease を閉じる (close_lease が f() を見るので外へ出してから)。 */
    let old = core::mem::replace(&mut f().engine, Engine::new());
    drop(old);
    *f() = Font::NEW;
    unsafe {
        gshell_font_stats = FontStats::ZERO;
        gshell_font_sample = core::mem::zeroed();
    }
}

/// 試験の観測点。
#[cfg(test)]
pub fn engine() -> &'static Engine<ShellSource> {
    &f().engine
}

/// 試験の観測点: 次の consume で開き直すか。
#[cfg(test)]
pub fn retry_armed() -> bool {
    f().retry
}

/// lease を閉じる。guard の中なら抜けてから ([`flush_deferred`])。
///
/// WM の handler / pump の中 (段 B が WM 外の close を断る文脈) で入口の検証が
/// Stale になると、`Engine::begin` が face を閉じてここへ来る。service はそこで
/// close を INVAL で断る (lease は retire まで残る) ので、**断られた close も保留**
/// し、次の top-level の安全点 ([`consume_retry`] の頭) で 1 回だけ閉じ直す
/// (Fable の S3 レビュー、設計 t7bb34「S3・S4 の取り込みで送る項目」)。
fn close_lease(id: u32) {
    if f().in_guard {
        defer_close(id);
        return;
    }
    if let Some(t) = ffi::shell_service() {
        if svc_close(t, id) < 0 {
            defer_close(id);
        } else {
            stats().closes = stats().closes.wrapping_add(1);
        }
    }
}

fn defer_close(id: u32) {
    let font = f();
    if font.n_deferred < DEFER_MAX {
        font.deferred[font.n_deferred] = id;
        font.n_deferred += 1;
    }
}

/// 保留した close を閉じ直す。`keep` = 断られたらまた保留する (guard を抜けた
/// 直後 — WM の中かもしれない)。top-level の安全点では `false` (断られたら諦め、
/// 残りは retire が閉じる — 同じ id を毎周叩かない)。
fn flush_deferred_ex(keep: bool) {
    let font = f();
    let n = font.n_deferred;
    let ids = font.deferred;
    font.n_deferred = 0;
    let t = ffi::shell_service();
    let mut i = 0;
    while i < n {
        let id = ids[i];
        i += 1;
        let t = match t {
            Some(t) => t,
            None => continue,
        };
        if svc_close(t, id) < 0 {
            if keep {
                defer_close(id);
            }
        } else {
            stats().closes = stats().closes.wrapping_add(1);
        }
    }
}

fn flush_deferred() {
    flush_deferred_ex(true);
}

/// 試験の観測点: 保留中の close の本数。
#[cfg(test)]
pub fn deferred_closes() -> usize {
    f().n_deferred
}

/// guard の中で `call(ctx)` を 1 回。戻りは callback の戻りか guard の誤り。
fn guard_run(t: &ShellService, call: GuardCall, ctx: *mut u8) -> i32 {
    stats().guard_runs = stats().guard_runs.wrapping_add(1);
    f().in_guard = true;
    let rc = svc_guard_run(t, call, ctx);
    f().in_guard = false;
    flush_deferred();
    rc
}

fn sync_stats() {
    let font = f();
    let s = stats();
    let (state, reason) = match font.engine.state() {
        State::Closed => (ST_CLOSED, 0),
        State::Ready => (ST_READY, 0),
        State::KcgOnly(r) => (ST_KCG_ONLY, r as u32),
    };
    s.state = state;
    s.reason = reason;
    s.font_set = u32::from(font.font_set);
    s.set_slot = u32::from(font.engine.set_slot());
    s.n_valid = font.engine.cache().n_valid() as u32;
    s.cache = *font.engine.stats();
}

/* ================================================================ */
/*  open / close (top-level の安全点だけ)                            */
/* ================================================================ */

/// `open_cb` へ渡す lease 2 本 (callback が取る、取られなければ guard の外で drop)。
struct OpenJob {
    latin: Option<ShellSource>,
    ja: Option<ShellSource>,
}

/// 結果の語 (ログ用)。
fn reason_word(state: State) -> &'static [u8] {
    match state {
        State::Ready => b"OK",
        State::Closed => b"CLOSED",
        State::KcgOnly(Reason::Config) => b"KCG(config)",
        State::KcgOnly(Reason::NoFpu) => b"KCG(nofpu)",
        State::KcgOnly(Reason::OldShell) => b"KCG(oldshell)",
        State::KcgOnly(Reason::Nospc) => b"KCG(nospc)",
        State::KcgOnly(Reason::Busy) => b"KCG(busy)",
        State::KcgOnly(Reason::Stale) => b"KCG(stale)",
        State::KcgOnly(Reason::OpenFailed) => b"KCG(failed)",
    }
}

unsafe extern "C" fn open_cb(ctx: *mut u8) -> i32 {
    let job = &mut *(ctx as *mut OpenJob);
    let (latin, ja) = match (job.latin.take(), job.ja.take()) {
        (Some(l), Some(j)) => (l, j),
        _ => return -1,
    };
    let eng = &mut f().engine;
    let rc = match eng.open(latin, ja) {
        Ok(()) => 0,
        Err(EngineError::Busy) => -2,
        Err(EngineError::Font(_)) => -1,
    };
    rc
}

/// 見本 1 字を引いて `gshell_font_sample` に写す (b3⑦)。入口の検証は guard の
/// 外 (`begin`)、raster は guard の中 ([`sample_cb`])。
fn sample_once(t: &ShellService) {
    let eng = &mut f().engine;
    if !eng.begin() {
        return;
    }
    let _ = guard_run(t, sample_cb, core::ptr::null_mut());
    f().engine.end();
}

unsafe extern "C" fn sample_cb(_ctx: *mut u8) -> i32 {
    take_sample(&mut f().engine);
    0
}

fn take_sample(eng: &mut Engine<ShellSource>) {
    let mut g = Glyph::default();
    let r = eng.resolve(SAMPLE_CP, Cell::Full, PixelSize::Px16, &mut g);
    let sample = unsafe { &mut *core::ptr::addr_of_mut!(gshell_font_sample) };
    *sample = unsafe { core::mem::zeroed() };
    sample.cp = SAMPLE_CP;
    sample.kind = match r {
        Resolved::Bitmap => 1,
        Resolved::Space => 2,
        Resolved::Kcg => 3,
        Resolved::Missing => 4,
        Resolved::Miss => 5,
    };
    sample.left = g.entry.left;
    sample.top = g.entry.top;
    sample.width = g.entry.width;
    sample.height = g.entry.height;
    sample.pitch = g.entry.pitch;
    let bits = eng.bitmap(&g);
    let n = bits.len().min(SAMPLE_BYTES);
    sample.bits[..n].copy_from_slice(&bits[..n]);
    eng.release();
}

/// 開いた結果 (lease の open 失敗は rc を、Engine の open 失敗は -1/-2 を持つ)。
fn fail(reason: Reason, rc: i32, arm_retry: bool) -> i32 {
    let _ = f().engine.set_kcg_only(reason);
    if arm_retry && matches!(reason, Reason::OpenFailed | Reason::Nospc | Reason::Busy) {
        f().retry = true;
    }
    rc
}

fn arena_once() {
    let font = f();
    if font.arena_tried || font.engine.cache().has_arena() {
        return;
    }
    font.arena_tried = true;
    /* .bss の arena を 1 回だけ渡す (mem_alloc はしない)。attach が断れば cache 無し
     * = 描画段の peek が全部 Miss → KCG。 */
    let arena: &'static mut [u8] = unsafe { &mut *ARENA.0.get() };
    let (n_cell, n16) = layout(ARENA_BYTES_SHELL);
    let _ = font.engine.attach_arena(arena, n_cell, n16, N32);
}

fn open_inner(font_set: u8, arm_retry: bool) -> i32 {
    let font = f();
    font.font_set = font_set;
    font.retry = false;
    /* 旧 set を閉じる (lease の close はここ = guard の外)。 */
    if font.engine.close().is_err() {
        return fail(Reason::Busy, OS32_ERR_BUSY, arm_retry);
    }
    if u16::from(font_set) == FONT_SET_KCG_ONLY {
        return fail(Reason::Config, 0, false);
    }
    let t = match ffi::shell_service_state() {
        ShellServiceState::Ready(t) => t,
        ShellServiceState::OldShell => return fail(Reason::OldShell, OS32_ERR_NOSYS, false),
        ShellServiceState::Absent => return fail(Reason::OpenFailed, OS32_ERR_NOSYS, false),
    };
    arena_once();
    let mut sources = [None, None];
    for (k, path) in [FONT_LATIN_C, FONT_JA_C].iter().enumerate() {
        let (mut id, mut size) = (0u32, 0u32);
        let rc = svc_open(t, path, &mut id, &mut size);
        if rc < 0 {
            /* 開けた方は sources の drop (guard の外) で閉じる。 */
            let reason = match rc {
                OS32_ERR_NOSYS => Reason::NoFpu,
                OS32_ERR_NOSPC => Reason::Nospc,
                _ => Reason::OpenFailed,
            };
            return fail(reason, rc, arm_retry);
        }
        sources[k] = Some(ShellSource { id, size });
    }
    let [latin, ja] = sources;
    let mut job = OpenJob { latin, ja };
    let rc = guard_run(t, open_cb, &mut job as *mut OpenJob as *mut u8);
    /* callback が取らなかった lease (NOSYS / BUSY) はここ (guard の外) で閉じる。 */
    drop(job);
    match rc {
        0 => {
            sample_once(t);
            0
        }
        OS32_ERR_NOSYS => fail(Reason::NoFpu, rc, false),
        OS32_ERR_BUSY => fail(Reason::Busy, rc, arm_retry),
        _ => fail(Reason::OpenFailed, rc, arm_retry),
    }
}

/// 設定 `font_set` で開く (起動時・設定変更・再試行)。**呼べるのは top-level の
/// 安全点だけ** (gui_register の後の main、`settings::consume`) — lease の open は
/// 段 B で WM 中を断る。戻り値は 0 = Ready (または設定で KCG のみ)、負は開けな
/// かった rc (KCG で描く)。
pub fn open_at_safe_point(st: &GuiState, font_set: u8) -> i32 {
    open_set(st, font_set, true)
}

fn open_set(st: &GuiState, font_set: u8, arm_retry: bool) -> i32 {
    let before = f().engine.set_slot();
    let rc = open_inner(font_set, arm_retry);
    let s = stats();
    s.last_rc = rc;
    if f().engine.state() == State::Ready {
        s.opens = s.opens.wrapping_add(1);
    } else if !matches!(f().engine.state(), State::KcgOnly(Reason::Config)) {
        s.open_fail = s.open_fail.wrapping_add(1);
    }
    sync_stats();
    log_open(st, before, rc);
    rc
}

/// 設定変更 (`settings::apply` から、top-level)。開き直し → tracked app 全部に
/// `GUI_EV_FONT` → 全クライアントと WM の chrome を描き直す。
pub fn reconfigure(st: &mut GuiState, font_set: u8) {
    open_at_safe_point(st, font_set);
    deliver_ev_font(st);
    damage::invalidate_all_clients(st);
    let full = Rect::new(0, 0, st.screen_w, st.screen_h);
    st.dirty_screen(full);
}

/// 前回の open 失敗 (`OpenFailed` / `Nospc` / `Busy`) を 1 回だけ開き直す
/// (`settings::consume` の頭、top-level)。その前に WM の中で断られた close を
/// 1 回だけ閉じ直す ([`close_lease`])。戻り値 `true` = 開き直した。
pub fn consume_retry(st: &mut GuiState) -> bool {
    /* WM の中で断られた close (Stale) を先に閉じる。 */
    if f().n_deferred > 0 {
        flush_deferred_ex(false);
        sync_stats();
    }
    if !f().retry {
        return false;
    }
    f().retry = false;
    stats().retries = stats().retries.wrapping_add(1);
    let font_set = f().font_set;
    if open_set(st, font_set, false) == 0 {
        /* WM 自身の文字だけ描き直す (app は自分の engine を持つ)。 */
        let full = Rect::new(0, 0, st.screen_w, st.screen_h);
        st.dirty_screen(full);
    }
    true
}

/// GUI を抜ける (CUI 切替・halt)。lease を全部閉じる (retire も残りを閉じる)。
pub fn close_all() {
    let _ = f().engine.close();
    f().retry = false;
    flush_deferred_ex(false);
    sync_stats();
}

/// cache を空にする (face の read cache も)。arena と lease は保つ。
/// gshell は trim を font に結ばない (設計 §2-3、arena は .bss) — host 試験用。
#[cfg_attr(not(test), allow(dead_code))]
pub fn trim() -> bool {
    let ok = f().engine.trim().is_ok();
    sync_stats();
    ok
}

/// tracked app 全部 (前景を含む) の ring に `GUI_EV_FONT` を 1 件。満杯は数えるだけ
/// (app は前景化と paint の周で `font/gen` を照合して拾う)。
fn deliver_ev_font(st: &GuiState) {
    let ev = ring::ev_simple(GUI_EV_FONT, 0, 0);
    let mut id = multiapp::APP_ID_MIN;
    while id <= multiapp::APP_ID_MAX {
        if multiapp::is_tracked(id) {
            if let Some(slot) = st.slot_of_owner(id) {
                let s = stats();
                if ring::append(st, slot, &ev) {
                    s.ev_font_sent = s.ev_font_sent.wrapping_add(1);
                } else {
                    s.ev_font_full = s.ev_font_full.wrapping_add(1);
                }
            }
        }
        id += 1;
    }
}

/* ================================================================ */
/*  描画 (text.rs から。WM の handler / pump の中でもよい)            */
/* ================================================================ */

/// raster callback の仕事: この text() の TTF 経路の字。
struct RasterJob {
    s: *const u8,
    len: usize,
}

unsafe extern "C" fn raster_cb(ctx: *mut u8) -> i32 {
    let job = &*(ctx as *const RasterJob);
    let s = core::slice::from_raw_parts(job.s, job.len);
    let eng = &mut f().engine;
    for (cp, cell) in text::ttf_glyphs(s) {
        let mut g = Glyph::default();
        let _ = eng.resolve(cp, cell, PixelSize::Px16, &mut g);
        eng.release();
    }
    0
}

/// text() の入口: lease を face ごとに 1 回検証し、cache に無い字があれば guard の
/// 中で 1 回だけ raster する。戻り値 `false` = この呼出しは全部 KCG。
/// 必ず [`end_text`] と対で呼ぶ。
pub fn begin_text(s: &[u8]) -> bool {
    stats().texts = stats().texts.wrapping_add(1);
    let font = f();
    if !font.engine.begin() {
        /* Stale ならここで KcgOnly(Stale) になり lease は閉じた。 */
        return false;
    }
    if !font.engine.need_raster(PixelSize::Px16, text::ttf_glyphs(s).map(|(cp, _)| cp)) {
        return true;
    }
    let t = match ffi::shell_service() {
        Some(t) => t,
        None => return false,
    };
    let mut job = RasterJob { s: s.as_ptr(), len: s.len() };
    let rc = guard_run(t, raster_cb, &mut job as *mut RasterJob as *mut u8);
    if rc == OS32_ERR_NOSYS {
        /* FPU 無し: 以後ずっと KCG。 */
        font.engine.end();
        let _ = font.engine.set_kcg_only(Reason::NoFpu);
        return false;
    }
    if rc == OS32_ERR_BUSY {
        /* 再入・IRQ 文脈: この呼出しだけ KCG。 */
        font.engine.note_guard_busy();
        return false;
    }
    true
}

/// cache の hit だけで 1 字を引く (raster しない)。Bitmap なら slot の bitmap を
/// 借りて `draw` に渡し、終わったら借用を返す。戻り値は描けたか
/// (`Some(true)` = bitmap、`Some(false)` = 空白、`None` = KCG で描く)。
pub fn with_glyph<F: FnOnce(&Glyph, &[u8])>(cp: u32, draw: F) -> Option<bool> {
    let eng = &mut f().engine;
    let mut g = Glyph::default();
    let r = eng.peek(cp, PixelSize::Px16, &mut g);
    match r {
        Resolved::Bitmap => {
            let out = if g.cached {
                draw(&g, eng.bitmap(&g));
                Some(true)
            } else {
                None
            };
            eng.release();
            out
        }
        Resolved::Space => Some(false),
        Resolved::Miss => {
            stats().over_slot = stats().over_slot.wrapping_add(1);
            None
        }
        Resolved::Kcg | Resolved::Missing => None,
    }
}

/// text() の出口。
pub fn end_text() {
    f().engine.end();
    sync_stats();
}

/* ================================================================ */
/*  ログ (con_sink と gserial の通知)                                */
/* ================================================================ */

struct Line {
    b: [u8; 96],
    n: usize,
}
impl Line {
    fn s(&mut self, s: &[u8]) {
        for &c in s {
            if self.n < self.b.len() - 2 {
                self.b[self.n] = c;
                self.n += 1;
            }
        }
    }
    fn dec(&mut self, v: i32) {
        if v < 0 {
            self.s(b"-");
        }
        let mut u = (v as i64).unsigned_abs();
        let mut tmp = [0u8; 20];
        let mut k = 0;
        loop {
            tmp[k] = b'0' + (u % 10) as u8;
            k += 1;
            u /= 10;
            if u == 0 {
                break;
            }
        }
        while k > 0 {
            k -= 1;
            self.s(&[tmp[k]]);
        }
    }
}

/// `font: gen <旧 slot>-><新 slot> open=<OK|KCG(..)> set=<font_set> rc=<rc>`。
fn log_open(st: &GuiState, before: u16, rc: i32) {
    let mut l = Line { b: [0; 96], n: 0 };
    l.s(b"font: gen ");
    l.dec(i32::from(before));
    l.s(b"->");
    l.dec(i32::from(f().engine.set_slot()));
    l.s(b" open=");
    l.s(reason_word(f().engine.state()));
    l.s(b" set=");
    l.dec(i32::from(f().font_set));
    l.s(b" rc=");
    l.dec(rc);
    let n = l.n;
    if let Ok(msg) = core::str::from_utf8(&l.b[..n]) {
        gserial::notify(st, msg);
    }
    l.b[n] = b'\n';
    l.b[n + 1] = 0;
    unsafe {
        (os32api::api().kprintf)(os32api::ATTR_WHITE, b"%s\0".as_ptr(), l.b.as_ptr());
    }
}

/// gserial が開いた後に今の状態を 1 行通知する (起動時の open は gserial より前)。
pub fn announce(st: &GuiState) {
    let mut l = Line { b: [0; 96], n: 0 };
    l.s(b"font: state ");
    l.s(reason_word(f().engine.state()));
    l.s(b" gen ");
    l.dec(i32::from(f().engine.set_slot()));
    l.s(b" set=");
    l.dec(i32::from(f().font_set));
    if let Ok(msg) = core::str::from_utf8(&l.b[..l.n]) {
        gserial::notify(st, msg);
    }
}

#[cfg(test)]
#[path = "../host/font_tests.rs"]
mod tests;
