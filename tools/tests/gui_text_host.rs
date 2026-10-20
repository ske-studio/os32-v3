// T7b-b4 S5: the production libos32gui text() (draw.rs) over the production
// font.rs / gstate.rs / clip.rs / surface.rs, the real os32-font crate and the
// real Inconsolata / JF Dot Shinonome 16, with libos32gfx's pixel entry points
// and the KAPI (mem_alloc, font_source_*, kcg_read_*, unicode_to_jis,
// gfx_screen_info) mocked on the host. Composed by tools/tests/test_gui_text.py:
// the production modules are inserted at the `crate` marker below.
//
// The expected images are built independently: glyphs straight from the font
// crate's Face + RasterScratch (no cache, no engine), placed pixel by pixel at
// (left, ascent - top) and cut to the cell; ROM glyphs from the mock ROM.
//
// A failed check prints `FAIL <label>` and the run goes on; the last test exits
// 101 when anything failed (the mutation runner looks for the label).
#![allow(dead_code, unused_imports, unused_unsafe, static_mut_refs, non_upper_case_globals)]

mod ffi {
    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct GfxRect {
        pub x: i32,
        pub y: i32,
        pub w: i32,
        pub h: i32,
    }
    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct GfxFramebuffer {
        pub width: i32,
        pub height: i32,
        pub pitch: i32,
        pub planes: [*mut u8; 4],
    }
    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct GfxSurface {
        pub w: i32,
        pub h: i32,
        pub pitch: i32,
        pub planes: [*mut u8; 4],
        pub _pool_idx: i32,
    }
    pub static mut gfx_fb: GfxFramebuffer =
        GfxFramebuffer { width: 0, height: 0, pitch: 0, planes: [core::ptr::null_mut(); 4] };
    pub static mut gfx_ready: i32 = 1;

    pub const W: i32 = 640;
    pub const H: i32 = 400;
    /// The full-screen back buffer (PLANAR4 writes go through gfx_pixel, PACKED8 directly).
    pub static mut FB: [u8; (W * H) as usize] = [0; (W * H) as usize];
    /// Pixel writes outside the screen (libos32gfx clips them; text() must not rely on it).
    pub static mut OOB: u32 = 0;

    pub unsafe fn gfx_pixel(x: i32, y: i32, c: u8) {
        if x < 0 || y < 0 || x >= W || y >= H {
            OOB += 1;
            return;
        }
        FB[(y * W + x) as usize] = c;
    }
    pub unsafe fn gfx_get_pixel(x: i32, y: i32) -> u8 {
        if x < 0 || y < 0 || x >= W || y >= H {
            return 0;
        }
        FB[(y * W + x) as usize]
    }
    pub unsafe fn gfx_fill_rect(x: i32, y: i32, w: i32, h: i32, c: u8) {
        for yy in y..y + h {
            for xx in x..x + w {
                gfx_pixel(xx, yy, c);
            }
        }
    }
    pub unsafe fn gfx_create_surface(w: i32, h: i32) -> *mut GfxSurface {
        let buf: &'static mut [u8] = Box::leak(vec![0u8; (w * h) as usize].into_boxed_slice());
        Box::into_raw(Box::new(GfxSurface {
            w,
            h,
            pitch: w,
            planes: [buf.as_mut_ptr(), core::ptr::null_mut(), core::ptr::null_mut(), core::ptr::null_mut()],
            _pool_idx: 0,
        }))
    }
    pub unsafe fn gfx_free_surface(_s: *mut GfxSurface) {}
    pub unsafe fn gfx_surface_pixel(s: *mut GfxSurface, x: i32, y: i32, c: u8) {
        let s = &*s;
        if x < 0 || y < 0 || x >= s.w || y >= s.h {
            OOB += 1;
            return;
        }
        *s.planes[0].offset((y * s.pitch + x) as isize) = c;
    }
    pub unsafe fn gfx_surface_fill_rect(s: *mut GfxSurface, x: i32, y: i32, w: i32, h: i32, c: u8) {
        for yy in y..y + h {
            for xx in x..x + w {
                gfx_surface_pixel(s, xx, yy, c);
            }
        }
    }
    pub unsafe fn gfx_blit_colorkey(_dx: i32, _dy: i32, _s: *const GfxSurface, _r: *const GfxRect, _k: u8) {
        panic!("blit is not part of this test");
    }
    /// lib/utf8.c unicode_to_ank (same rules).
    pub unsafe fn unicode_to_ank(cp: u32) -> u8 {
        match cp {
            0x20..=0x7E => cp as u8,
            0xFF01..=0xFF5E => (cp - 0xFF01 + 0x21) as u8,
            0xFF61..=0xFF9F => (cp - 0xFF61 + 0xA1) as u8,
            0xA5 => 0x5C,
            _ => 0,
        }
    }
}

mod cfgro {
    static mut CFG: Option<(i32, i32)> = Some((0, 1));
    pub fn set(v: Option<(i32, i32)>) {
        unsafe { CFG = v }
    }
    pub fn font_settings(_: i32, _: i32) -> Option<(i32, i32)> {
        unsafe { CFG }
    }
}
mod client {
    pub fn dbg_print_num(m: &[u8], v: i32) {
        println!("{} {}", String::from_utf8_lossy(m), v);
    }
}

// ---- crate ----

use std::collections::HashMap;
use std::fs::File;
use std::os::unix::fs::FileExt;

use os32_font::engine::{Glyph, State};
use os32_font::limits::{ASCENT_JA_PX, ASCENT_LATIN_PX, CELL_H_PX};
use os32_font::raster::{PixelSize, RasterScratch};
use os32_font::reader::{Face, Result as FontResult, Source};
use os32api::gui::proto::GUI_STYLE_TRANSPARENT_BG;
use os32api::gui::types::{Rect, ScreenInfo, Style, SurfaceId, GFX_FMT_PACKED8, GFX_FMT_PLANAR4};

static mut FAILED: bool = false;
fn check(c: bool, label: &str) {
    if !c {
        println!("FAIL {label}");
        unsafe { FAILED = true };
    }
}

// ---- mocked KAPI -------------------------------------------------------------

const PAGE: usize = 4096;
static mut FAIL_ALLOC: bool = false;
static mut BLOCKS: Option<HashMap<usize, usize>> = None;
static mut FILES: Option<HashMap<u32, File>> = None;
static mut NEXT_ID: u32 = 0;
static mut PACKED8: bool = false;
/// ROM reads: ('a', ank) / ('k', jis).
static mut ROM_READS: Vec<(char, u16)> = Vec::new();

unsafe extern "C" fn m_alloc(size: u32) -> *mut u8 {
    if FAIL_ALLOC {
        return core::ptr::null_mut();
    }
    let n = size as usize;
    let p = std::alloc::alloc(std::alloc::Layout::from_size_align(n, PAGE).unwrap());
    p.write_bytes(0xA5, n); // mem_alloc does not promise zeroed memory
    BLOCKS.get_or_insert_with(HashMap::new).insert(p as usize, n);
    p
}
unsafe extern "C" fn m_free(p: *mut u8) {
    let n = BLOCKS.as_mut().unwrap().remove(&(p as usize)).expect("free of an unknown block");
    std::alloc::dealloc(p, std::alloc::Layout::from_size_align(n, PAGE).unwrap());
}
fn latin_path() -> std::path::PathBuf {
    std::path::PathBuf::from(env!("OS32_ROOT")).join("assets/fonts/Inconsolata-Regular.ttf")
}
fn ja_path() -> std::path::PathBuf {
    std::path::PathBuf::from(std::env::var("OS32_FONT_SHINONOME").unwrap())
}
unsafe extern "C" fn m_open(path: *const u8, id: *mut u32, size: *mut u32) -> i32 {
    let p = std::ffi::CStr::from_ptr(path as *const std::ffi::c_char).to_str().unwrap();
    let file = if p == os32_font::paths::FONT_LATIN {
        latin_path()
    } else {
        assert_eq!(p, os32_font::paths::FONT_JA);
        ja_path()
    };
    let f = File::open(file).unwrap();
    *size = f.metadata().unwrap().len() as u32;
    NEXT_ID += 1;
    *id = NEXT_ID;
    FILES.get_or_insert_with(HashMap::new).insert(NEXT_ID, f);
    0
}
unsafe extern "C" fn m_read(id: u32, off: u32, dst: *mut u8, len: u32) -> i32 {
    let f = match FILES.as_ref().unwrap().get(&id) {
        Some(f) => f,
        None => return -11,
    };
    let buf = std::slice::from_raw_parts_mut(dst, len as usize);
    f.read_at(buf, u64::from(off)).map(|n| n as i32).unwrap_or(-1)
}
unsafe extern "C" fn m_close(id: u32) -> i32 {
    assert!(FILES.as_mut().unwrap().remove(&id).is_some(), "double close {id}");
    0
}

/// Mock ROM ANK 8x16 (cell rows from the top). Ink in rows 2..=13 with the
/// cell's left and right columns set (so a shifted or widened cell shows), the
/// PC-98 ROM baseline under row 13 (14 行目); 'g' has a descender to row 15.
fn rom_ank(ch: u8) -> [u8; 16] {
    let mut p = [0u8; 16];
    for r in 2..=13 {
        p[r] = ch | 0x81;
    }
    if ch == b'g' {
        p[14] = 0x42;
        p[15] = 0x3C;
    }
    p
}
/// Mock ROM kanji 16x16: ink rows ROM_KANJI_TOP..=ROM_KANJI_BOTTOM — the 16-dot
/// JIS box (a blank row above and below), the box JF Dot Shinonome 16 also uses
/// (its 亜 inks rows 1..=14 at ascent 14). The real ROM's rows are seen on the
/// guest (S7 b4①); here the check is that KCG is drawn from the cell top and TTF
/// from `ascent - top`, so the two boxes line up.
const ROM_KANJI_TOP: usize = 1;
const ROM_KANJI_BOTTOM: usize = 14;
fn rom_kanji(jis: u16) -> [u8; 32] {
    let mut p = [0u8; 32];
    let v = jis | 0x8001;
    for r in ROM_KANJI_TOP..=ROM_KANJI_BOTTOM {
        p[2 * r] = (v >> 8) as u8;
        p[2 * r + 1] = v as u8;
    }
    p
}
unsafe extern "C" fn m_read_ank(ch: u8, buf: *mut u8) {
    ROM_READS.push(('a', u16::from(ch)));
    std::ptr::copy_nonoverlapping(rom_ank(ch).as_ptr(), buf, 16);
}
unsafe extern "C" fn m_read_kanji(jis: u16, buf: *mut u8) {
    ROM_READS.push(('k', jis));
    std::ptr::copy_nonoverlapping(rom_kanji(jis).as_ptr(), buf, 32);
}
fn jis_of(cp: u32) -> u16 {
    match cp {
        0x4E9C => 0x3021, // 亜
        0x1F600 => 0,     // not in JIS
        _ => ((cp & 0x5E5E) | 0x2121) as u16,
    }
}
unsafe extern "C" fn m_jis(cp: u32) -> u16 {
    jis_of(cp)
}
unsafe extern "C" fn m_screen_info(out: *mut u8) {
    let i = &mut *(out as *mut ScreenInfo);
    *i = ScreenInfo::ZERO;
    i.width = ffi::W as u16;
    i.height = ffi::H as u16;
    i.bpp = if PACKED8 { 8 } else { 4 };
    i.format = if PACKED8 { GFX_FMT_PACKED8 } else { GFX_FMT_PLANAR4 };
}

static INIT: std::sync::Once = std::sync::Once::new();
fn install() {
    INIT.call_once(|| unsafe {
        let api: &'static mut os32api::KernelAPI = Box::leak(Box::new(os32api::mock_api()));
        api.mem_alloc = m_alloc;
        api.mem_free = m_free;
        api.font_source_open = m_open;
        api.font_source_read_at = m_read;
        api.font_source_close = m_close;
        api.kcg_read_ank = m_read_ank;
        api.kcg_read_kanji = m_read_kanji;
        api.unicode_to_jis = m_jis;
        api.gfx_screen_info = m_screen_info;
        os32api::os32_init(api);
        ffi::gfx_fb = ffi::GfxFramebuffer {
            width: ffi::W,
            height: ffi::H,
            pitch: ffi::W,
            planes: [ffi::FB.as_mut_ptr(), core::ptr::null_mut(), core::ptr::null_mut(), core::ptr::null_mut()],
        };
    });
}
fn set_mode(packed8: bool) {
    install();
    unsafe { PACKED8 = packed8 };
    gstate::refresh_screen_info();
}
fn screen() -> SurfaceId {
    surface::screen_surface()
}
fn fb() -> Vec<u8> {
    unsafe { ffi::FB.to_vec() }
}
fn fill_fb(c: u8) {
    unsafe { ffi::FB.fill(c) };
}
fn rom_reads() -> Vec<(char, u16)> {
    unsafe { std::mem::take(&mut ROM_READS) }
}

// ---- reference glyphs (font crate straight, no engine) -------------------------

struct FileSource(File, u64);
impl Source for FileSource {
    fn file_size(&self) -> u64 {
        self.1
    }
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> FontResult<usize> {
        self.0.read_at(dst, offset).map_err(|_| os32_font::reader::Error::Io)
    }
}
struct Reference {
    latin: Box<Face<FileSource>>,
    ja: Box<Face<FileSource>>,
    scratch: Box<RasterScratch>,
}
fn open_face(path: std::path::PathBuf) -> Box<Face<FileSource>> {
    let f = File::open(path).unwrap();
    let n = f.metadata().unwrap().len();
    let mut face = Box::new(Face::new());
    face.open(FileSource(f, n)).unwrap();
    face
}
static mut REF: Option<Reference> = None;
fn reference() -> &'static mut Reference {
    unsafe {
        REF.get_or_insert_with(|| Reference {
            latin: open_face(latin_path()),
            ja: open_face(ja_path()),
            scratch: Box::new(RasterScratch::new()),
        })
    }
}

type Cell = [u16; CELL_H_PX];
const CH: i32 = CELL_H_PX as i32;

/// TTF glyph cut to a cell of width `w` (None: in no face of the search order).
fn ttf_cell(cp: u32, half: bool, w: i32) -> Option<Cell> {
    let r = reference();
    let order: &[bool] = if half { &[true, false] } else { &[false] }; // true = latin
    for &latin in order {
        let face = if latin { &mut r.latin } else { &mut r.ja };
        let gid = match face.glyph_index(cp).unwrap() {
            Some(g) => g,
            None => continue,
        };
        let mut buf = vec![0u8; os32_font::limits::MAX_BITMAP_BYTES];
        let p = r.scratch.render(face, gid, PixelSize::Px16, &mut buf).ok()?;
        let ascent = i32::from(if latin { ASCENT_LATIN_PX } else { ASCENT_JA_PX });
        let mut cell: Cell = [0; CELL_H_PX];
        for row in 0..i32::from(p.height) {
            for col in 0..i32::from(p.width) {
                let byte = buf[(row * i32::from(p.pitch) + col / 8) as usize];
                if byte & (0x80 >> (col % 8)) == 0 {
                    continue;
                }
                let x = i32::from(p.left) + col;
                let y = ascent - i32::from(p.top) + row;
                if (0..w).contains(&x) && (0..CH).contains(&y) {
                    cell[y as usize] |= 0x8000 >> x;
                }
            }
        }
        return Some(cell);
    }
    None
}
fn rom_ank_cell(ch: u8) -> Cell {
    let mut c: Cell = [0; CELL_H_PX];
    for (r, b) in rom_ank(ch).iter().enumerate() {
        c[r] = u16::from(*b) << 8;
    }
    c
}
fn rom_cp_cell(cp: u32) -> Cell {
    let ank = unsafe { ffi::unicode_to_ank(cp) };
    if ank != 0 {
        return rom_ank_cell(ank);
    }
    let mut jis = jis_of(cp);
    if jis == 0 {
        jis = 0x2222;
    }
    let p = rom_kanji(jis);
    let mut c: Cell = [0; CELL_H_PX];
    for r in 0..CELL_H_PX {
        c[r] = (u16::from(p[2 * r]) << 8) | u16::from(p[2 * r + 1]);
    }
    c
}

/// Expected cells of `s` (width, cell) with the design §2-2 routes for the app.
fn expect_cells(s: &str, ttf: bool) -> Vec<(i32, Cell)> {
    let mut out = Vec::new();
    for ch in s.chars() {
        let cp = ch as u32;
        if cp < 0x80 {
            let b = cp as u8;
            let cell = if ttf && (0x20..=0x7E).contains(&b) && b != 0x5C {
                ttf_cell(cp, true, 8).unwrap_or_else(|| rom_ank_cell(b))
            } else {
                rom_ank_cell(b)
            };
            out.push((8, cell));
        } else {
            // P3-4: U+00A5 is the ROM yen too (JF-Dot-Shinonome16 draws a backslash shape there).
            let cell = if ttf && cp != 0xA5 { ttf_cell(cp, false, 16) } else { None };
            out.push((16, cell.unwrap_or_else(|| rom_cp_cell(cp))));
        }
    }
    out
}

/// Paint the expected `s` into `img` (`iw` wide) cut by `clip` (x0, y0, x1, y1).
#[allow(clippy::too_many_arguments)]
fn expect_draw(img: &mut [u8], iw: i32, clip: (i32, i32, i32, i32), x: i32, y: i32, s: &str, fg: u8,
               bg: Option<u8>, ttf: bool) -> i32 {
    let mut pen = x;
    for (w, cell) in expect_cells(s, ttf) {
        for r in 0..CH {
            for c in 0..w {
                let (px, py) = (pen + c, y + r);
                if px < clip.0 || py < clip.1 || px >= clip.2 || py >= clip.3 {
                    continue;
                }
                let ink = cell[r as usize] & (0x8000 >> c) != 0;
                let v = if ink { Some(fg) } else { bg };
                if let Some(v) = v {
                    img[(py * iw + px) as usize] = v;
                }
            }
        }
        pen += w;
    }
    pen - x
}

const FULL: (i32, i32, i32, i32) = (0, 0, ffi::W, ffi::H);
/// ASCII 0x20..=0x7E in two lines (one line would leave the 640px screen).
fn ascii_lines() -> (String, String) {
    let all: String = (0x20u8..=0x7E).map(|b| b as char).collect();
    (all[..48].to_string(), all[48..].to_string())
}
/// Japanese, Latin-1, ¥ (U+00A5), half-width kana, full-width A, a control byte,
/// and a code point in neither face nor JIS (□ from ROM).
const JA: &str = "議鬱薔薇機械設定 亜Åç¥ｱＡ\u{1}😀";

fn corpus() -> Vec<(i32, i32, String)> {
    let (a, b) = ascii_lines();
    vec![(3, 5, a), (3, 23, b), (5, 41, JA.to_string()), (0, 59, "gjpqy|W_M@m".to_string())]
}

fn draw_corpus(style: Style) -> Vec<i32> {
    corpus().iter().map(|(x, y, s)| draw::text(screen(), *x, *y, s.as_bytes(), style)).collect()
}
fn expect_corpus(base: u8, fg: u8, bg: Option<u8>, clip: (i32, i32, i32, i32)) -> Vec<u8> {
    let mut img = vec![base; (ffi::W * ffi::H) as usize];
    for (x, y, s) in corpus() {
        expect_draw(&mut img, ffi::W, clip, x, y, &s, fg, bg, true);
    }
    img
}
fn diff_at(a: &[u8], b: &[u8], iw: i32) -> String {
    match a.iter().zip(b).position(|(x, y)| x != y) {
        Some(i) => format!("first diff at ({}, {}): got {} want {}", i as i32 % iw, i as i32 / iw, a[i], b[i]),
        None => "equal".to_string(),
    }
}
fn engine_state() -> State {
    font::ensure_open().map(|e| e.state()).unwrap_or(State::Closed)
}

// ---- tests (one process, run in name order) ----------------------------------

#[test]
fn t00_no_engine_draws_everything_from_rom() {
    set_mode(false);
    unsafe { FAIL_ALLOC = true };
    fill_fb(0);
    let w = draw::text(screen(), 0, 0, "Ag亜\\".as_bytes(), Style::new(15, 1));
    check(w == 8 + 8 + 16 + 8, "advance");
    let reads = rom_reads();
    check(reads == vec![('a', 0x41), ('a', 0x67), ('k', 0x3021), ('a', 0x5C)], "no-engine-rom");
    let mut want = vec![0u8; (ffi::W * ffi::H) as usize];
    expect_draw(&mut want, ffi::W, FULL, 0, 0, "Ag亜\\", 15, Some(1), false);
    check(fb() == want, "no-engine-rom");
    unsafe { FAIL_ALLOC = false };
    font::retry_if_kcg_only(); // lifts the allocation block
}

#[test]
fn t01_pixel_match_planar() {
    set_mode(false);
    fill_fb(0);
    let _ = rom_reads();
    draw_corpus(Style::new(15, 1));
    check(engine_state() == State::Ready, "engine-ready");
    let reads = rom_reads();
    // b4/P3-4: 0x5C is the ROM ¥; Inconsolata's '\' is never drawn.
    check(reads.contains(&('a', 0x5C)), "yen");
    check(!reads.iter().any(|r| r.0 == 'a' && r.1 != 0x5C && r.1 != 0x01 && r.1 < 0x80), "ttf-ascii-from-rom");
    check(reads.contains(&('k', 0x2222)), "missing-glyph-rom");
    let want = expect_corpus(0, 15, Some(1), FULL);
    let got = fb();
    if got != want {
        println!("planar: {}", diff_at(&got, &want, ffi::W));
    }
    check(got == want, "pixel-match");
    check(unsafe { ffi::OOB } == 0, "cell-contract");
    let s = font::stats();
    println!("planar corpus: raster={} hit={} miss={} missing={} uncached={} validate={}",
             s.engine.raster, s.engine.hit_bitmap, s.engine.miss, s.engine.cmap_missing, s.engine.uncached,
             s.engine.validate);
    check(s.engine.validate == 2 * corpus().len() as u32, "validate-once-per-text");
    check(!font::ensure_open().unwrap().is_busy(), "busy-after-text");
}

#[test]
fn t02_pixel_match_packed8_is_the_same() {
    set_mode(true);
    fill_fb(0);
    let before = font::stats().engine;
    draw_corpus(Style::new(15, 1));
    let got = fb();
    let want = expect_corpus(0, 15, Some(1), FULL);
    if got != want {
        println!("packed8: {}", diff_at(&got, &want, ffi::W));
    }
    check(got == want, "pixel-match");
    let after = font::stats().engine;
    // Warm cache: the second pass rasterises nothing (no cmap, no read_at).
    check(after.raster == before.raster && after.hit_bitmap > before.hit_bitmap, "cache-hit");
    set_mode(false);
}

#[test]
fn t03_clip_is_per_pixel_in_both_modes() {
    // A clip whose edges cut through glyph rows and columns, half and full width.
    let clip = Rect::new(7, 9, 150, 40);
    let cl = (7, 9, 7 + 150, 9 + 40);
    for packed in [false, true] {
        set_mode(packed);
        fill_fb(4);
        clip::set_base_clip(screen(), clip);
        draw_corpus(Style::new(15, 1));
        clip::clear_base_clip();
        let got = fb();
        let want = expect_corpus(4, 15, Some(1), cl);
        if got != want {
            println!("clip packed={packed}: {}", diff_at(&got, &want, ffi::W));
        }
        check(got == want, "clip");
    }
    set_mode(false);
}

#[test]
fn t04_transparent_draws_fg_only() {
    set_mode(false);
    fill_fb(7);
    let st = Style::new(15, 1).with_flags(GUI_STYLE_TRANSPARENT_BG);
    draw_corpus(st);
    let want = expect_corpus(7, 15, None, FULL);
    check(fb() == want, "transparent");
}

#[test]
fn t05_offscreen_full_width() {
    set_mode(false);
    let id = surface::create_surface(64, 32);
    let s = gstate::st();
    let idx = s.resolve(id).unwrap();
    let surf = match s.surfaces[idx].kind {
        gstate::SurfaceKind::Offscreen { surf } => surf,
        _ => unreachable!(),
    };
    // Clipped by the surface: the last glyph runs over the right edge.
    let txt = "議A亜gW議";
    let w = draw::text(id, 2, 3, txt.as_bytes(), Style::new(9, 2));
    let got = unsafe { std::slice::from_raw_parts((*surf).planes[0], 64 * 32).to_vec() };
    let mut want = vec![0u8; 64 * 32];
    let ww = expect_draw(&mut want, 64, (0, 0, 64, 32), 2, 3, txt, 9, Some(2), true);
    check(w == ww, "advance");
    if got != want {
        println!("offscreen: {}", diff_at(&got, &want, 64));
    }
    check(got.iter().any(|&p| p == 9), "offscreen");
    check(got == want, "offscreen");
    surface::destroy_surface(id);
}

#[test]
fn t06_return_equals_measure() {
    set_mode(false);
    let widths = draw_corpus(Style::new(15, 1));
    for ((_, _, s), w) in corpus().iter().zip(widths) {
        let (mw, mh) = draw::measure_text(s.as_bytes());
        check(w == mw && mh == CH, "advance");
        let mut img = vec![0u8; (ffi::W * ffi::H) as usize];
        check(expect_draw(&mut img, ffi::W, FULL, 0, 0, s, 1, None, true) == mw, "advance");
    }
    // NUL ends the text; an incomplete trailing sequence is cut at the boundary.
    check(draw::text(screen(), 0, 0, b"ab\0cd", Style::new(1, 0)) == 16, "advance");
    check(draw::text(screen(), 0, 0, b"a\xE4\xBA", Style::new(1, 0)) == 8, "advance");
}

#[test]
fn t07_cell_contract() {
    set_mode(false);
    // b4② Inconsolata ASCII 95 fits the 8x16 cell at ascent 13 (no clipping needed).
    let e = font::ensure_open().unwrap();
    assert!(e.begin());
    let mut out_of_cell = 0;
    for b in 0x21u8..=0x7E {
        let mut g = Glyph::default();
        let r = font::fp_scope(|| e.resolve(u32::from(b), os32_font::engine::Cell::Half, PixelSize::Px16, &mut g));
        if r != os32_font::engine::Resolved::Bitmap {
            continue;
        }
        let en = g.entry;
        let fits = en.left >= 0 && i32::from(en.left) + i32::from(en.width) <= 8 && en.top <= ASCENT_LATIN_PX
            && i32::from(en.height) - i32::from(en.top) <= CH - i32::from(ASCENT_LATIN_PX);
        if !fits {
            out_of_cell += 1;
            println!("out of cell: {:?} {:?}", b as char, en);
        }
        e.release();
    }
    e.end();
    check(out_of_cell == 0, "ascii-fits-cell");
    // A bitmap wider and taller than the cell: columns past the cell and rows
    // outside 0..16 are dropped (G2 cell contract), nothing wraps.
    let mut g = Glyph::default();
    g.entry.left = 2;
    g.entry.top = 15;
    g.entry.width = 12;
    g.entry.height = 20;
    g.entry.pitch = 2;
    g.ascent = 13;
    // Bitmap rows 0, 1, 18, 19 fall outside the cell (ascent 13 - top 15 = -2);
    // rows 2 and 17 land on cell rows 0 and 15. Every inked row is 16 px wide.
    let mut bits = vec![0u8; 40];
    for r in [0usize, 1, 2, 17, 18, 19] {
        bits[2 * r] = 0xFF;
        bits[2 * r + 1] = 0xFF;
    }
    let half = draw::cell_from_glyph(&g, &bits, 8);
    let full = draw::cell_from_glyph(&g, &bits, 16);
    let edge = |c: &[u16; CELL_H_PX], v: u16| {
        c.iter().enumerate().all(|(r, &x)| x == if r == 0 || r == 15 { v } else { 0 })
    };
    check(edge(&half, 0x3F00), "cell-contract");
    check(edge(&full, 0x3FFF), "cell-contract");
}

fn ink_rows(cell_x: i32, cell_y: i32, w: i32) -> Vec<i32> {
    let f = fb();
    (0..CH).filter(|r| (0..w).any(|c| f[((cell_y + r) * ffi::W + cell_x + c) as usize] == 15)).collect()
}

#[test]
fn t08_fallback_baseline() {
    set_mode(false);
    // TTF: 亜 from Shinonome (ascent 14), g from Inconsolata (ascent 13).
    fill_fb(0);
    draw::text(screen(), 0, 0, "亜g".as_bytes(), Style::new(15, 0));
    let ttf_kanji = ink_rows(0, 0, 16);
    let ttf_g = ink_rows(16, 0, 8);
    // KCG only (font_set = 1): the same two from ROM, drawn from the cell top.
    cfgro::set(Some((1, 1)));
    font::reload();
    fill_fb(0);
    let _ = rom_reads();
    draw::text(screen(), 0, 0, "亜g".as_bytes(), Style::new(15, 0));
    check(engine_state() == State::KcgOnly(os32_font::engine::Reason::Config), "kcg-only-config");
    check(rom_reads() == vec![('k', 0x3021), ('a', u16::from(b'g'))], "kcg-only-config");
    let kcg_kanji = ink_rows(0, 0, 16);
    let kcg_g = ink_rows(16, 0, 8);
    println!("亜 rows ttf {:?}..{:?} kcg {:?}..{:?}; g bottom ttf {:?} kcg {:?}",
             ttf_kanji.first(), ttf_kanji.last(), kcg_kanji.first(), kcg_kanji.last(), ttf_g.last(), kcg_g.last());
    check(kcg_kanji.last() == ttf_kanji.last(), "fallback-baseline");
    check(kcg_kanji.first() == Some(&(ROM_KANJI_TOP as i32)), "fallback-baseline");
    let (a, b) = (*ttf_g.last().unwrap(), *kcg_g.last().unwrap());
    check((a - b).abs() <= 1, "fallback-baseline");
    cfgro::set(Some((0, 1)));
    font::reload();
}

#[test]
fn t09_trim_regenerates() {
    set_mode(false);
    fill_fb(0);
    draw_corpus(Style::new(15, 1));
    let first = fb();
    let s0 = font::stats();
    check(font::trim(), "trim");
    check(font::stats().has_arena == 0, "trim");
    fill_fb(0);
    draw_corpus(Style::new(15, 1));
    let s1 = font::stats();
    check(fb() == first, "trim");
    check(s1.has_arena == 1 && s1.arena_allocs == s0.arena_allocs + 1, "trim");
    check(s1.engine.raster > s0.engine.raster, "trim");
}

#[test]
fn t10_ev_font_draws_no_old_entry() {
    set_mode(false);
    fill_fb(0);
    draw_corpus(Style::new(15, 1));
    let first = fb();
    let slot = font::stats().set_slot;
    let s0 = font::stats().engine;
    font::reload(); // GUI_EV_FONT
    fill_fb(0);
    draw_corpus(Style::new(15, 1));
    let s1 = font::stats();
    check(s1.set_slot != slot, "ev-font");
    // Every TTF code point of the new generation misses once: no old entry is drawn.
    let mut distinct = std::collections::BTreeSet::new();
    for (_, _, t) in corpus() {
        for ch in t.chars() {
            let cp = ch as u32;
            if (cp >= 0x80 && cp != 0xA5) || ((0x20..=0x7E).contains(&cp) && cp != 0x5C) {
                distinct.insert(cp);
            }
        }
    }
    let misses = s1.engine.miss - s0.miss;
    println!("after EV_FONT: misses={misses} distinct TTF code points={}", distinct.len());
    check(misses as usize == distinct.len(), "ev-font");
    check(fb() == first, "ev-font");
}

#[test]
fn t11_yen_sign_is_rom() {
    // P3-4 (S7 guest acceptance): U+00A5 is drawn from the ROM ANK 0x5C in a 16px cell,
    // never from JF-Dot-Shinonome16 (whose U+00A5 glyph is a backslash shape).
    set_mode(false);
    fill_fb(0);
    let _ = rom_reads();
    let w = draw::text(screen(), 0, 0, "\u{A5}".as_bytes(), Style::new(15, 1));
    check(w == 16, "yen-advance");
    check(rom_reads() == vec![('a', 0x5C)], "yen-u00a5-rom");
    let mut want = vec![0u8; (ffi::W * ffi::H) as usize];
    expect_draw(&mut want, ffi::W, FULL, 0, 0, "\u{A5}", 15, Some(1), true);
    check(fb() == want, "yen-u00a5-pixels");
}

#[test]
fn zz_verdict() {
    if unsafe { FAILED } {
        std::process::exit(101);
    }
}
