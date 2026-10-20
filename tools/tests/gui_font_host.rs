// T7b-b3 S4: the production libos32gui font.rs (Engine on the exec heap, arena
// trim/reallocate, lease open/close, font/gen check, focus retry, fp_scope) over
// the real os32-font crate, real Inconsolata / JF Dot Shinonome 16 files and
// mocked KAPI mem_alloc/mem_free/font_source_*. Appended after the mock modules
// `cfgro` / `client` and `#[path] mod font` by tools/tests/test_gui_trim.py.
use std::collections::HashMap;
use std::fs::File;
use std::os::unix::fs::FileExt;

use font::{AppSource, AppFontStats};
use os32_font::engine::{Cell, Engine, Glyph, Reason, Resolved, State};
use os32_font::limits::{layout, ARENA_BYTES_APP, N32};
use os32_font::raster::PixelSize;

fn check(condition: bool, label: &str) {
    if !condition {
        use std::io::Write;
        let mut out = std::io::stdout();
        let _ = out.write_all(label.as_bytes());
        let _ = out.flush();
        std::process::exit(101);
    }
}

// ---- mocked KAPI -----------------------------------------------------------

struct Kapi {
    blocks: HashMap<usize, usize>,
    allocs: Vec<usize>,
    frees: Vec<usize>,
    files: HashMap<u32, File>,
    next_id: u32,
    opened: Vec<String>,
    closed: Vec<u32>,
    force_latin: Option<i32>,
    force_ja: Option<i32>,
    /// TASK_FONTPERF F3: the .g16 bundle file (None: its open answers NOTFOUND).
    g16: Option<std::path::PathBuf>,
}
static mut K: Option<Kapi> = None;
fn k() -> &'static mut Kapi {
    unsafe { K.as_mut().unwrap() }
}

const PAGE: usize = 4096;
unsafe extern "C" fn m_alloc(size: u32) -> *mut u8 {
    let n = size as usize;
    let p = std::alloc::alloc(std::alloc::Layout::from_size_align(n, PAGE).unwrap());
    // mem_alloc does not promise zeroed memory: hand out garbage.
    p.write_bytes(0xA5, n);
    k().blocks.insert(p as usize, n);
    k().allocs.push(n);
    p
}
unsafe extern "C" fn m_free(p: *mut u8) {
    let n = k().blocks.remove(&(p as usize)).expect("free of an unknown block");
    k().frees.push(n);
    std::alloc::dealloc(p, std::alloc::Layout::from_size_align(n, PAGE).unwrap());
}
fn font_root() -> std::path::PathBuf {
    std::path::PathBuf::from(env!("OS32_ROOT")).join("assets/fonts")
}
unsafe extern "C" fn m_open(path: *const u8, id: *mut u32, size: *mut u32) -> i32 {
    let p = std::ffi::CStr::from_ptr(path as *const std::ffi::c_char).to_str().unwrap().to_string();
    if p == os32_font::paths::FONT_JA_G16 {
        let file = match k().g16.clone() {
            Some(f) => f,
            None => return os32api::gui::proto::OS32_ERR_NOTFOUND,
        };
        let f = File::open(file).unwrap();
        *size = f.metadata().unwrap().len() as u32;
        k().next_id += 1;
        *id = k().next_id;
        k().files.insert(*id, f);
        k().opened.push(p);
        return 0;
    }
    let latin = p == os32_font::paths::FONT_LATIN;
    assert!(latin || p == os32_font::paths::FONT_JA, "{p}");
    let forced = if latin { k().force_latin } else { k().force_ja };
    if let Some(rc) = forced {
        return rc;
    }
    let file = if latin {
        font_root().join("Inconsolata-Regular.ttf")
    } else {
        std::path::PathBuf::from(std::env::var("OS32_FONT_SHINONOME").unwrap())
    };
    let f = File::open(file).unwrap();
    *size = f.metadata().unwrap().len() as u32;
    k().next_id += 1;
    *id = k().next_id;
    k().files.insert(*id, f);
    k().opened.push(p);
    0
}
unsafe extern "C" fn m_read(id: u32, off: u32, dst: *mut u8, len: u32) -> i32 {
    let f = match k().files.get(&id) {
        Some(f) => f,
        None => return -11, // STALE
    };
    let buf = std::slice::from_raw_parts_mut(dst, len as usize);
    f.read_at(buf, u64::from(off)).map(|n| n as i32).unwrap_or(-1)
}
unsafe extern "C" fn m_close(id: u32) -> i32 {
    assert!(k().files.remove(&id).is_some(), "double close {id}");
    k().closed.push(id);
    0
}

fn install() {
    unsafe {
        K = Some(Kapi {
            blocks: HashMap::new(),
            allocs: Vec::new(),
            frees: Vec::new(),
            files: HashMap::new(),
            next_id: 0,
            opened: Vec::new(),
            closed: Vec::new(),
            force_latin: None,
            force_ja: None,
            g16: None,
        });
        let api: &'static mut os32api::KernelAPI = Box::leak(Box::new(os32api::mock_api()));
        api.mem_alloc = m_alloc;
        api.mem_free = m_free;
        api.font_source_open = m_open;
        api.font_source_read_at = m_read;
        api.font_source_close = m_close;
        os32api::os32_init(api);
    }
}

fn st() -> AppFontStats {
    font::stats()
}
fn open_leases() -> usize {
    k().files.len()
}

// ---- tests -----------------------------------------------------------------

static NEW: Engine<AppSource> = Engine::new();

#[test]
fn zero_fill_equals_new() {
    // P2-10: the app's zero-filled exec_heap block must be Engine::new().
    let size = std::mem::size_of::<Engine<AppSource>>();
    let bytes = unsafe { std::slice::from_raw_parts(&NEW as *const _ as *const u8, size) };
    check(bytes.iter().all(|&b| b == 0), "Engine<AppSource>::new() is not all zero");
    println!("Engine<AppSource> {size} B, align {}", std::mem::align_of::<Engine<AppSource>>());
}

fn cw() -> u16 {
    let mut v: u16 = 0;
    unsafe { core::arch::asm!("fnstcw word ptr [{0}]", in(reg) &mut v as *mut u16) };
    v
}

#[test]
fn fp_scope_restores_caller_cw() {
    let other: u16 = 0x0F7F;
    unsafe { core::arch::asm!("fldcw word ptr [{0}]", in(reg) &other as *const u16) };
    let inside = font::fp_scope(cw);
    let after = cw();
    let std_cw: u16 = 0x037F;
    unsafe { core::arch::asm!("fldcw word ptr [{0}]", in(reg) &std_cw as *const u16) };
    check(inside == 0x037F, "fp-scope-default-cw");
    check(after == other, "fp-scope-restore");
}

fn sw() -> u16 {
    let mut v: u16 = 0;
    unsafe { core::arch::asm!("fnstsw word ptr [{0}]", in(reg) &mut v as *mut u16) };
    v
}

#[test]
fn fp_scope_status_flags_kept() {
    // T7b-b5⑤: the caller's sticky exception flags (here ZE from 1/0, masked) survive,
    // and flags raised inside the scope do not leak out (C11 7.6 convention).
    let std_cw: u16 = 0x037F;
    unsafe {
        core::arch::asm!("fninit", "fldcw word ptr [{0}]", "fld1", "fldz", "fdivp st(1), st", "fstp st(0)",
                         in(reg) &std_cw as *const u16);
    }
    let before = sw();
    font::fp_scope(|| unsafe {
        // raise IE inside (0/0, masked)
        core::arch::asm!("fldz", "fldz", "fdivp st(1), st", "fstp st(0)");
    });
    let after = sw();
    unsafe { core::arch::asm!("fninit", "fldcw word ptr [{0}]", in(reg) &std_cw as *const u16) };
    check(before & 0x3F == 0x04, "fp-scope-fixture-ze");
    check(after & 0x3F == before & 0x3F, "fp-scope-status-flags");
    check(after & 0x3800 == before & 0x3800, "fp-scope-top");
}

#[test]
fn app_font_lifecycle() {
    install();
    let engine_size = std::mem::size_of::<Engine<AppSource>>();
    cfgro::set(Some((0, 5)));

    // (1) First demand: Engine once (zero-filled garbage), arena once, both leases.
    let e = font::ensure_open().expect("engine");
    check(e.state() == State::Ready, "zero-fill: a garbage Engine did not open");
    assert_eq!(k().allocs, vec![engine_size, ARENA_BYTES_APP]);
    assert_eq!(k().opened, vec![os32_font::paths::FONT_LATIN.to_string(), os32_font::paths::FONT_JA.to_string()]);
    let s = st();
    assert_eq!((s.state, s.opens, s.open_gen, s.open_rc, s.has_arena, s.arena_allocs), (1, 1, 5, 0, 1, 1));
    assert_eq!(cfgro::reads(), 1);
    // F2: the 128KiB app arena is cells + Big16 (layout()).
    let (n_cell, n16) = layout(ARENA_BYTES_APP);
    assert_eq!(e.cache().layout(), (n_cell, n16, N32));
    assert_eq!(n_cell, 3_200);

    // (2) Later demands allocate and open nothing.
    let _ = font::ensure_open().unwrap();
    check(k().allocs.len() == 2, "arena-once");
    assert_eq!((k().opened.len(), cfgro::reads()), (2, 1));

    // (3) b3⑤ / P2-8: while a bitmap is borrowed, trim and EV_FONT change nothing.
    let e = font::ensure_open().unwrap();
    assert!(e.begin());
    let mut g = Glyph::default();
    let r = font::fp_scope(|| e.resolve(u32::from(b'A'), Cell::Half, PixelSize::Px16, &mut g));
    assert_eq!(r, Resolved::Bitmap);
    assert!(e.is_busy());
    check(!font::trim(), "trim-while-busy: arena returned during a borrow");
    check(k().frees.is_empty() && e.cache().has_arena() && e.is_busy(), "trim-while-busy");
    font::reload();
    check(e.state() == State::Ready && open_leases() == 2, "reload-while-busy");
    e.release();
    e.end();

    // (4) b3④: trim frees the arena only; faces, leases and the Engine stay.
    let before = e.cache().n_valid();
    assert!(before > 0);
    check(font::trim(), "trim-frees-arena");
    check(k().frees == vec![ARENA_BYTES_APP], "trim-frees-arena");
    assert!(!e.cache().has_arena() && e.state() == State::Ready && open_leases() == 2);
    assert!(k().closed.is_empty());
    // (5) The next demand reallocates the arena, opens nothing.
    let e = font::ensure_open().unwrap();
    assert!(e.cache().has_arena());
    assert_eq!((k().allocs.len(), k().opened.len()), (3, 2));
    assert!(e.begin());
    let r = font::fp_scope(|| e.resolve(u32::from(b'A'), Cell::Half, PixelSize::Px16, &mut g));
    assert_eq!(r, Resolved::Bitmap);
    e.release();
    e.end();

    // (6) font/gen: same -> nothing; changed (EV_FONT lost) -> reload, reopen on demand.
    check(!font::check_gen(), "gen-same-reloads");
    cfgro::set(Some((0, 6)));
    check(font::check_gen(), "gen-reload");
    check(e.state() == State::Closed && open_leases() == 0 && k().closed.len() == 2, "gen-reload");
    assert!(e.cache().has_arena(), "reload keeps the arena");
    let e = font::ensure_open().unwrap();
    assert_eq!((e.state(), st().open_gen, k().allocs.len()), (State::Ready, 6, 3));
    cfgro::set(None);
    check(!font::check_gen(), "unreadable settings must not reload");

    // (7) GUI_EV_FONT: close + invalidate, Closed until the next demand.
    let slot = e.set_slot();
    font::reload();
    assert_eq!((e.state(), open_leases(), e.cache().n_valid()), (State::Closed, 0, 0));
    check(!font::check_gen(), "closed engine reads no setting");
    cfgro::set(Some((0, 6)));
    let e = font::ensure_open().unwrap();
    assert_eq!((e.state(), open_leases()), (State::Ready, 2));
    assert!(e.set_slot() != slot);

    // (8) NOSPC -> KcgOnly(Nospc), no lease kept; focus retries once, then TTF.
    font::reload();
    k().force_ja = Some(os32api::gui::proto::OS32_ERR_NOSPC);
    let e = font::ensure_open().unwrap();
    assert_eq!((e.state(), open_leases(), st().open_rc), (State::KcgOnly(Reason::Nospc), 0, -4));
    let _ = font::ensure_open().unwrap();
    assert_eq!(st().opens, 4, "KcgOnly does not retry per text()");
    check(font::on_focus_in(), "retry");
    assert_eq!((e.state(), st().retries), (State::Closed, 1));
    k().force_ja = None;
    let e = font::ensure_open().unwrap();
    check(e.state() == State::Ready, "retry");
    check(client::log().iter().any(|l| l == "font: retry rc= 0"), "retry-log");
    check(!font::on_focus_in(), "focus on a ready engine changes nothing");

    // (9) Other open failures; NOSYS (no FPU) is permanent.
    font::reload();
    k().force_latin = Some(os32api::gui::proto::OS32_ERR_NOTFOUND);
    let e = font::ensure_open().unwrap();
    assert_eq!((e.state(), open_leases()), (State::KcgOnly(Reason::OpenFailed), 0));
    font::reload();
    k().force_latin = Some(os32api::gui::proto::OS32_ERR_NOSYS);
    let e = font::ensure_open().unwrap();
    assert_eq!(e.state(), State::KcgOnly(Reason::NoFpu));
    check(!font::on_focus_in(), "no-fpu-retried");
    k().force_latin = None;

    // (10) font_set = KCG only: no lease at all.
    cfgro::set(Some((1, 7)));
    font::reload();
    let n = k().opened.len();
    let e = font::ensure_open().unwrap();
    assert_eq!((e.state(), k().opened.len(), open_leases()), (State::KcgOnly(Reason::Config), n, 0));
    let s = st();
    println!("app font: opens={} retries={} reloads={} gen_checks={} gen_changes={} trims={} arena {}/{}",
             s.opens, s.retries, s.reloads, s.gen_checks, s.gen_changes, s.trims, s.arena_allocs, s.arena_frees);
    assert_eq!(k().allocs.iter().filter(|&&n| n == engine_size).count(), 1, "Engine allocated once");
    bundle_phase();
}

fn bundle_phase() {
    // TASK_FONTPERF F3 (continues app_font_lifecycle: the library's state is one
    // static per process): the third lease (.g16), the preload at open
    // (APP_PRELOAD_MAX), and F-8: every reload drops the bundle's lease with the
    // faces (20 settings changes keep the lease count at 3, every close matched).
    font::reload();
    k().g16 = Some(std::path::PathBuf::from(std::env::var("OS32_FONT_G16").unwrap()));
    cfgro::set(Some((0, 100)));
    let before = st().engine;
    let e = font::ensure_open().expect("engine");
    assert_eq!(e.state(), State::Ready);
    check(open_leases() == 3, "bundle-lease");
    let s = st();
    check(s.bundle.ok == 1 && s.bundle.rejected == 0, "bundle-ok");
    let (n_all, n_pre) = e.bundle_counts();
    let want = (n_pre as usize).min(os32_font::limits::APP_PRELOAD_MAX);
    println!("app bundle: n_all={n_all} n_pre={n_pre} preloaded={}", e.cache().n_valid());
    check(want > 0 && e.cache().n_valid() == want, "app-preload-count");
    let commits = (s.engine.commit_bitmap + s.engine.commit_fallback)
        - (before.commit_bitmap + before.commit_fallback);
    check(commits as usize == want, "app-preload-count");
    let closed = k().closed.len();
    for round in 1..=20 {
        cfgro::set(Some((0, 100 + round)));
        check(font::check_gen(), "gen-reload");
        check(open_leases() == 0, "bundle-lease-kept-after-reload");
        let e = font::ensure_open().unwrap();
        check(e.state() == State::Ready && open_leases() == 3, "bundle-lease-count");
    }
    check(k().closed.len() - closed == 60, "bundle-lease-close-count");
    // trim keeps the leases (faces and bundle).
    check(font::trim(), "trim-frees-arena");
    check(open_leases() == 3, "trim-keeps-bundle");
    font::reload();
    check(open_leases() == 0, "bundle-lease-kept-after-reload");
}
