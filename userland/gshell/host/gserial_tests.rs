use super::*;
use crate::{mocks, wm, slot, input, fullscreen, multiapp};
use std::sync::atomic::Ordering::SeqCst;
fn setup() -> GuiState {
    mocks::init(); session::clear(); fullscreen::reset(); multiapp::reset();
    mocks::SERIAL_INITIALIZED.store(1, SeqCst);
    let mut st = GuiState::NEW; open(&mut st, 0);
    mocks::SERIAL_OUT.lock().unwrap().clear(); st
}
fn wire(st: &mut GuiState, bytes: &[u8]) -> String {
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(bytes);
    receive(st);
    String::from_utf8(std::mem::take(&mut *mocks::SERIAL_OUT.lock().unwrap())).unwrap()
}
fn command(st: &mut GuiState, body: &str) -> String {
    wire(st, format!("\x01aBcD0123 {body}\n").as_bytes())
}
fn target(st: &mut GuiState, shm: &mocks::Shm) {
    st.shm_base = shm.base(); st.slots[0].used = true; st.slots[0].owner = 2;
    slot::init_header(st, 0);
    let mut w = wm::Win::EMPTY; w.used = true; w.visible = true; w.owner = 2; w.gen = 1;
    st.windows[0] = w; st.zorder[0] = 0; st.z_count = 1;
}
fn space(st: &GuiState, n: u16) {
    let mut h = slot::read_header(st, 0); h.ring_head = 0;
    h.ring_tail = os32api::gui::proto::GUI_RING_CAPACITY as u16 - n;
    slot::write_header(st, 0, &h);
}
#[test]
fn gserial_framing_resync_control_length_and_ids() {
    let mut st = setup();
    assert!(wire(&mut st, b"ping\n").is_empty());
    assert!(wire(&mut st, b"\x01aBcD0123 ping\r\n").contains("gs:aBcD0123 pong tick=0\r\ngs:aBcD0123 end 0\r\n"));
    assert!(wire(&mut st, b"\x01aBcD0123 type a\x01deadBEEF ping\n").contains("gs:deadBEEF pong"));
    assert_eq!(st.gserial.keys, 0);
    assert!(command(&mut st, "type @").contains("type queued n=1"));
    assert_eq!(st.gserial.keys, 1);
    assert!(wire(&mut st, b"\x01aBcD0123 type b\x1b\n").contains("err junk"));
    assert_eq!(st.gserial.keys, 1);
    for bytes in [b"\x01\n".as_slice(), b"\x01xxx ping\n", b"\x01zzzzzzzz ping\n", b"\x01aBcD0123 ping\rX\n", b"\x01aBcD0123 type \x80\n"] {
        assert!(wire(&mut st, bytes).contains("end 3"));
    }
    assert!(command(&mut st,"nope").contains("end 2"));
    assert!(command(&mut st,"").contains("end 1"));
    assert!(command(&mut st,&format!("type {}", "a".repeat(115))).contains("err long"));
    assert_eq!(st.gserial.keys,1);
    assert!(command(&mut st,&format!("type {}", "a".repeat(114))).contains("end 0"));
    assert_eq!(st.gserial.keys,115);
}
#[test]
fn gserial_queue_atomic_inverse_break_and_long_type() {
    let mut st = setup();
    for ch in 0x20..=0x7e { let (scan,mods) = input::ascii_key(ch).unwrap(); assert_eq!(input::translate(scan,mods),ch); }
    assert_eq!(parse(b"type a"),Cmd::Type(b"a"));
    assert_eq!(enqueue(&mut st,b"\x7f",true),Err("char"));
    assert!(command(&mut st,&format!("type {}", "a".repeat(65))).contains("n=65"));
    assert!(command(&mut st,&format!("type {}", "b".repeat(65))).contains("err full"));
    assert_eq!(st.gserial.keys,65);
    let mut rounds=0;
    while st.gserial.keys > 0 {
        deliver(&mut st); assert!(st.pending_raw_n <= 32);
        assert_eq!(st.pending_raw_n % 2,0);
        for pair in st.pending_raw[..st.pending_raw_n].chunks(2) {
            assert_eq!(pair, &[0x11d,0x1d]);
        }
        st.pending_raw_n=0; rounds+=1;
    }
    assert!(rounds>1); assert_eq!(st.gserial.sent,65);
    enqueue(&mut st,b"a",true).unwrap();
    assert!(command(&mut st,"flush").contains("flush n=1"));
    assert_eq!((st.gserial.keys,st.gserial.raws),(0,0));
}
#[test]
fn gserial_pending_reservation_and_modifier_expansion_preserve_queue() {
    let mut st=setup(); let shm=mocks::Shm::new(); target(&mut st,&shm);
    enqueue(&mut st,b"a",true).unwrap();
    st.pending_raw[..2].copy_from_slice(&[0x11d,0x1d]); st.pending_raw_n=2;
    space(&st,9); deliver(&mut st); assert_eq!((st.gserial.keys,st.pending_raw_n),(1,2));
    space(&st,11); deliver(&mut st); assert_eq!((st.gserial.keys,st.pending_raw_n),(0,4));
    st.z_count=0; st.pending_raw_n=30;
    enqueue(&mut st,b"1d+g",false).unwrap(); deliver(&mut st);
    assert_eq!((st.gserial.keys,st.pending_raw_n),(1,30));
    st.pending_raw_n=28; deliver(&mut st);
    assert_eq!((st.gserial.keys,st.pending_raw_n),(0,32));
    assert_eq!(&st.pending_raw[28..32],&[raw(SC_GRPH,true,8) as i32, raw(0x1d,true,8) as i32,raw(0x1d,false,8) as i32,raw(SC_GRPH,false,0) as i32]);
    assert_eq!(expand(0x1d,12).1,6);
}
#[test]
fn gserial_ctrl_escape_follows_local_input_and_pump_does_not_read() {
    let mut st=setup();
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(b"\x01aBcD0123 key 00+c\n");
    wm::wm_cycle(&mut st,input::Ctx::Pump); assert_eq!(st.gserial.keys,0);
    receive(&mut st); deliver(&mut st);
    assert_eq!(&st.pending_raw[..2],&[0x2100,0x2000]);
    input::capture(&mut st,input::Ctx::Standalone); assert!(startmenu::is_open());
    mocks::push_rawkeys(&[0x100,0]); input::capture(&mut st,input::Ctx::Standalone);
    assert!(!startmenu::is_open());
}
#[test]
fn gserial_one_line_per_cycle() {
    let mut st=setup();
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(b"\x01aaaaaaaa ping\n\x01bbbbbbbb state\n");
    cycle(&mut st);
    let out=String::from_utf8(mocks::SERIAL_OUT.lock().unwrap().clone()).unwrap();
    assert!(out.contains("gs:aaaaaaaa end 0")); assert!(!out.contains("bbbbbbbb"));
    cycle(&mut st);
    assert!(String::from_utf8(mocks::SERIAL_OUT.lock().unwrap().clone()).unwrap().contains("gs:bbbbbbbb end 0"));
}
#[test]
fn gserial_watchdog_refresh_expiry_disable_wrap_busy_retry() {
    let mut st=setup(); st.gserial.arm(10,2);
    st.now=209; cycle(&mut st); assert_eq!(session::pending_action(),0);
    assert!(command(&mut st,"ping").contains("end 0"));
    st.now=210; cycle(&mut st); assert_eq!(session::pending_action(),0);
    st.now=409; cycle(&mut st); assert_eq!(session::pending_action(),GUI_SESSION_SWITCH_CUI);
    assert_eq!(String::from_utf8(mocks::SERIAL_OUT.lock().unwrap().clone()).unwrap(),"gs:! wd expired\r\n");
    session::clear(); cycle(&mut st); assert_eq!(session::pending_action(),GUI_SESSION_SWITCH_CUI);
    assert_eq!(mocks::SERIAL_OUT.lock().unwrap().len(),b"gs:! wd expired\r\n".len());
    session::clear(); command(&mut st,"wd 0"); st.now=1000; cycle(&mut st); assert_eq!(session::pending_action(),0);
    st.gserial.arm(u32::MAX-50,1); st.now=48; cycle(&mut st); assert_eq!(session::pending_action(),0);
    st.now=49; cycle(&mut st); assert_eq!(session::pending_action(),GUI_SESSION_SWITCH_CUI);
    session::clear(); st.gserial.arm(0,1);
    session::set_wm(&mut st, os32api::gui::proto::GUI_SESSION_SHUTDOWN,b"");
    st.now=100; cycle(&mut st); assert_eq!(session::pending_action(),os32api::gui::proto::GUI_SESSION_SHUTDOWN);
    session::clear(); cycle(&mut st); assert_eq!(session::pending_action(),GUI_SESSION_SWITCH_CUI);
    session::clear();
}
#[test]
fn gserial_lost_counts_raw_and_rejected_fullscreen_injection() {
    let mut st=setup(); let shm=mocks::Shm::new(); target(&mut st,&shm);
    space(&st,0); st.pending_raw[..2].copy_from_slice(&[0x11d,0x1d]); st.pending_raw_n=2;
    input::capture_keyboard(&mut st,input::Ctx::Standalone);
    assert_eq!(st.inj_lost,2); assert!(command(&mut st,"state").contains("lost=2"));
    st.z_count=0; fullscreen::arm(&[0;48]); mocks::set_screen_owner(3); fullscreen::observe();
    mocks::INJECT_LIMIT.store(0,SeqCst); mocks::push_rawkeys(&[0x11d]);
    input::capture_keyboard(&mut st,input::Ctx::Standalone);
    assert_eq!(st.inj_lost,3); assert!(command(&mut st,"state").contains("lost=3"));
    fullscreen::reset();
}
#[test]
fn gserial_cui_two_stage_cfg_failure_and_success_restore_rshell() {
    let mut st=setup();
    assert_eq!(*mocks::RSHELL_ACTIVE.lock().unwrap(),vec![0]);
    assert_eq!(command(&mut st,"cui"),"gs:aBcD0123 cui queued\r\ngs:aBcD0123 end 0\r\n");
    assert_eq!(session::pending_action(),GUI_SESSION_SWITCH_CUI);
    assert!(command(&mut st,"cui").contains("err busy"));
    let shm=mocks::Shm::new(); target(&mut st,&shm);
    assert!(!session::ready_to_run(&st));
    st.windows[0].used=false; st.slots[0].used=false; st.z_count=0;
    assert!(session::ready_to_run(&st));
    // sys_open's empty-file default fails the write; GUI remains active.
    assert!(crate::switch_cui(&mut st));
    let out=String::from_utf8(std::mem::take(&mut *mocks::SERIAL_OUT.lock().unwrap())).unwrap();
    assert!(out.contains("gs:! cui failed cfg\r\n"));
    assert_eq!(*mocks::RSHELL_ACTIVE.lock().unwrap(),vec![0]);
    assert!(mocks::SWITCHES.lock().unwrap().is_empty());
    // Close the failure notification modal before successful CUI handoff.
    modal::on_key(&mut st,0x1c,13,0);
    mocks::set_file(b"GUI=1\nGSERIAL_TIMEOUT=0\n");
    unsafe extern "C" fn write(_fd:i32,_s:*const u8,n:u32)->i32 { n as i32 }
    let mut api=unsafe { core::ptr::read(os32api::api()) }; api.sys_write=write;
    os32api::os32_init(Box::into_raw(Box::new(api)));
    command(&mut st,"cui");
    assert!(!crate::switch_cui(&mut st));
    let state=command(&mut st,"state");
    let out=String::from_utf8(mocks::SERIAL_OUT.lock().unwrap().clone()).unwrap();
    // command() collects the notification queued immediately before state.
    assert!(state.starts_with("gs:! cui done\r\n"),"{state} {out}");
    assert!(state.contains("gs:aBcD0123 state ") && state.ends_with("gs:aBcD0123 end 0\r\n"));
    assert_eq!(*mocks::RSHELL_ACTIVE.lock().unwrap(),vec![0,1]);
    assert_eq!(*mocks::SWITCHES.lock().unwrap(),vec![b"/sys/shell.bin".to_vec()]);
}
#[test]
fn gserial_config_default_disabled_and_exact_response_prefixes() {
    mocks::init(); assert_eq!(load_timeout(),0);
    for (file,seconds) in [(b"GUI=1\nGSERIAL_TIMEOUT=20\r\n".as_slice(),20),
                           (b"GSERIAL_TIMEOUT=0\n",0),(b" GSERIAL_TIMEOUT = 15 \r\n",15),(b"GSERIAL_TIMEOUT=-1\n",0),
                           (b"GSERIAL_TIMEOUT=21474837\n",0)] {
        mocks::set_file(file); assert_eq!(load_timeout(),seconds);
    }
    let mut st=GuiState::NEW; open(&mut st,20);
    assert!(!st.gserial.enabled && mocks::RSHELL_ACTIVE.lock().unwrap().is_empty());
    mocks::SERIAL_INITIALIZED.store(1,SeqCst); open(&mut st,20);
    assert_eq!(*mocks::SERIAL_OUT.lock().unwrap(),b"gs:! ready v1\r\n");
    mocks::SERIAL_OUT.lock().unwrap().clear();
    for cmd in ["ping","state","dump","flush","wd 0","key 00+c","type @","bad"] {
        let out=command(&mut st,cmd);
        assert!(out.lines().all(|line| line.starts_with("gs:aBcD0123 ")));
        assert!(out.lines().last().unwrap().starts_with("gs:aBcD0123 end "));
        assert!(out.lines().all(|line| line.len()+2<=200));
        assert!(!out.as_bytes().contains(&4));
    }
}
#[test]
fn gserial_sixty_five_characters_reach_application_text_events() {
    use os32api::gui::proto::{GuiEvent,GUI_RING_CAPACITY,GUI_EV_TEXT};
    let mut st=setup(); let shm=mocks::Shm::new(); target(&mut st,&shm);
    enqueue(&mut st,&[b'a';65],true).unwrap();
    let mut text=Vec::new(); let mut rounds=0;
    while st.gserial.keys>0 {
        wm::wm_cycle(&mut st,input::Ctx::Wait); rounds+=1;
        let mut h=slot::read_header(&st,0);
        for i in h.ring_head..h.ring_tail {
            let ev:GuiEvent=unsafe { core::ptr::read_unaligned(slot::ring_ptr(&st,0).add(i as usize % GUI_RING_CAPACITY *16) as *const GuiEvent) };
            if ev.kind==GUI_EV_TEXT { text.push(ev.payload[0]); }
        }
        h.ring_head=h.ring_tail; slot::write_header(&st,0,&h);
    }
    assert_eq!(text,vec![b'a';65]); assert!(rounds>1); assert_eq!(st.inj_lost,0);
}

#[test]
fn gserial_watchdog_cfg_failure_disarms_next_cycle() {
    let mut st = setup();
    st.gserial.arm(0, 1);
    st.now = 100;
    cycle(&mut st);
    assert_eq!(session::pending_action(), GUI_SESSION_SWITCH_CUI);
    assert!(crate::switch_cui(&mut st));
    assert_eq!(session::pending_action(), 0);
    session::clear();
    cycle(&mut st);
    assert_eq!(session::pending_action(), 0);
    assert!(String::from_utf8(mocks::SERIAL_OUT.lock().unwrap().clone()).unwrap()
        .contains("gs:! cui failed cfg\r\n"));
}
#[test]
fn gserial_disabled_sends_no_notifications_or_cui_output() {
    mocks::init(); session::clear();
    let mut st = GuiState::NEW;
    open(&mut st, 1);
    assert!(!st.gserial.enabled);
    notify(&st, "wd expired");
    cui_done(&st);
    assert!(crate::switch_cui(&mut st));
    cycle(&mut st);
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
}

#[test]
fn gserial_px_reply_maximum_and_unavailable_backends() {
    use os32api::gui::types::{GFX_FMT_PLANAR4, GFX_CAP_HW_FILL, GFX_CAP_HW_BLT};
    static INFO: std::sync::Mutex<ScreenInfo> = std::sync::Mutex::new(ScreenInfo::ZERO);
    unsafe extern "C" fn screen(out: *mut u8) {
        *(out as *mut ScreenInfo) = *INFO.lock().unwrap();
    }
    unsafe extern "C" fn stats(out: *mut u8) -> i32 {
        *(out as *mut Stats) = Stats { present_bytes: u32::MAX, commits: u32::MAX, ..Stats::ZERO };
        0
    }
    let mut st = setup();
    let mut api = unsafe { core::ptr::read(os32api::api()) };
    api.gfx_screen_info = screen; api.gfx_stats = stats;
    os32api::os32_init(Box::into_raw(Box::new(api)));
    let pegc = ScreenInfo { width: 640, height: 480, bpp: 8, format: GFX_FMT_PACKED8,
                            flags: GFX_CAP_TEXT_OVERLAY, ..ScreenInfo::ZERO };
    *INFO.lock().unwrap() = pegc;
    mocks::gfx_pixel(OS32_GUI_DIAG_P1_X, OS32_GUI_DIAG_P1_Y, 0xff);
    mocks::gfx_pixel(OS32_GUI_DIAG_P2_X, OS32_GUI_DIAG_P2_Y, 0xff);
    let output = command(&mut st, "px");
    assert_eq!(output, "gs:aBcD0123 px ff,ff pb=4294967295 cm=4294967295\r\ngs:aBcD0123 end 0\r\n");
    assert_eq!(output.lines().next().unwrap().len(), 48);
    assert!(output.lines().all(|line| line.len() <= 198));
    mocks::gfx_pixel(OS32_GUI_DIAG_P1_X, OS32_GUI_DIAG_P1_Y, 0x12);
    mocks::gfx_pixel(OS32_GUI_DIAG_P2_X, OS32_GUI_DIAG_P2_Y, 0x34);
    assert!(command(&mut st, "px").contains("px 12,34 pb="));
    for info in [ScreenInfo { height: 400, ..pegc },
                 ScreenInfo { bpp: 4, format: GFX_FMT_PLANAR4, ..pegc },
                 ScreenInfo { flags: GFX_CAP_HW_FILL | GFX_CAP_HW_BLT, ..pegc }] {
        *INFO.lock().unwrap() = info;
        assert_eq!(command(&mut st, "px"), "gs:aBcD0123 px na\r\ngs:aBcD0123 end 0\r\n");
    }
    *INFO.lock().unwrap() = pegc;
    unsafe { mocks::gfx_ready = 0; }
    assert_eq!(command(&mut st, "px"), "gs:aBcD0123 px na\r\ngs:aBcD0123 end 0\r\n");
    unsafe { mocks::gfx_ready = 1; }
    assert!(command(&mut st, "px extra").contains("end 1"));
    st.screen_w = 640; st.screen_h = 480;
    startmenu::toggle_start(&mut st);
    assert!(startmenu::rect().contains(OS32_GUI_DIAG_P1_X, OS32_GUI_DIAG_P1_Y));
    assert!(!startmenu::rect().contains(OS32_GUI_DIAG_P2_X, OS32_GUI_DIAG_P2_Y));
    assert!(!crate::taskbar::rect(&st).contains(OS32_GUI_DIAG_P1_X, OS32_GUI_DIAG_P1_Y));
    startmenu::toggle_start(&mut st);
}
