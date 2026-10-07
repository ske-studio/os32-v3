use os32api::gui::types::{Rect, ScreenInfo, Style, GFX_FMT_PACKED8};
use std::sync::atomic::{AtomicI32, AtomicUsize, Ordering::SeqCst};
static UNICODE_INITS: AtomicUsize = AtomicUsize::new(0);
#[no_mangle] extern "C" fn libos32gfx_unicode_init() { UNICODE_INITS.fetch_add(1,SeqCst); }
static CHECKS: AtomicUsize = AtomicUsize::new(0);
static DETACHES: AtomicUsize = AtomicUsize::new(0);
static RC: AtomicI32 = AtomicI32::new(0);
static WAIT_RESULT: AtomicI32 = AtomicI32::new(7);
static HEIGHT: AtomicI32 = AtomicI32::new(4);
static OFFSCREEN_WRITES: AtomicUsize = AtomicUsize::new(0);
static PRESENTS: AtomicUsize = AtomicUsize::new(0);
#[no_mangle] static mut gfx_api: *mut os32api::KernelAPI = core::ptr::null_mut();
#[no_mangle] static mut gfx_ready: i32 = 1;
#[no_mangle] static mut gfx_fb: ffi::GfxFramebuffer = ffi::GfxFramebuffer {
    width: 4, height: 4, pitch: 4, planes: [core::ptr::null_mut();4],
};
#[no_mangle] extern "C" fn libos32gfx_check() -> i32 {
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
    let unicode_before=UNICODE_INITS.load(SeqCst);
    assert_eq!(os32gui_shlib_init(&mut api),0,"attach failure must not reject bind");
    assert_eq!(UNICODE_INITS.load(SeqCst),unicode_before+1,"shlib Unicode acquisition missing");
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
    pub static DETACHED: AtomicUsize=AtomicUsize::new(0);
    pub mod gfx {
        use super::*;
        pub fn check()->i32 {STATIC_CHECKS.fetch_add(1,SeqCst);STATIC_RC.load(SeqCst)}
        pub fn detach() {DETACHED.fetch_add(1,SeqCst);}
    }
    pub mod gui { pub mod stub {
        use super::super::*;
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
}
