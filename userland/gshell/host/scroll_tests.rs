use super::*;
use crate::{handler, mocks};
use os32api::gui::proto::*;
use std::sync::{Mutex, atomic::{AtomicI32, Ordering}};
static FLUSH: AtomicI32 = AtomicI32::new(0);
static COPY_RC: AtomicI32 = AtomicI32::new(0);
static COPIES: Mutex<Vec<(Rect, Rect)>> = Mutex::new(Vec::new());
#[no_mangle]
pub extern "C" fn gshell_scroll_test_flush() -> i32 { FLUSH.load(Ordering::SeqCst) }
#[no_mangle]
pub extern "C" fn gshell_scroll_test_copy(dx: i32, dy: i32, sx: i32, sy: i32, w: i32, h: i32) -> i32 {
    if COPY_RC.load(Ordering::SeqCst) < 0 { return -1; }
    COPIES.lock().unwrap().push((Rect::new(dx,dy,w,h), Rect::new(sx,sy,w,h)));
    let p = mocks::pixels();
    for y in 0..h { for x in 0..w {
        mocks::gfx_pixel(dx+x, dy+y, p[(sy+y) as usize*mocks::W+(sx+x) as usize]);
    }}
    0
}
fn setup() -> GuiState {
    mocks::init();
    mocks::clear(0);
    FLUSH.store(0, Ordering::SeqCst);
    COPY_RC.store(0, Ordering::SeqCst);
    COPIES.lock().unwrap().clear();
    let mut st = GuiState::NEW;
    st.screen_w = 640; st.screen_h = 480;
    let w = &mut st.windows[0];
    w.used = true; w.visible = true; w.gen = 1; w.owner = 2;
    w.x = 64; w.y = 64; w.w = 192; w.h = 192;
    w.vis.push(Rect::new(0,0,64,128));
    w.vis.push(Rect::new(64,16,64,32));
    w.vis.push(Rect::new(64,80,64,48));
    st
}
fn has(set: &RectSet, x: i32, y: i32) -> bool { set.as_slice().iter().any(|r| r.contains(x,y)) }
fn pattern(x: i32, y: i32) -> u8 { (32+(x*13+y*7)%160) as u8 }
#[test]
fn scroll_formula_pixels_all_directions_and_outside_rect() {
    for (dx,dy) in [(5,0),(-5,0),(0,5),(0,-5),(5,5),(-5,-5),(5,-5),(-5,5),(0,0),(96,0),(0,-96)] {
        let mut st = setup();
        let rect = Rect::new(16,16,96,96);
        let vis = st.windows[0].vis;
        st.windows[0].dirty.push(Rect::new(24,24,8,16));
        let dirty = st.windows[0].dirty;
        let org = st.windows[0].client_origin();
        for y in 0..160 { for x in 0..160 { mocks::gfx_pixel(org.0+x,org.1+y,pattern(x,y)); }}
        let before = mocks::pixels();
        scroll(&mut st,0,rect,dx,dy);
        let copies = COPIES.lock().unwrap();
        let after = mocks::pixels();
        for y in 0..160 { for x in 0..160 {
            let expected = rect.contains(x,y) && has(&vis,x,y)
                && rect.contains(x-dx,y-dy) && has(&vis,x-dx,y-dy)
                && !has(&dirty,x-dx,y-dy) && dx.abs()<rect.w && dy.abs()<rect.h;
            let copied = copies.iter().any(|(r,_)| r.contains(org.0+x,org.1+y));
            assert_eq!(copied,expected,"cp formula at ({x},{y}) d=({dx},{dy})");
            let off = (org.1+y) as usize*mocks::W+(org.0+x) as usize;
            if copied { assert_eq!(after[off],pattern(x-dx,y-dy),"overlap order"); }
            else {
                assert_eq!(after[off],before[off],"outside cp unchanged");
                if rect.contains(x,y) && has(&vis,x,y) { assert!(has(&st.windows[0].dirty,x,y),"Paint covers rv minus cp"); }
            }
        }}
    }
}
#[test]
fn scroll_failure_and_capacity_repaint() {
    for mode in 0..3 {
        let mut st = setup();
        if mode == 0 { FLUSH.store(-1,Ordering::SeqCst); }
        if mode == 1 { COPY_RC.store(-1,Ordering::SeqCst); }
        if mode == 2 { st.windows[0].vis_capped = true; }
        scroll(&mut st,0,Rect::new(0,0,128,128),0,16);
        assert!(COPIES.lock().unwrap().is_empty());
        for r in st.windows[0].vis.as_slice() {
            for y in r.y..r.bottom() { for x in r.x..r.right() { assert!(has(&st.windows[0].dirty,x,y)); }}
        }
    }
    let mut st = setup();
    st.windows[0].vis.clear();
    for i in 0..MAX_VIS { st.windows[0].vis.push(Rect::new(i as i32*8,0,4,128)); }
    st.windows[0].dirty.push(Rect::new(0,48,128,16));
    scroll(&mut st,0,Rect::new(0,0,128,128),0,4);
    assert!(COPIES.lock().unwrap().is_empty(),"fragment overflow must disable COPY");
}
#[test]
fn scroll_protocol_dispatch_commit_and_old_version() {
    let mut st = setup();
    let shm = mocks::Shm::new();
    st.shm_base = shm.base(); st.inited = true;
    assert_eq!(handler::op_init(&mut st,2,1),OS32_ERR_VERSION);
    assert!(!st.slots[0].used);
    assert_eq!(handler::op_init(&mut st,2,GUI_PROTO_VERSION as u32),0);
    assert_eq!(slot::read_header(&st,0).proto_version,2);
    let req = reqs::GuiReqScroll { window: st.windows[0].id(0),
        rect: GuiRect16 { x: 0,y: 0,w: 128,h: 128 }, dx: 0,dy: 16 };
    unsafe { core::ptr::write_unaligned(slot::req_ptr(&st,0) as *mut reqs::GuiReqScroll,req); }
    st.windows[0].issued.push(Rect::new(0,0,32,32));
    let op = handler::lookup(GUI_OP_SCROLL).expect("SCROLL table entry");
    assert_eq!(op(&mut st,3,0,0),OS32_ERR_INVAL);
    assert!(!st.windows[0].issued.is_empty());
    let presents = mocks::present_counts().1;
    assert_eq!(op(&mut st,2,0,0),0);
    assert!(st.windows[0].issued.is_empty(),"COMMIT before scroll");
    assert!(mocks::present_counts().1 > presents);
    assert!(!COPIES.lock().unwrap().is_empty());
    st.windows[0].gen += 1;
    assert_eq!(op(&mut st,2,0,0),OS32_ERR_STALE);
    // arg=0 の従来の版指定なし INIT は維持する。
    assert_eq!(handler::op_init(&mut st,3,0),1);
}

#[test]
fn scroll_excludes_issued_topmost_and_offscreen_source() {
    for mode in 0..3 {
        let mut st = setup();
        st.windows[0].vis.clear();
        st.windows[0].vis.push(Rect::new(0,0,128,128));
        let org = st.windows[0].client_origin();
        let hole = Rect::new(16,16,32,32);
        if mode == 0 { st.windows[0].issued.push(hole); }
        if mode == 1 {
            st.drag_index = 0;
            st.drag_frame = hole.translate(org.0,org.1);
        }
        if mode == 2 { st.windows[0].x = -32; }
        let org = st.windows[0].client_origin();
        scroll(&mut st,0,Rect::new(0,0,128,128),8,8);
        let copies = COPIES.lock().unwrap();
        assert!(!copies.is_empty());
        for (_,src) in copies.iter() {
            assert!(src.x >= 0 && src.y >= 0);
            assert!(src.right() <= st.screen_w && src.bottom() <= st.screen_h);
            if mode == 0 { assert!(!src.intersects(&hole.translate(org.0,org.1))); }
            if mode == 1 {
                let frame = st.drag_frame;
                // 枠の上辺と左辺を元画素にしない。
                assert!(!src.intersects(&Rect::new(frame.x,frame.y,frame.w,1)));
                assert!(!src.intersects(&Rect::new(frame.x,frame.y,1,frame.h)));
            }
        }
    }
}

#[test]
fn scroll_frame_redraw_keeps_taskbar_on_top() {
    let mut st = setup();
    st.windows[0].vis.clear();
    st.windows[0].vis.push(Rect::new(0,0,128,128));
    st.windows[1].used = true;
    st.drag_index = 1;
    st.drag_frame = Rect::new(100,100,200,370);
    let rect = Rect::new(32,32,16,16);
    let org = st.windows[0].client_origin();
    let touched = rect.translate(org.0,org.1);
    assert!(touched.intersects(&Rect::new(100,100,1,370)));
    assert!(!touched.intersects(&taskbar::rect(&st)));
    taskbar::draw(&st, taskbar::rect(&st));
    let before = mocks::pixels();
    scroll(&mut st,0,rect,0,4);
    let after = mocks::pixels();
    let bar = taskbar::rect(&st);
    for y in bar.y..bar.bottom() { for x in bar.x..bar.right() {
        let i = y as usize * mocks::W + x as usize;
        assert_eq!(after[i],before[i],"taskbar pixel ({x},{y})");
    }}
}
