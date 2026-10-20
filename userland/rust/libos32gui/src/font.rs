//! font.rs — アプリ側の字形 engine (T7b-b3 S4、設計 §2-1・§2-3・§2-5)。
//!
//! ```text
//!   text() (S5) ── ensure_open ──> Engine<AppSource> (exec_heap、mem_alloc 1 回)
//!                                   ├ latin / ja Face ── AppSource ── KAPI font_source_* (v77)
//!                                   ├ RasterScratch
//!                                   └ GlyphCache ── arena 128KiB (mem_alloc 1 回、trim で free)
//! ```
//!
//! - **Engine 本体** (2 face + scratch + cache 文脈) は `ensure_open` が exec_heap に
//!   `mem_alloc` 1 回で取り (LARGE)、**確保した直後に自分で 0 埋め**する (mem_alloc は
//!   0 埋めを約束しない)。0 埋めの状態が `Engine::new()` と等価 (Face::new は全 0) で、
//!   host 試験が Engine<AppSource> で assert する。stack に一時値を作らない
//!   (`ptr::write(p, Engine::new())` は使わない)。trim でも保つ。static はポインタだけ
//!   (shlib の .bss はアプリごとに実頁で複製されるので、本体を .bss に置かない)。
//! - **arena** (`ARENA_BYTES_APP`) は別の `mem_alloc` 1 回 (LARGE)。`trim` で `mem_free`、次の需要で
//!   再確保。取れなければ cache 無し (字は scratch から描く、`stats.uncached`)。
//! - **face** は KAPI `font_source_*` の lease。FPU 無しは kernel が open を NOSYS で
//!   断る → `KcgOnly(NoFpu)`。枠不足 (NOSPC) → `KcgOnly(Nospc)`。
//! - **x87**: `Face::open` と miss の raster は [`fp_scope`] の中 (FNSTENV の環境の保存/復元。
//!   ST は cdecl で呼び手が保存しない約束)。
//! - **失効** (§2-3): `GUI_EV_FONT` → [`reload`]。取りこぼし対策に `font/gen` を
//!   前景化 (EV_FOCUS) と paint 1 周に 1 回だけ照合 ([`check_gen`]、cfgro は毎回
//!   SQLite を開閉するので text() からは読まない)。NOSPC / open 失敗は前景化の
//!   たびに 1 回だけ再試行 ([`on_focus_in`])。
//!
//! - **束** (TASK_FONTPERF F3): face の open の直後に 3 本目の lease で `.g16` を開き
//!   (NOTFOUND / NOSPC は束なし)、先読み節を `APP_PRELOAD_MAX` 字まで cache へ。
//!   束の lease は face と同じ経路 (reload・再試行・Stale) で Engine が drop する。
//!
//! シングルタスク・協調切替。ここの関数はどれも 1 本のループからしか呼ばれない。
#![allow(dead_code)]

use core::cell::UnsafeCell;
use core::mem::{align_of, size_of};
use core::ptr;

use os32_font::bundle::BundleStats;
use os32_font::cache::Stats;
use os32_font::engine::{Engine, Reason, State};
use os32_font::limits::{layout, APP_PRELOAD_MAX, ARENA_BYTES_APP, FONT_SET_KCG_ONLY, FONT_SET_STD, N32};
use os32_font::paths::{FONT_JA_C, FONT_JA_G16_C, FONT_LATIN_C};
use os32_font::reader::{Error, Result as FontResult, Source};
use os32api::gui::proto::{OS32_ERR_IO, OS32_ERR_NOSPC, OS32_ERR_NOSYS};

/// x87 の制御語の既定 (`fninit` 後の値: 全例外マスク・64bit 精度・最近接丸め)。
/// raster はこの CW を前提にする (kernel の font_guard と同じ)。
const X87_CW_STD: u16 = 0x037F;

/// `font/gen` が未設定のときの値 (defaults.tsv の初期値)。
const FONT_GEN_DEFAULT: i32 = 0;

pub type AppEngine = Engine<AppSource>;

/* ================================================================ */
/*  AppSource — KAPI font_source_* (v77) の lease 1 本                */
/* ================================================================ */

/// アプリの font read lease。Drop で `font_source_close` (Face の close / 再 open /
/// open 失敗が Source を捨てると閉じる)。fault・終了は kernel の
/// `font_lease_close_owned` が回収する。
pub struct AppSource {
    id: u32,
    size: u32,
}

impl AppSource {
    /// `path` は NUL 終端。負は KAPI の戻り値 (NOSYS / NOSPC / NOTFOUND …)。
    fn open(path: &[u8]) -> Result<Self, i32> {
        let mut id = 0u32;
        let mut size = 0u32;
        let rc = unsafe { (os32api::api().font_source_open)(path.as_ptr(), &mut id, &mut size) };
        if rc < 0 {
            Err(rc)
        } else {
            Ok(Self { id, size })
        }
    }
}

impl Source for AppSource {
    fn file_size(&self) -> u64 {
        u64::from(self.size)
    }
    /// 短読はそのまま返す。offset / 長さが u32 を超えるものは Bounds、負の戻りは Io。
    /// 長さ 0 は kernel が lease を検証してから 0 を返す (text() 入口の 1 回検証)。
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> FontResult<usize> {
        let off = u32::try_from(offset).map_err(|_| Error::Bounds)?;
        let len = u32::try_from(dst.len()).map_err(|_| Error::Bounds)?;
        let rc = unsafe { (os32api::api().font_source_read_at)(self.id, off, dst.as_mut_ptr(), len) };
        if rc < 0 || rc as u32 > len {
            Err(Error::Io)
        } else {
            Ok(rc as usize)
        }
    }
}

impl Drop for AppSource {
    fn drop(&mut self) {
        unsafe {
            (os32api::api().font_source_close)(self.id);
        }
    }
}

/* ================================================================ */
/*  x87 の範囲 (設計 §2-5 app 側)                                     */
/* ================================================================ */

/// `f` を既定の CW で走らせ、呼び手の x87 環境 (CW・SW の例外フラグと条件・TAG) に戻す。
/// アプリは CPL3 の前景で自分の FPU 文脈を持つ。ST の中身は cdecl で呼び手が保存しない
/// 約束 (呼出し時は空) なので、保存/復元するのは FNSTENV の 28 B (T7b-b5⑤: 以前は CW
/// だけを戻して fnclex で呼び手の例外フラグを消していた — C の慣例 (C11 7.6) に反する)。
#[inline(never)]
pub fn fp_scope<R>(f: impl FnOnce() -> R) -> R {
    let mut saved = [0u32; 7];
    unsafe {
        /* fnstenv は保存の後に全例外をマスクする。直後に既定の CW を入れる。 */
        core::arch::asm!("fnstenv [{0}]", in(reg) saved.as_mut_ptr(),
                         options(nostack, preserves_flags));
        core::arch::asm!("fldcw word ptr [{0}]", in(reg) &X87_CW_STD as *const u16,
                         options(nostack, preserves_flags));
    }
    let r = f();
    unsafe {
        /* f の例外フラグを捨て、呼び手の CW・SW・TAG に戻す。 */
        core::arch::asm!("fnclex", options(nomem, nostack, preserves_flags));
        core::arch::asm!("fldenv [{0}]", in(reg) saved.as_ptr(),
                         options(nostack, preserves_flags));
    }
    r
}

/* ================================================================ */
/*  状態 (static はポインタと数だけ)                                  */
/* ================================================================ */

/// `os32gui_font_stats` が写す形 (repr(C)、u32 の並び)。版を足すときは末尾に。
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct AppFontStats {
    /// engine の数 (hit / miss / raster / validate / stale / kcg_only / busy …)。
    pub engine: Stats,
    /// 0 = Closed、1 = Ready、2 = KcgOnly (理由は `reason`)、0xFFFF_FFFF = engine 無し。
    pub state: u32,
    /// KcgOnly の理由 (`os32_font::engine::Reason` の並び)。
    pub reason: u32,
    pub set_slot: u32,
    pub has_arena: u32,
    /// 最後に開いたときの `font/gen` と open の戻り値 (0 = 成功)。
    pub open_gen: i32,
    pub open_rc: i32,
    pub opens: u32,
    pub retries: u32,
    pub reloads: u32,
    pub gen_checks: u32,
    pub gen_changes: u32,
    pub trims: u32,
    pub arena_allocs: u32,
    pub arena_frees: u32,
    pub alloc_fails: u32,
    /// TASK_FONTPERF F3 (G-3: 末尾にだけ足す、u32 は計 37): 束の ok / rejected / hit / miss / err。
    pub bundle: BundleStats,
}
// fontbench (gdi_test) と libos32gui_stub は 37 語 (NSTAT) で読む。
const _: () = assert!(size_of::<AppFontStats>() == 37 * 4);

struct FontState {
    engine: *mut AppEngine,
    /// Engine 本体の確保に失敗した (次の前景化 / EV_FONT まで試さない)。
    engine_blocked: bool,
    /// arena の確保に失敗した (次の trim / 前景化 / EV_FONT まで試さない)。
    arena_blocked: bool,
    /// 次の open は前景化による再試行 (シリアルに `font: retry rc=N`)。
    retry: bool,
    open_gen: i32,
    open_rc: i32,
    opens: u32,
    retries: u32,
    reloads: u32,
    gen_checks: u32,
    gen_changes: u32,
    trims: u32,
    arena_allocs: u32,
    arena_frees: u32,
    alloc_fails: u32,
}

impl FontState {
    const NEW: Self = Self {
        engine: ptr::null_mut(),
        engine_blocked: false,
        arena_blocked: false,
        retry: false,
        open_gen: 0,
        open_rc: 0,
        opens: 0,
        retries: 0,
        reloads: 0,
        gen_checks: 0,
        gen_changes: 0,
        trims: 0,
        arena_allocs: 0,
        arena_frees: 0,
        alloc_fails: 0,
    };
}

struct Cell(UnsafeCell<FontState>);
unsafe impl Sync for Cell {}
static FONT: Cell = Cell(UnsafeCell::new(FontState::NEW));

#[inline]
fn st() -> &'static mut FontState {
    unsafe { &mut *FONT.0.get() }
}

/// 確保済みの Engine (無ければ None、確保しない)。
#[inline]
fn current() -> Option<&'static mut AppEngine> {
    unsafe { st().engine.as_mut() }
}

/* ================================================================ */
/*  確保                                                             */
/* ================================================================ */

/// Engine 本体を 1 回だけ確保して 0 埋めする (= `Engine::new()`)。
fn engine() -> Option<&'static mut AppEngine> {
    let f = st();
    if f.engine.is_null() {
        if f.engine_blocked {
            return None;
        }
        let size = size_of::<AppEngine>();
        let p = unsafe { (os32api::api().mem_alloc)(size as u32) };
        if p.is_null() || (p as usize) % align_of::<AppEngine>() != 0 {
            if !p.is_null() {
                unsafe { (os32api::api().mem_free)(p) };
            }
            f.engine_blocked = true;
            f.alloc_fails = f.alloc_fails.wrapping_add(1);
            return None;
        }
        /* mem_alloc は 0 埋めを約束しない。0 埋め == Engine::new() (host で assert)。 */
        unsafe { ptr::write_bytes(p, 0, size) };
        f.engine = p as *mut AppEngine;
    }
    current()
}

/// arena を 1 回確保して cache に渡す。取れなければ cache 無しで続ける。
fn attach_arena(e: &mut AppEngine) {
    let f = st();
    let p = unsafe { (os32api::api().mem_alloc)(ARENA_BYTES_APP as u32) };
    if p.is_null() {
        f.arena_blocked = true;
        f.alloc_fails = f.alloc_fails.wrapping_add(1);
        return;
    }
    let arena = unsafe { core::slice::from_raw_parts_mut(p, ARENA_BYTES_APP) };
    let (n_cell, n16) = layout(ARENA_BYTES_APP);
    match e.attach_arena(arena, n_cell, n16, N32) {
        Ok(()) => f.arena_allocs = f.arena_allocs.wrapping_add(1),
        Err(back) => unsafe { (os32api::api().mem_free)(back.as_mut_ptr()) },
    }
}

/// NOSYS / NOSPC / その他の open 失敗を KcgOnly の理由へ。
fn reason_of(rc: i32) -> Reason {
    match rc {
        OS32_ERR_NOSYS => Reason::NoFpu,
        OS32_ERR_NOSPC => Reason::Nospc,
        _ => Reason::OpenFailed,
    }
}

/// 2 本の lease を開いて Engine::open (fp_scope の中)。0 / 負。
fn open_faces(e: &mut AppEngine) -> i32 {
    let latin = match AppSource::open(FONT_LATIN_C) {
        Ok(s) => s,
        Err(rc) => {
            let _ = e.set_kcg_only(reason_of(rc));
            return rc;
        }
    };
    let ja = match AppSource::open(FONT_JA_C) {
        Ok(s) => s,
        Err(rc) => {
            drop(latin);
            let _ = e.set_kcg_only(reason_of(rc));
            return rc;
        }
    };
    /* 失敗は Engine が両 face を閉じ (Source の Drop で close) KcgOnly(OpenFailed)。 */
    if fp_scope(|| e.open(latin, ja)).is_err() {
        return OS32_ERR_IO;
    }
    /* F3: 束 (開けない・合わないときは TTF 経路のまま) と先読み (整数だけ、x87 不要)。 */
    if let Ok(g16) = AppSource::open(FONT_JA_G16_C) {
        if e.open_bundle(g16).is_ok() && APP_PRELOAD_MAX > 0 {
            let _ = e.preload_from_bundle(APP_PRELOAD_MAX);
        }
    }
    0
}

/// Closed の Engine を設定どおりに開く (font_set と font/gen を読み直す)。
fn open_set(e: &mut AppEngine) {
    let f = st();
    let retry = f.retry;
    f.retry = false;
    let (set, gen) = crate::cfgro::font_settings(FONT_SET_STD as i32, FONT_GEN_DEFAULT)
        .unwrap_or((FONT_SET_STD as i32, FONT_GEN_DEFAULT));
    f.open_gen = gen;
    let rc = if set == FONT_SET_KCG_ONLY as i32 {
        let _ = e.set_kcg_only(Reason::Config);
        0
    } else {
        open_faces(e)
    };
    f.open_rc = rc;
    f.opens = f.opens.wrapping_add(1);
    if retry {
        crate::client::dbg_print_num(b"font: retry rc=", rc);
    } else if rc < 0 {
        crate::client::dbg_print_num(b"font: open rc=", rc);
    }
}

/* ================================================================ */
/*  入口                                                             */
/* ================================================================ */

/// text() の需要で呼ぶ: Engine (1 回)・arena (無ければ)・face (Closed なら) を揃える。
/// 借用中は何も変えない。Engine が取れなければ None (呼び手は全字 KCG)。
pub fn ensure_open() -> Option<&'static mut AppEngine> {
    let e = engine()?;
    if e.is_busy() {
        return Some(e);
    }
    if !e.cache().has_arena() && !st().arena_blocked {
        attach_arena(e);
    }
    if e.state() == State::Closed {
        open_set(e);
    }
    Some(e)
}

/// GUI_EV_FONT と世代の食い違い: face と lease を閉じ全 entry を失効、Closed へ
/// (arena と Engine 本体は保つ)。次の text() が設定を読み直して開く。
/// TRIM_DONE は送らない (呼び手 app.rs)。借用中なら何もしない。
pub fn reload() {
    let f = st();
    f.reloads = f.reloads.wrapping_add(1);
    if let Some(e) = current() {
        if e.close().is_err() {
            return;
        }
    }
    f.engine_blocked = false;
    f.arena_blocked = false;
    f.retry = false;
}

/// `font/gen` を 1 回照合し、違えば [`reload`]。開いていなければ読まない
/// (次の open が読む)。true = reload した (呼び手が全窓を invalidate)。
pub fn check_gen() -> bool {
    let e = match current() {
        Some(e) => e,
        None => return false,
    };
    if e.state() == State::Closed {
        return false;
    }
    let f = st();
    f.gen_checks = f.gen_checks.wrapping_add(1);
    match crate::cfgro::font_settings(FONT_SET_STD as i32, FONT_GEN_DEFAULT) {
        Some((_, gen)) if gen != f.open_gen => {
            f.gen_changes = f.gen_changes.wrapping_add(1);
            reload();
            true
        }
        _ => false,
    }
}

/// NOSPC / open 失敗の KcgOnly を Closed に戻し、次の open を 1 回の再試行にする。
/// true = 戻した。
pub fn retry_if_kcg_only() -> bool {
    let f = st();
    f.engine_blocked = false;
    f.arena_blocked = false;
    let e = match current() {
        Some(e) => e,
        None => return false,
    };
    if !matches!(e.state(), State::KcgOnly(Reason::Nospc) | State::KcgOnly(Reason::OpenFailed)) {
        return false;
    }
    if e.close().is_err() {
        return false;
    }
    f.retry = true;
    f.retries = f.retries.wrapping_add(1);
    true
}

/// 前景化 (EV_FOCUS in): 世代の照合、同じなら再試行。true = 全窓を描き直す。
pub fn on_focus_in() -> bool {
    if check_gen() {
        return true;
    }
    retry_if_kcg_only()
}

/// EV_TRIM の安全点 (app.rs、TRIM_DONE の前): 全 entry 失効・face の read cache を
/// 捨て・arena を mem_free。face / lease / Engine 本体は保つ。借用中は何もしない。
/// true = arena を返した。
pub fn trim() -> bool {
    let f = st();
    f.trims = f.trims.wrapping_add(1);
    f.arena_blocked = false;
    let e = match current() {
        Some(e) => e,
        None => return false,
    };
    if e.trim().is_err() {
        return false;
    }
    match e.detach_arena() {
        Ok(Some(arena)) => {
            unsafe { (os32api::api().mem_free)(arena.as_mut_ptr()) };
            f.arena_frees = f.arena_frees.wrapping_add(1);
            true
        }
        _ => false,
    }
}

/// 観測用の写し (`os32gui_font_stats`)。
pub fn stats() -> AppFontStats {
    let f = st();
    let mut out = AppFontStats {
        open_gen: f.open_gen,
        open_rc: f.open_rc,
        opens: f.opens,
        retries: f.retries,
        reloads: f.reloads,
        gen_checks: f.gen_checks,
        gen_changes: f.gen_changes,
        trims: f.trims,
        arena_allocs: f.arena_allocs,
        arena_frees: f.arena_frees,
        alloc_fails: f.alloc_fails,
        state: u32::MAX,
        ..AppFontStats::default()
    };
    if let Some(e) = current() {
        out.engine = *e.stats();
        out.bundle = e.bundle_stats();
        let (state, reason) = match e.state() {
            State::Closed => (0, 0),
            State::Ready => (1, 0),
            State::KcgOnly(r) => (2, r as u32),
        };
        out.state = state;
        out.reason = reason;
        out.set_slot = u32::from(e.set_slot());
        out.has_arena = e.cache().has_arena() as u32;
    }
    out
}
