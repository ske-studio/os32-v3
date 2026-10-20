//! fontbench — T7b-b5 ①⑤ の計測 (製品経路: libos32gui の `text()` → Engine・字形 cache)。
//!
//! `gdi_test font seq|scatter` : 1000 字 (a6 の `font_test -p/-s` と同じ字の並び、
//!   corpus.rs) を 40 字 × 25 行で screen に描く。1 回目 = cold (この process で
//!   Engine・arena・face を初めて開く)、2 回目 = warm (同じ画面の 2 回目)、
//!   最後に先頭 600 字 (arena 128KiB の Cell 枠 3,200 に収まる) の 2 周目。各 pass の tick・
//!   1 回の text() (40 字) の最大 tick・字形 engine の数の差 (`font_stats`)・
//!   stack の high-water (入口から下 48KiB に印を塗って数える) を出す。
//! `gdi_test font x87` : CW=0x0E7F・ZE を立てた状態で未 cache 字を描き、
//!   text() の前後の FNSTENV (CW / SW / TAG) を比べる (b5⑤)。
//!
//! 結果は gfx を閉じてから kprintf (rshell で読む)。試験アプリ ([test])、製品ではない。

use crate::corpus::{SCATTER, SEQ};
use libos32gui::gapi as g;
use os32api::gui::proto::*;
use os32api::gui::types::{Rect, Style, SurfaceId};

const PER_LINE: usize = 40;
const LINES: usize = 25;
const SUBSET: usize = 600;
const STACK_PAINT: usize = 48 * 1024;
const STACK_MARK: u32 = 0x5A3C_96E1;
/// `AppFontStats` の u32 数 (engine Stats 17 + 15 + 束 5、TASK_FONTPERF F3 で末尾に足した)。
const NSTAT: usize = 37;
const I_STATE: usize = 17;
const I_REASON: usize = 18;
const I_ARENA: usize = 20;
const I_OPEN_RC: usize = 22;
const I_OPENS: usize = 23;
const I_ARENA_ALLOCS: usize = 29;
const I_ALLOC_FAILS: usize = 31;
/// 束の数 (`BundleStats`: ok / rejected / hit / miss / err)。
const I_BUNDLE_OK: usize = 32;
const I_BUNDLE_REJ: usize = 33;
const I_BUNDLE_HIT: usize = 34;
const I_BUNDLE_MISS: usize = 35;
const I_BUNDLE_ERR: usize = 36;

fn fstats() -> [u32; NSTAT] {
    let mut raw = [0u8; NSTAT * 4];
    let n = libos32gui::draw::font_stats(&mut raw);
    let mut out = [0u32; NSTAT];
    if n > 0 {
        let mut i = 0;
        while i < NSTAT && (i + 1) * 4 <= n as usize {
            out[i] = u32::from_le_bytes([raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2], raw[i * 4 + 3]]);
            i += 1;
        }
    }
    out
}

/// 1 字ずつの UTF-8 の境界 (先頭 byte の位置)。
fn char_starts(s: &[u8], out: &mut [usize; 1001]) -> usize {
    let mut n = 0;
    let mut i = 0;
    while i < s.len() && n < 1000 {
        out[n] = i;
        n += 1;
        let b = s[i];
        i += if b < 0x80 { 1 } else if b < 0xE0 { 2 } else if b < 0xF0 { 3 } else { 4 };
    }
    out[n] = s.len().min(i);
    n
}

#[derive(Clone, Copy)]
struct PassResult {
    ticks: u32,
    max_call: u32,
    d: [u32; NSTAT],
}

fn pass(screen: SurfaceId, s: &[u8], starts: &[usize; 1001], nchars: usize) -> PassResult {
    let st = Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW);
    let s0 = fstats();
    let t0 = os32api::get_tick();
    let mut max_call = 0u32;
    let mut line = 0usize;
    while line * PER_LINE < nchars && line < LINES {
        let a = line * PER_LINE;
        let b = core::cmp::min(a + PER_LINE, nchars);
        let c0 = os32api::get_tick();
        g::text(screen, 0, (line * 16) as i32, &s[starts[a]..starts[b]], st);
        let dc = os32api::get_tick().wrapping_sub(c0);
        if dc > max_call {
            max_call = dc;
        }
        line += 1;
    }
    let ticks = os32api::get_tick().wrapping_sub(t0);
    let s1 = fstats();
    let mut d = [0u32; NSTAT];
    let mut i = 0;
    while i < NSTAT {
        d[i] = s1[i].wrapping_sub(s0[i]);
        i += 1;
    }
    d[I_STATE] = s1[I_STATE];
    d[I_REASON] = s1[I_REASON];
    PassResult { ticks, max_call, d }
}

#[inline(never)]
fn esp() -> usize {
    let v: usize;
    unsafe { core::arch::asm!("mov {0}, esp", out(reg) v, options(nomem, nostack, preserves_flags)) };
    v
}

#[inline(never)]
fn paint_stack(top: usize) {
    let mut p = top - STACK_PAINT;
    let end = top - 256; /* この関数自身の frame より下だけ */
    while p < end {
        unsafe { core::ptr::write_volatile(p as *mut u32, STACK_MARK) };
        p += 4;
    }
}

fn stack_used(top: usize) -> u32 {
    let mut p = top - STACK_PAINT;
    while p < top && unsafe { core::ptr::read_volatile(p as *const u32) } == STACK_MARK {
        p += 4;
    }
    (top - p) as u32
}

fn report(label: &[u8], r: &PassResult) {
    let d = &r.d;
    os32api::kprint!(b"FBENCH %s t=%d maxcall=%d hit=%d hitfb=%d miss=%d\r\n\0",
        label.as_ptr(), r.ticks as i32, r.max_call as i32, d[0] as i32, d[1] as i32, d[2] as i32);
    os32api::kprint!(b"FBENCH %s commit=%d evict=%d uncached=%d raster=%d rerr=%d\r\n\0",
        label.as_ptr(), d[3] as i32, d[5] as i32, d[6] as i32, d[7] as i32, d[8] as i32);
    os32api::kprint!(b"FBENCH %s cmapmiss=%d validate=%d kcg=%d state=%d reason=%d\r\n\0",
        label.as_ptr(), d[9] as i32, d[10] as i32, d[12] as i32, d[I_STATE] as i32, d[I_REASON] as i32);
    os32api::kprint!(b"FBENCH %s g16hit=%d g16miss=%d g16err=%d\r\n\0",
        label.as_ptr(), d[I_BUNDLE_HIT] as i32, d[I_BUNDLE_MISS] as i32, d[I_BUNDLE_ERR] as i32);
}

pub fn run(screen: SurfaceId, sw: i32, sh: i32, mode: &[u8]) -> i32 {
    if mode == b"x87" {
        return x87(screen);
    }
    let s: &[u8] = if mode == b"scatter" { SCATTER } else { SEQ };
    let top = esp();
    paint_stack(top);
    let mut starts = [0usize; 1001];
    let n = char_starts(s, &mut starts);
    g::fill_rect(screen, Rect::new(0, 0, sw as i16, sh as i16), Style::new(0, GUI_COLOR_WINDOW));

    /* open: 最初の text() が Engine・arena・face を揃える (ASCII 1 字)。 */
    let s0 = fstats();
    let t0 = os32api::get_tick();
    g::text(screen, 0, sh - 16, b"A", Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));
    let topen = os32api::get_tick().wrapping_sub(t0);
    let s1 = fstats();

    let cold = pass(screen, s, &starts, n);
    let warm = pass(screen, s, &starts, n);
    let sub1 = pass(screen, s, &starts, SUBSET);
    let sub2 = pass(screen, s, &starts, SUBSET);
    unsafe {
        let a = os32api::api();
        (a.gfx_add_dirty_rect)(0, 0, sw, sh);
        (a.gfx_present_dirty)();
    }
    let used = stack_used(top);
    let fin = fstats();
    os32api::gfx::shutdown();

    os32api::kprint!(b"FBENCH n=%d open_t=%d opens=%d open_rc=%d state=%d reason=%d arena=%d\r\n\0",
        n as i32, topen as i32, s1[I_OPENS].wrapping_sub(s0[I_OPENS]) as i32, s1[I_OPEN_RC] as i32,
        s1[I_STATE] as i32, s1[I_REASON] as i32, s1[I_ARENA] as i32);
    os32api::kprint!(b"FBENCH g16 ok=%d rej=%d open_commit=%d open_hit=%d\r\n\0",
        s1[I_BUNDLE_OK] as i32, s1[I_BUNDLE_REJ] as i32, s1[3].wrapping_sub(s0[3]) as i32,
        s1[I_BUNDLE_HIT].wrapping_sub(s0[I_BUNDLE_HIT]) as i32);
    report(b"cold\0", &cold);
    report(b"warm\0", &warm);
    report(b"sub600_1\0", &sub1);
    report(b"sub600_2\0", &sub2);
    os32api::kprint!(b"FBENCH stack_used=%d (painted %d) arena_allocs=%d alloc_fails=%d\r\n\0",
        used as i32, STACK_PAINT as i32, fin[I_ARENA_ALLOCS] as i32, fin[I_ALLOC_FAILS] as i32);
    0
}

/* ---------------- ⑤ x87 ---------------- */

#[repr(C)]
#[derive(Clone, Copy)]
struct Env {
    w: [u32; 7],
}

#[inline(never)]
fn stenv(e: &mut Env) {
    unsafe {
        core::arch::asm!("fnstenv [{0}]", "fldenv [{0}]", in(reg) e as *mut Env, options(nostack));
    }
}

fn x87(screen: SurfaceId) -> i32 {
    let cw: u16 = 0x0E7F; /* RC=切捨て、PC=53bit、全例外マスク */
    unsafe {
        core::arch::asm!("fninit", "fldcw word ptr [{0}]", "fld1", "fldz", "fdivp st(1), st", "fstp st(0)",
                         in(reg) &cw as *const u16, options(nostack));
    }
    let mut before = Env { w: [0; 7] };
    let mut after = Env { w: [0; 7] };
    stenv(&mut before);
    let s0 = fstats();
    /* 未 cache の字 (この process で初めて描く) — miss → raster が fp_scope の中で走る。 */
    g::text(screen, 0, 0, "鬱薔薇議機械設定".as_bytes(), Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));
    stenv(&mut after);
    let s1 = fstats();
    os32api::gfx::shutdown();
    let cw_b = before.w[0] & 0xFFFF;
    let cw_a = after.w[0] & 0xFFFF;
    let sw_b = before.w[1] & 0xFFFF;
    let sw_a = after.w[1] & 0xFFFF;
    let tag_b = before.w[2] & 0xFFFF;
    let tag_a = after.w[2] & 0xFFFF;
    os32api::kprint!(b"X87 before cw=%04x sw=%04x tag=%04x\r\n\0", cw_b as i32, sw_b as i32, tag_b as i32);
    os32api::kprint!(b"X87 after  cw=%04x sw=%04x tag=%04x\r\n\0", cw_a as i32, sw_a as i32, tag_a as i32);
    os32api::kprint!(b"X87 raster=%d miss=%d state=%d\r\n\0",
        s1[7].wrapping_sub(s0[7]) as i32, s1[2].wrapping_sub(s0[2]) as i32, s1[I_STATE] as i32);
    let cw_ok = cw_b == cw_a;
    let top_ok = (sw_b >> 11) & 7 == (sw_a >> 11) & 7;
    let tag_ok = tag_b == tag_a;
    let exc_ok = sw_b & 0x3F == sw_a & 0x3F;
    os32api::kprint!(b"X87 cw_ok=%d top_ok=%d tag_ok=%d exc_flags_ok=%d\r\n\0",
        cw_ok as i32, top_ok as i32, tag_ok as i32, exc_ok as i32);
    if cw_ok && top_ok && tag_ok { 0 } else { 1 }
}
