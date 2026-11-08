//! 実物の WM / visible / damage / composite を host ABI で動かす。
use super::*;
use crate::mocks;
use std::sync::Mutex;
use std::sync::atomic::{AtomicI32, Ordering};
static FLUSH_RC: AtomicI32 = AtomicI32::new(0);
static COPY_RC: AtomicI32 = AtomicI32::new(0);
static COPIES: Mutex<Vec<(Rect, Rect)>> = Mutex::new(Vec::new());

#[no_mangle]
pub extern "C" fn gfx_flush() -> i32 { FLUSH_RC.load(Ordering::SeqCst) }
#[no_mangle]
pub extern "C" fn gfx_copy_rect(dx: i32, dy: i32, sx: i32, sy: i32, w: i32, h: i32) -> i32 {
    assert!(mocks::present_counts().1 > 0, "present before COPY");
    if COPY_RC.load(Ordering::SeqCst) < 0 { return -1; }
    COPIES.lock().unwrap().push((Rect::new(dx, dy, w, h), Rect::new(sx, sy, w, h)));
    let pixels = mocks::pixels(); // memmove semantics within each COPY
    for y in 0..h { for x in 0..w {
        let c = pixels[(sy + y) as usize * mocks::W + (sx + x) as usize];
        mocks::gfx_pixel(dx + x, dy + y, c);
    }}
    0
}
fn setup() -> GuiState {
    mocks::init();
    mocks::clear(0);
    FLUSH_RC.store(0, Ordering::SeqCst);
    COPY_RC.store(0, Ordering::SeqCst);
    COPIES.lock().unwrap().clear();
    let mut st = GuiState::NEW;
    st.screen_w = 640;
    st.screen_h = 480;
    for (i, (x, y, w, h)) in [(0, 40, 160, 160), (32, 64, 128, 128), (112, 64, 64, 64)].into_iter().enumerate() {
        let win = &mut st.windows[i];
        win.used = true; win.visible = true; win.gen = 1;
        win.x = x; win.y = y; win.w = w; win.h = h;
        st.zorder[i] = i;
    }
    st.z_count = 3;
    visible::recompute_and_expose(&mut st);
    for win in &mut st.windows { win.dirty.clear(); }
    st
}
fn has(set: &RectSet, x: i32, y: i32) -> bool {
    set.as_slice().iter().any(|r| r.contains(x, y))
}
fn pattern(x: i32, y: i32) -> u8 { (32 + (x * 13 + y * 7) % 160) as u8 }
fn paint_source(st: &GuiState, index: usize) {
    let w = &st.windows[index];
    let org = w.client_origin();
    for r in w.vis.as_slice() { for y in r.y..r.bottom() { for x in r.x..r.right() {
        mocks::gfx_pixel(x + org.0, y + org.1, pattern(x, y));
    }}}
}

#[test]
fn movecopy_three_windows_valid_pixels_and_exposed_paint() {
    let mut st = setup();
    st.windows[1].dirty.push(Rect::new(0, 0, 32, 32));
    st.windows[1].issued.push(Rect::new(32, 32, 32, 32));
    let old_vis = st.windows[1].vis;
    let dirty = st.windows[1].dirty;
    let issued = st.windows[1].issued;
    let old_org = st.windows[1].client_origin();
    let back_vis = st.windows[0].vis;
    paint_source(&st, 1);
    super::move_window(&mut st, 1, 64, 96);
    let org = st.windows[1].client_origin();
    let copies = COPIES.lock().unwrap();
    assert!(!copies.is_empty());
    let pixels = mocks::pixels();
    for (dst, src) in copies.iter() {
        for y in 0..src.h { for x in 0..src.w {
            let lx = src.x + x - old_org.0;
            let ly = src.y + y - old_org.1;
            assert!(has(&old_vis, lx, ly) && !has(&dirty, lx, ly) && !has(&issued, lx, ly));
            assert!(has(&st.windows[1].vis, dst.x + x - org.0, dst.y + y - org.1), "recompute before COPY");
            assert_eq!(pixels[(dst.y+y) as usize*mocks::W+(dst.x+x) as usize], pattern(lx, ly), "COPY before composite");
        }}
    }
    for r in st.windows[1].vis.as_slice() { for y in r.y..r.bottom() { for x in r.x..r.right() {
        let copied = copies.iter().any(|(d, _)| d.contains(org.0+x, org.1+y));
        if !copied { assert!(has(&st.windows[1].dirty, x, y), "vis minus ok needs Paint"); }
    }}}
    let mut exposed = 0;
    for r in st.windows[0].vis.as_slice() { for y in r.y..r.bottom() { for x in r.x..r.right() {
        if !has(&back_vis, x, y) {
            exposed += 1;
            assert!(has(&st.windows[0].dirty, x, y));
        }
    }}}
    assert!(exposed > 0);
    assert!(st.windows[1].configure_pending);
}

#[test]
fn movecopy_fragment_order_all_directions() {
    for (dx, dy) in [(5,0), (-5,0), (0,5), (0,-5), (5,5), (-5,-5), (5,-5), (-5,5)] {
        let mut st = setup();
        wm::flush_present();
        let mut ok = RectSet::EMPTY;
        // 左の縦長と右の短い断片。単純な矩形順では複写元が壊れる配置。
        ok.push(Rect::new(0, 0, 10, 30));
        ok.push(Rect::new(10, 5, 10, 10));
        ok.push(Rect::new(10, 20, 10, 10));
        for r in ok.as_slice() { for y in r.y..r.bottom() { for x in r.x..r.right() {
            mocks::gfx_pixel(100+x, 100+y, pattern(x,y));
        }}}
        assert!(copy_valid(&ok, (100,100), (100+dx,100+dy)));
        let p = mocks::pixels();
        for r in ok.as_slice() { for y in r.y..r.bottom() { for x in r.x..r.right() {
            assert_eq!(p[(100+dy+y) as usize*mocks::W+(100+dx+x) as usize], pattern(x,y));
        }}}
        st.windows[1].vis.clear();
    }
}

#[test]
fn movecopy_failure_repaints_and_frame_damage_excludes_copy() {
    let mut st = setup();
    FLUSH_RC.store(-1, Ordering::SeqCst);
    super::move_window(&mut st, 1, 64, 96);
    assert!(COPIES.lock().unwrap().is_empty());
    assert_eq!(st.windows[1].dirty.len, 1);
    let (w,h) = st.windows[1].client_size();
    assert!(st.windows[1].dirty.rects[0] == Rect::new(0,0,w,h));
    let mut st = setup();
    COPY_RC.store(-1, Ordering::SeqCst);
    super::move_window(&mut st, 1, 64, 96);
    for r in st.windows[1].vis.as_slice() {
        assert!(has(&st.windows[1].dirty, r.x, r.y));
    }
    let mut st = setup();
    st.drag_index = 1;
    st.drag_frame = st.windows[1].outer();
    super::move_window(&mut st, 1, 64, 96);
    assert!(COPIES.lock().unwrap().is_empty(), "erased frame damage excludes source");
    assert!(st.drag_frame.is_empty());
    assert_eq!(st.drag_index, -1);
}

#[test]
fn movecopy_capacity_falls_back_to_paint() {
    let mut st = setup();
    st.windows[1].vis.clear();
    for i in 0..MAX_VIS {
        st.windows[1].vis.push(Rect::new(i as i32 * 8, 0, 4, 120));
    }
    // 16 本それぞれを上下に分けると容量を超える。
    st.windows[1].dirty.push(Rect::new(0, 50, 128, 10));
    super::move_window(&mut st, 1, 64, 96);
    assert!(COPIES.lock().unwrap().is_empty());
    for r in st.windows[1].vis.as_slice() {
        for y in r.y..r.bottom() { for x in r.x..r.right() {
            assert!(has(&st.windows[1].dirty, x, y));
        }}
    }
}
