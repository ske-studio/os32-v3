use core::ffi::c_void;
use os32api::gui::{GuiEvent, GUI_EV_TRIM, GUI_OP_TRIM_DONE, GUI_RING_CAPACITY};
type GuiResult<T> = Result<T,()>;
struct AppVTable;
struct Ui { timeout_ticks: u32 }
struct VApp { vt: *const AppVTable, this: *mut c_void }
impl VApp {
    fn on_overflow(&self, _: &mut Ui, _: u32) {}
    fn after_commit(&self, _: &mut Ui) { unsafe { LOG.push("after_commit"); } }
}
struct State {
    trim_batch: bool, trim_epoch: u32, hook: Option<extern "C" fn()->u32>,
    trim_events: u32, trim_done_sent: u32, hook_calls: u32, hook_pages: u32,
    input_unknown: bool, quit: bool,
}
static mut STATE: State = State { trim_batch:false, trim_epoch:0, hook:None,
    trim_events:0, trim_done_sent:0, hook_calls:0, hook_pages:0,
    input_unknown:false, quit:false };
fn s()-> &'static mut State { unsafe { &mut STATE } }
static mut LOG: Vec<&str> = Vec::new();
static mut HANDLER: bool = false;
static mut ROUND: usize = 0;
mod client {
    use super::*;
    pub struct GuiErr;
    impl GuiErr { pub const INVAL: ()=(); }
    pub struct Poll { pub count: usize, pub overflow:bool, pub dropped:u32 }
    pub fn enter_handler() { unsafe { HANDLER=true; } }
    pub fn leave_handler() { unsafe { HANDLER=false; } }
    pub fn dbg_print_num(_: &[u8], _:i32) {}
    pub fn poll(buf: &mut [GuiEvent])->GuiResult<Poll> {
        unsafe {
            ROUND+=1;
            let epoch=if ROUND==1 { 51u32 } else { 77u32 };
            for i in 0..2 { buf[i]=GuiEvent {kind:GUI_EV_TRIM, sub:0, serial:0, window:0,payload:[0;8]};
                buf[i].payload[..4].copy_from_slice(&(epoch+i as u32).to_le_bytes()); }
            buf[2].kind=1;
            Ok(Poll {count:3,overflow:false,dropped:0})
        }
    }
    pub fn commit(_:u32)->GuiResult<()> { unsafe { LOG.push("commit"); } Ok(()) }
    pub fn call(op:u32,epoch:u32)->GuiResult<i32> {
        assert_eq!(op,GUI_OP_TRIM_DONE);
        assert_eq!(epoch,if unsafe {ROUND}==1 {52} else {78});
        unsafe { LOG.push("done"); } Ok(0)
    }
    pub fn wait(_:u32)->GuiResult<()> { unsafe { LOG.push("wait"); } Ok(()) }
}
mod window { pub fn count()->usize {1} }
fn drain_pending_focus(_: &VApp,_:&mut Ui) {}
fn flush_damage() {}
fn paint_damaged(_: &VApp,_:&mut Ui) {}
fn dispatch(_: &VApp,_:&mut Ui,ev:&GuiEvent) {
    assert_ne!(ev.kind,GUI_EV_TRIM,"forward-trim-to-app / on_raw");
    unsafe { LOG.push("handler"); if ROUND==2 { s().quit=true; } }
}
fn runtime_check(condition:bool,label:&str) {
    if !condition { std::io::Write::write_all(&mut std::io::stdout(),label.as_bytes()).unwrap(); std::process::exit(101); }
}
fn cache_hook()->u32 {
    unsafe {
        runtime_check(!HANDLER,"trim-in-handler");
        LOG.push("hook");
        FAIL=true;
        assert!(Os32Alloc.alloc(Layout::from_size_align(32,8).unwrap()).is_null());
        runtime_check(YIELDS == 0,"hook allocation must not yield");
    }
    3
}
#[test]
fn trim_batch_contract() {
    let mut api=os32api::mock_api(); api.mem_alloc=raw_alloc; api.sys_yield=yield_once;
    os32api::os32_init(&mut api);
    init_host::check(&mut api); set_trim_hook(cache_hook);
    s().hook=Some(trim_trampoline);
    let vt=AppVTable; let mut ui=Ui { timeout_ticks:0 };
    assert!(run_vt(&vt,core::ptr::null_mut(),&mut ui).is_ok());
    unsafe { assert_eq!(LOG,vec!["handler","commit","after_commit","hook","done","wait",
        "handler","commit","after_commit","hook","done"],"hook-twice-per-batch / done-before-hook / safe point"); }
    assert_eq!((s().trim_events,s().trim_done_sent,s().hook_calls,s().hook_pages),(4,2,2,6));
    assert_eq!(retry_stats(),(0,0,6));
    // Optional app hook absent still sends exactly one DONE, including quit.
    TRIM_HOOK.0.set(None); s().quit=false;
    unsafe { ROUND=1; LOG.clear(); }
    assert!(run_vt(&vt,core::ptr::null_mut(),&mut ui).is_ok());
    unsafe { assert_eq!(LOG,vec!["handler","commit","after_commit","done"]); }
}

// The fixed-address jump-table binding is the host seam; init's ordering and
// success/failure branches below come verbatim from the application stub.
mod init_host {
    use super::trim_trampoline;
    type GuiResult<T> = Result<T,GuiErr>;
    #[derive(Debug)]
    struct GuiErr(i32);
    impl GuiErr {
        const INVAL: Self=Self(-9);
        fn name(&self)->&[u8] {b"error"}
    }
    static mut RC:i32=-9;
    static mut CALLS:Vec<&str>=Vec::new();
    mod os32api {
        pub use ::os32api::{KernelAPI,os32_init};
        pub mod gui {
            pub fn retry_enable() { unsafe { crate::init_host::CALLS.push("enable"); } crate::retry_enable(); }
            pub mod stub {
                pub use ::os32api::gui::stub::{E_CLIENT_INIT,E_TRIM_HOOK_SET};
                pub fn bind() { unsafe { crate::init_host::CALLS.push("bind"); } }
            }
        }
    }
    mod client {
        pub fn dbg_print_num(_: &[u8],_:i32) {}
        pub fn dbg_print(_: &[u8]) {}
    }
    macro_rules! shcall {
        ($slot:expr, $ty:ty) => {{
            assert_eq!($slot,os32api::gui::stub::E_CLIENT_INIT);
            unsafe { CALLS.push("op_init"); RC }
        }};
        ($slot:expr, $ty:ty, $hook:expr) => {{
            assert_eq!($slot,os32api::gui::stub::E_TRIM_HOOK_SET);
            assert_eq!($slot,120);
            unsafe { CALLS.push("register"); }
            crate::s().hook=$hook;
        }};
    }
    // INSERT_PRODUCTION_INIT
    pub fn check(api:*mut ::os32api::KernelAPI) {
        assert!(init(core::ptr::null_mut()).is_err());
        unsafe { assert!(CALLS.is_empty()); }
        assert!(init(api).is_err());
        assert!(!crate::GUI_RETRY.0.get(),"failed OP_INIT does not enable retry");
        unsafe { assert_eq!(CALLS,vec!["bind","op_init"]); CALLS.clear(); RC=7; }
        assert_eq!(init(api).unwrap(),7);
        assert!(crate::GUI_RETRY.0.get());
        unsafe { assert_eq!(CALLS,vec!["bind","op_init","enable","register"]); }
        assert!(crate::s().hook.is_some());
    }
}
