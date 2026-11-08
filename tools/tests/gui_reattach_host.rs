use os32api::gui::types::{Rect, ScreenInfo, Style, GFX_FMT_PACKED8};
use std::sync::atomic::{AtomicI32, AtomicUsize, Ordering::SeqCst};
static CHECKS: AtomicUsize = AtomicUsize::new(0);
static DETACHES: AtomicUsize = AtomicUsize::new(0);
static RC: AtomicI32 = AtomicI32::new(0);
static WAIT_RESULT: AtomicI32 = AtomicI32::new(7);
static HEIGHT: AtomicI32 = AtomicI32::new(4);
static OFFSCREEN_WRITES: AtomicUsize = AtomicUsize::new(0);
static PRESENTS: AtomicUsize = AtomicUsize::new(0);
#[no_mangle] static mut gfx_api: *mut os32api::KernelAPI = core::ptr::null_mut();
#[no_mangle] static mut gfx_direct: i32 = 0;
#[no_mangle] extern "C" fn gfx_cmd_enqueue_fill(_:i32,_:i32,_:i32,_:i32,_:u8,_:u8) -> i32 { assert!(ALLOW_U2_FILLS.load(SeqCst), "unexpected direct fill"); PENDING_FILLS.fetch_add(1,SeqCst); 0 }
#[no_mangle] extern "C" fn gfx_cmd_enqueue_put(_:i32,_:i32,_:i32,_:i32,_:*const u8,_:u32,_:i32) -> i32 { panic!("unexpected direct put"); }
#[no_mangle] extern "C" fn gfx_cmd_enqueue_mono(_:i32,_:i32,_:i32,_:i32,_:*const u8,_:u32,_:u8,_:i32,_:i32,_:i32,_:i32,_:i32) -> i32 { panic!("unexpected direct mono"); }
#[no_mangle] static mut gfx_ready: i32 = 1;
#[no_mangle] static mut gfx_fb: ffi::GfxFramebuffer = ffi::GfxFramebuffer {
    width: 4, height: 4, pitch: 4, planes: [core::ptr::null_mut();4],
};
#[no_mangle] extern "C" fn libos32gfx_check() -> i32 {
    TRACE.lock().unwrap().push(1);
    CHECKS.fetch_add(1,SeqCst);
    let rc=RC.load(SeqCst);
    unsafe { gfx_ready = if rc < 0 { 0 } else { 1 }; }
    rc
}
#[no_mangle] extern "C" fn libos32gfx_attach_checked() -> i32 { libos32gfx_check() }
#[no_mangle] extern "C" fn libos32gfx_detach() {
    DETACHES.fetch_add(1,SeqCst);
    unsafe { gfx_ready=0; }
}
#[no_mangle] extern "C" fn libos32gfx_shutdown() { libos32gfx_detach(); }
#[no_mangle] extern "C" fn gfx_present() { libos32gfx_check(); }
#[no_mangle] extern "C" fn gfx_pixel(_:i32,_:i32,_:u8) { panic!("unexpected planar write"); }
#[no_mangle] extern "C" fn gfx_get_pixel(_:i32,_:i32) -> u8 { panic!("unexpected planar read"); }
#[no_mangle] extern "C" fn gfx_fill_rect(_:i32,_:i32,_:i32,_:i32,_:u8) { panic!("unexpected planar fill"); }
#[no_mangle] extern "C" fn gfx_surface_pixel(_: *mut ffi::GfxSurface,_:i32,_:i32,_:u8) { OFFSCREEN_WRITES.fetch_add(1,SeqCst); }
#[no_mangle] extern "C" fn gfx_surface_fill_rect(_: *mut ffi::GfxSurface,_:i32,_:i32,_:i32,_:i32,_:u8) { panic!("unexpected surface fill"); }
unsafe extern "C" fn screen(out: *mut u8) {
    let mut info=ScreenInfo::ZERO;
    info.width=4;info.height=HEIGHT.load(SeqCst) as u16;info.bpp=8;info.format=GFX_FMT_PACKED8;
    *(out as *mut ScreenInfo)=info;
}
unsafe extern "C" fn wait(op:u32,_:u32) -> i32 {
    assert_eq!(op,os32api::gui::proto::GUI_OP_WAIT);
    HEIGHT.store(2,SeqCst); // mode changed while parked
    WAIT_RESULT.load(SeqCst)
}
unsafe extern "C" fn key_wait() -> i32 { 65 }
static POLL_RESULT: AtomicI32 = AtomicI32::new(-1);
unsafe extern "C" fn key_poll() -> i32 { POLL_RESULT.load(SeqCst) }
unsafe extern "C" fn present() { PRESENTS.fetch_add(1,SeqCst); }
unsafe extern "C" fn clear() {}

#[test]
fn return_gate_and_painter() {
    let mut api=os32api::mock_api();api.gfx_screen_info=screen;api.gui_call=wait;
    api.kbd_getchar=key_wait;api.kbd_trygetchar=key_poll;
    api.gfx_present_dirty=present;api.tvram_clear=clear;
    api.version=os32api::OS32X_HDR_V3_MIN_API;
    os32api::os32_init(&mut api);
    let mut pixels=[0u8;16];
    unsafe { gfx_fb.planes[0]=pixels.as_mut_ptr(); }
    assert_eq!(client::attach_gfx(),0);
    let surf=surface::screen_surface();
    assert_eq!(surface::surface_size(surf),(4,4));
    let before=CHECKS.load(SeqCst);
    assert_eq!(client::wait(1).unwrap_or(-999),7);
    assert_eq!(CHECKS.load(SeqCst),before+1,"return check missing");
    assert!(!gstate::st().screen_valid,"stale screen cache");
    // Use the old SurfaceId directly, without calling screen_surface again.
    draw::fill_rect(surf,Rect::new(0,0,4,4),Style::new(0,9));
    assert_eq!(&pixels[..8],&[9;8]);assert_eq!(&pixels[8..],&[0;8],"old geometry used");
    assert_eq!(surface::surface_size(surf),(4,2));
    assert!(surface::screen_surface()==surf);
    // ready=0 must guard even a non-null stale PACKED8 pointer.
    pixels.fill(0);unsafe { gfx_ready=0; }
    let t=clip::Target {idx:0,ox:0,oy:0,offscreen:false,clip:Rect::new(0,0,4,2)};
    let painter=draw::Painter::from_target(&t);
    painter.put(0,0,3);
    assert_eq!(pixels,[0;16],"Painter gate missing");
    assert!(painter.row(0).is_none());
    // Offscreen storage remains valid even when display attachment fails.
    gstate::st().surfaces[15].kind = gstate::SurfaceKind::Offscreen {
        surf: core::ptr::NonNull::<ffi::GfxSurface>::dangling().as_ptr()
    };
    let off=clip::Target {idx:15,ox:0,oy:0,offscreen:true,clip:Rect::new(0,0,2,2)};
    let writes=OFFSCREEN_WRITES.load(SeqCst);
    draw::Painter::from_target(&off).put(0,0,3);
    assert_eq!(OFFSCREEN_WRITES.load(SeqCst),writes+1,"offscreen remains drawable");

    RC.store(-22,SeqCst);
    assert_eq!(os32gui_shlib_init(&mut api),0,"attach failure must not reject bind");
    let detach_before=DETACHES.load(SeqCst);
    table_detach();
    assert_eq!(DETACHES.load(SeqCst),detach_before+1,"shlib detach body missing");

    assert!(unsafe { SHLIB_INIT_OK });
    let checks=CHECKS.load(SeqCst);
    assert_eq!(input_api::wait_key(),65);
    assert_eq!(CHECKS.load(SeqCst),checks+1,"input return check missing");
    for k in [-1, 0] {
        POLL_RESULT.store(k,SeqCst);
        assert_eq!(input_api::try_key(),None);
        assert_eq!(CHECKS.load(SeqCst),checks+1,"empty poll skips gfx check");
    }
    POLL_RESULT.store(66,SeqCst);
    assert_eq!(input_api::try_key(),Some(66));
    assert_eq!(CHECKS.load(SeqCst),checks+2,"input return check missing");
    let detached=DETACHES.load(SeqCst);
    assert_eq!(client::wait(1).unwrap_or(-999),7,"wait result survives attach failure");
    assert_eq!(DETACHES.load(SeqCst),detached+1,"failed shlib not detached");
    WAIT_RESULT.store(-11,SeqCst);
    assert_eq!(client::wait(1).unwrap_err().0,-11,"original wait error preserved");
    WAIT_RESULT.store(7,SeqCst);
    assert_eq!(draw::screen_info().width,0);
    RC.store(0,SeqCst);
    assert_eq!(client::wait(1).unwrap_or(-999),7);
    draw::fill_rect(surf,Rect::new(0,0,4,2),Style::new(0,5));
    assert_eq!(&pixels[..8],&[5;8],"failed to recover");
    // Geometry consumers must refresh before Painter is ever constructed.
    HEIGHT.store(4,SeqCst);gstate::st().screen_valid=false;
    assert_eq!(surface::surface_size(surf),(4,4),"first surface_size is stale");
    HEIGHT.store(2,SeqCst);gstate::st().screen_valid=false;
    surface::screen_surface(); // cache the smaller bounds
    HEIGHT.store(4,SeqCst);gstate::st().screen_valid=false;
    clip::set_base_clip(surf,Rect::new(0,0,4,4));
    draw::fill_rect(surf,Rect::new(0,0,4,4),Style::new(0,3));
    assert_eq!(pixels,[3;16],"base clip keeps old geometry");
    os32api::gfx::present();assert_eq!(PRESENTS.load(SeqCst),1);
    RC.store(-22,SeqCst);os32api::gfx::present();assert_eq!(PRESENTS.load(SeqCst),1);
    let detached=DETACHES.load(SeqCst);
    assert_eq!(os32api::gfx::check(),-22);
    assert_eq!(DETACHES.load(SeqCst),detached+1);
    os32api::gfx::shutdown();
    assert_eq!(DETACHES.load(SeqCst),detached+2);
}

mod app_api {
    use super::*;
    pub static STATIC_RC: AtomicI32=AtomicI32::new(0);
    pub static SHLIB_RC: AtomicI32=AtomicI32::new(0);
    pub static STATIC_CHECKS: AtomicUsize=AtomicUsize::new(0);
    pub static SHLIB_CHECKS: AtomicUsize=AtomicUsize::new(0);
    pub static SHLIB_DETACHED: AtomicUsize=AtomicUsize::new(0);
    pub static DETACHED: AtomicUsize=AtomicUsize::new(0);
    pub mod gfx {
        use super::*;
        pub fn check()->i32 {STATIC_CHECKS.fetch_add(1,SeqCst);STATIC_RC.load(SeqCst)}
        pub fn detach() {DETACHED.fetch_add(1,SeqCst);}
    }
    pub mod gui { pub mod stub {
        use super::super::*;
        pub fn detach_gfx() {SHLIB_DETACHED.fetch_add(1,SeqCst);}
        pub fn check_gfx()->i32 {SHLIB_CHECKS.fetch_add(1,SeqCst);SHLIB_RC.load(SeqCst)}
    }}
}
// The actual gdi_test check_both_gfx function is appended by the runner.
#[test]
fn gdi_stops_both_renderers() {
    use app_api::*;
    for (a,b) in [(0,0),(-22,0),(0,-22),(-22,-22)] {
        STATIC_RC.store(a,SeqCst);SHLIB_RC.store(b,SeqCst);
        let mut draws=0;
        if check_both_gfx() { draws+=2; }
        assert_eq!(draws,if a==0 && b==0 {2} else {0},"gdi must stop both renderers");
    }
    assert_eq!(STATIC_CHECKS.load(SeqCst),4,"gdi static check missing");
    assert_eq!(SHLIB_CHECKS.load(SeqCst),4,"gdi shlib check missing");
    assert_eq!(DETACHED.load(SeqCst),3,"gdi static rollback missing");
    assert_eq!(SHLIB_DETACHED.load(SeqCst),3,"gdi shlib rollback missing");
}

// Only the existing window registry boundary is mocked; client/scroll are real.
mod uistate {
    use super::*;
    #[derive(Clone, Copy)]
    pub struct Win { pub used: bool, pub id: u32, pub client: Rect }
    // This fixture has no queued widget Paint damage.
    #[derive(Clone, Copy)]
    pub struct PaintReq { pub window:u32, pub rect:Rect }
    pub struct Damage { pub items:[PaintReq;0], pub len:usize }
    impl Damage { pub fn push(&mut self,_:u32,_:Rect) { panic!("no pending damage in reattach fixture"); } }
    pub struct State { pub windows: [Win; 3], pub damage:Damage }
    static mut STATE: State = State { damage:Damage {items:[],len:0}, windows: [Win { used: false, id: 0, client: Rect::new(0,0,0,0) }; 3] };
    pub fn s() -> &'static mut State { unsafe { &mut *core::ptr::addr_of_mut!(STATE) } }
}
static TRACE: std::sync::Mutex<Vec<i32>> = std::sync::Mutex::new(Vec::new());
static REGEN: AtomicI32 = AtomicI32::new(0);
static FLUSH_RC: AtomicI32 = AtomicI32::new(0);
static ALLOW_U2_FILLS: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
static PENDING_FILLS: AtomicUsize = AtomicUsize::new(0);
static FLUSHED_FILLS: AtomicUsize = AtomicUsize::new(0);
static SCROLL_RC: AtomicI32 = AtomicI32::new(0);
#[no_mangle] extern "C" fn gfx_flush() -> i32 {
    TRACE.lock().unwrap().push(0);
    let rc = FLUSH_RC.load(SeqCst);
    if rc >= 0 { FLUSHED_FILLS.fetch_add(PENDING_FILLS.swap(0,SeqCst),SeqCst); }
    rc
}
#[no_mangle] extern "C" fn libos32gfx_take_regen() -> i32 {
    TRACE.lock().unwrap().push(2);
    REGEN.swap(0,SeqCst)
}
unsafe extern "C" fn gui_request(op: u32, _: u32) -> i32 {
    use os32api::gui::proto::*;
    TRACE.lock().unwrap().push(op as i32 + 100);
    if op == GUI_OP_INVALIDATE {
        let req = core::ptr::read_unaligned(client::slot_base().add(GUI_SLOT_REQ_OFF) as *const GuiReqInvalidate);
        INVALIDATED.lock().unwrap().push((req.window, Rect::new(req.rect.x, req.rect.y, req.rect.w, req.rect.h)));
    }
    if op == GUI_OP_SCROLL {
        assert_eq!(PENDING_FILLS.load(SeqCst),0,"SCROLL must follow pending FILLs");
        SCROLL_RC.load(SeqCst)
    } else { 0 }
}
static INVALIDATED: std::sync::Mutex<Vec<(u32, Rect)>> = std::sync::Mutex::new(Vec::new());
#[test]
fn u2_flush_scroll_and_poll_recovery() {
    use os32api::gui::proto::*;
    let mut api = os32api::mock_api();
    api.gui_call = gui_request;
    os32api::os32_init(&mut api);
    let mut shm = [0u8; GUI_SLOT_SIZE];
    unsafe { client::host_base(shm.as_mut_ptr()); }
    RC.store(0,SeqCst);
    FLUSH_RC.store(0,SeqCst);
    TRACE.lock().unwrap().clear();
    assert!(client::commit(7).is_ok());
    assert_eq!(*TRACE.lock().unwrap(), vec![0, GUI_OP_COMMIT as i32+100], "COMMIT must follow flush");
    let rect = Rect::new(2,3,40,30);
    TRACE.lock().unwrap().clear();
    ALLOW_U2_FILLS.store(true,SeqCst);
    for _ in 0..3 { unsafe { ffi::gfx_cmd_enqueue_fill(2,3,4,5,6,0) }; }
    assert!(scroll::scroll(7,rect,8,-2).is_ok());
    ALLOW_U2_FILLS.store(false,SeqCst);
    assert_eq!(FLUSHED_FILLS.load(SeqCst),3);
    assert_eq!(*TRACE.lock().unwrap(), vec![0, GUI_OP_SCROLL as i32+100]);
    let req = unsafe { core::ptr::read_unaligned(shm.as_ptr().add(GUI_SLOT_REQ_OFF) as *const GuiReqScroll) };
    assert_eq!((req.window, req.rect.x, req.rect.y, req.rect.w, req.rect.h, req.dx, req.dy), (7,2,3,40,30,8,-2));
    FLUSH_RC.store(-11,SeqCst);
    TRACE.lock().unwrap().clear(); INVALIDATED.lock().unwrap().clear();
    assert!(scroll::scroll(7,rect,8,0) == Err(client::GuiErr::STALE), "failed flush must invalidate");
    assert_eq!(*TRACE.lock().unwrap(), vec![0, GUI_OP_INVALIDATE as i32+100], "failed flush must invalidate");
    assert!(*INVALIDATED.lock().unwrap() == vec![(7,rect)]);
    FLUSH_RC.store(0,SeqCst);
    SCROLL_RC.store(OS32_ERR_NOSYS,SeqCst);
    TRACE.lock().unwrap().clear();
    assert!(scroll::scroll(7,rect,8,0).is_ok());
    assert_eq!(*TRACE.lock().unwrap(), vec![0, GUI_OP_SCROLL as i32+100, GUI_OP_INVALIDATE as i32+100]);
    SCROLL_RC.store(OS32_ERR_STALE,SeqCst);
    assert!(scroll::scroll(7,rect,8,0) == Err(client::GuiErr::STALE));
    for (i,id) in [(0,7),(2,9)] {
        uistate::s().windows[i] = uistate::Win { used:true, id, client:Rect::new(12,15,40,30) };
    }
    REGEN.store(1,SeqCst);
    TRACE.lock().unwrap().clear(); INVALIDATED.lock().unwrap().clear();
    assert!(client::poll(&mut []).is_ok());
    assert!(*INVALIDATED.lock().unwrap() == vec![(7,Rect::new(0,0,40,30)),(9,Rect::new(0,0,40,30))], "regen must invalidate every own window");
    assert_eq!(*TRACE.lock().unwrap(), vec![0,GUI_OP_POLL as i32+100,1,2,GUI_OP_INVALIDATE as i32+100,GUI_OP_INVALIDATE as i32+100]);
    INVALIDATED.lock().unwrap().clear();
    assert!(client::poll(&mut []).is_ok());
    assert!(INVALIDATED.lock().unwrap().is_empty(), "regen consumed once");
    REGEN.store(1,SeqCst); RC.store(-11,SeqCst);
    TRACE.lock().unwrap().clear();
    assert!(client::poll(&mut []).is_ok());
    assert_eq!(REGEN.load(SeqCst),1,"failed check must retain regen");
    assert!(INVALIDATED.lock().unwrap().is_empty());
    RC.store(0,SeqCst);
    TRACE.lock().unwrap().clear();
    assert!(client::wait(1).is_ok());
    assert_eq!(*TRACE.lock().unwrap(), vec![0,GUI_OP_WAIT as i32+100,1,2,GUI_OP_INVALIDATE as i32+100,GUI_OP_INVALIDATE as i32+100], "WAIT must follow flush");
    assert_eq!(INVALIDATED.lock().unwrap().len(),2);
    uistate::s().windows.iter_mut().for_each(|w| w.used=false);
    unsafe { client::host_base(core::ptr::null_mut()); }
}
