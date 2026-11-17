use super::*;
use crate::mocks;
use std::sync::{Mutex, atomic::Ordering::SeqCst};
#[derive(Default)]
struct Host { notice: Vec<u8>, output: Vec<u8>, opens: Vec<String>, result: bool, calls: Vec<u32>, switched_output: Option<Vec<u8>>, switched_pending: bool }
static HOST: Mutex<Option<Host>> = Mutex::new(None);
fn host<T>(f: impl FnOnce(&mut Host) -> T) -> T { f(HOST.lock().unwrap().as_mut().unwrap()) }
unsafe extern "C" fn boot(p: *mut u8) { core::ptr::write_bytes(p, 0xab, 16); }
unsafe extern "C" fn close(_: i32) -> i32 { 0 }
unsafe extern "C" fn open(p: *const u8, n: u32) -> i32 {
    host(|h| { let s = String::from_utf8_lossy(core::slice::from_raw_parts(p, n as usize)).into_owned();
        h.result = s.starts_with("GS OUT"); h.opens.push(s); }); 0
}
unsafe extern "C" fn status(_: i32, s: *mut u32, n: *mut u32) -> i32 {
    *s = 200; *n = host(|h| if h.result {0} else {16}); 0
}
unsafe extern "C" fn read(_: i32, p: *mut u8, _: u32) -> i32 {
    let b = b"gen 7 svc 65533\n"; core::ptr::copy_nonoverlapping(b.as_ptr(), p, b.len()); b.len() as i32
}
unsafe extern "C" fn write(_: i32, p: *const u8, n: u32) -> i32 {
    host(|h| h.output.extend_from_slice(core::slice::from_raw_parts(p, n as usize))); n as i32
}
unsafe extern "C" fn serial() -> u32 {
    host(|h| h.calls.push(0));
    mocks::HOST_NOTICE_SERIAL.load(SeqCst)
}
unsafe extern "C" fn notice(_: u32, service: u32, p: *mut u8) -> i32 {
    host(|h| {
        h.calls.push(service);
        if h.notice.is_empty() { return -7; }
        // host_notice_read ABI: 20-byte header followed by the payload.
        core::ptr::write_bytes(p, 0, 256);
        (p.add(8) as *mut u32).write_unaligned(7);
        (p.add(12) as *mut u16).write_unaligned(65533);
        (p.add(14) as *mut u16).write_unaligned(1);
        (p.add(16) as *mut u16).write_unaligned(h.notice.len() as u16);
        core::ptr::copy_nonoverlapping(h.notice.as_ptr(), p.add(20), h.notice.len());
        h.notice.clear(); 0
    })
}
fn setup() -> GuiState {
    mocks::init(); crate::session::clear();
    *HOST.lock().unwrap() = Some(Host::default());
    let mut api = unsafe { core::ptr::read(os32api::api()) };
    api.boot_token_get = boot; api.host_close = close;
    api.host_open = open; api.host_status = status; api.host_read = read;
    api.host_write = write; api.host_notice_read = notice;
    os32api::os32_init(Box::into_raw(Box::new(api)));
    let mut st = GuiState::NEW; open_serial(&mut st); st
}
fn open_serial(st: &mut GuiState) {
    mocks::SERIAL_INITIALIZED.store(1, SeqCst); super::open(st, 0);
    mocks::SERIAL_OUT.lock().unwrap().clear();
}
fn pump(st: &mut GuiState) { for _ in 0..10 { poll_lan(st); } }
fn lan(st: &mut GuiState, bytes: &[u8]) -> Vec<u8> {
    host(|h| { h.notice = bytes.to_vec(); h.output.clear(); });
    pump(st); host(|h| h.output.clone())
}
#[test]
fn remote_g2_same_parser_routes_and_preserves_serial_fragment() {
    let mut st = setup(); open_lan(&mut st, 0); pump(&mut st);
    assert_eq!(host(|h| h.output.clone()), b"gs:! ready v1\r\n");
    let bytes = b"\x0100000001 type a\n";
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(bytes); receive(&mut st);
    let serial = std::mem::take(&mut *mocks::SERIAL_OUT.lock().unwrap());
    assert_eq!(lan(&mut st, bytes), serial);
    assert_eq!(st.gserial.keys, 2);
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(b"\x01deadBEEF type b"); receive(&mut st);
    assert_eq!(lan(&mut st, b"\x0100000002 ping\n"), b"gs:00000002 pong tick=0\r\ngs:00000002 end 0\r\n");
    notify(&st, "cui done"); pump(&mut st);
    assert!(host(|h| h.output.ends_with(b"gs:! cui done\r\n")));
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(b"c\n"); receive(&mut st);
    assert_eq!(*mocks::SERIAL_OUT.lock().unwrap(), b"gs:deadBEEF type queued n=2\r\ngs:deadBEEF end 0\r\n");
    assert_eq!(st.gserial.keys, 4);
    notify(&st, "serial fallback");
    assert!(mocks::SERIAL_OUT.lock().unwrap().ends_with(b"gs:! serial fallback\r\n"));
    // A later serial request must not redirect a captured LAN response.
    unsafe { RESPONSE_LAN = true; }
    let source = response_lan();
    mocks::SERIAL_IN.lock().unwrap().extend_from_slice(b"\x0100000003 ping\n"); receive(&mut st);
    mocks::SERIAL_OUT.lock().unwrap().clear();
    reply_to(source, b"00000002", format_args!("end 0"));
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
}
#[test]
fn remote_g2_lan_without_uart_and_atomic_rejection() {
    let mut st = setup(); mocks::SERIAL_INITIALIZED.store(0, SeqCst); super::open(&mut st, 0);
    open_lan(&mut st, 1); pump(&mut st);
    assert!(!st.gserial.enabled && st.gserial.lan_enabled);
    assert!(lan(&mut st, b"\x0100000001 key 1d ZZ\n").ends_with(b"end 1\r\n"));
    assert_eq!(st.gserial.keys, 0);
    assert!(lan(&mut st, b"\x0100000002 type a\x1b\n").ends_with(b"end 3\r\n"));
    assert_eq!(st.gserial.keys, 0);
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
    st.now = 100; cycle(&mut st); pump(&mut st);
    assert!(host(|h| h.output.ends_with(b"gs:! wd expired\r\n")));
}

#[test]
fn remote_g3_wm_polls_without_serial_change_or_foreground() {
    use crate::{input::Ctx, wm};
    let mut st = setup();
    let mut api = unsafe { core::ptr::read(os32api::api()) };
    api.host_notice_serial = serial;
    os32api::os32_init(Box::into_raw(Box::new(api)));
    open_lan(&mut st, 0);
    // No foreground and serial stays at its initial value throughout startup.
    for _ in 0..12 { wm::wm_cycle(&mut st, Ctx::Standalone); }
    assert!(!host(|h| h.calls.is_empty()));
    assert!(host(|h| h.calls.windows(2).any(|pair| pair == [65533, 0])));
    assert!(host(|h| h.calls.iter().all(|&svc| svc == 65533 || svc == 0)));
    host(|h| { h.calls.clear(); h.notice = b"\x0100000001 ping\n".to_vec(); });
    wm::wm_cycle(&mut st, Ctx::Pump);
    assert!(host(|h| h.calls.is_empty()));
    for _ in 0..10 { wm::wm_cycle(&mut st, Ctx::Wait); }
    assert!(host(|h| h.output.ends_with(b"gs:00000001 pong tick=0\r\ngs:00000001 end 0\r\n")));
}

#[test]
fn remote_g2_maximum_wire_reaches_parser() {
    let mut st=setup(); open_lan(&mut st,0); pump(&mut st);
    let mut bytes=b"\x0100000001 type ".to_vec(); bytes.extend_from_slice(&[b'a';114]); bytes.push(b'\n');
    let out=lan(&mut st,&bytes);
    assert_eq!(out,b"gs:00000001 type queued n=114\r\ngs:00000001 end 0\r\n");
    assert_eq!(st.gserial.keys,114);
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
}
#[cfg(feature = "gf_accel_accept")]
#[test]
fn p0a_remote_g2_ops_body_uses_captured_route() {
    let mut st=setup(); crate::wm::gf_accept::pause(false);
    unsafe extern "C" fn counters(p: *mut u8) -> i32 {
        (p as *mut os32api::gui::types::Stats).write(os32api::gui::types::Stats::ZERO); 0
    }
    unsafe { (*os32api::api_ptr()).gfx_stats = counters; }
    open_lan(&mut st,0); pump(&mut st);
    assert!(lan(&mut st,b"\x0100000001 ops desktop 1\n").is_empty());
    assert!(st.ops.active);
    // Change the current response source while the asynchronous run is active.
    receive_line(&mut st,b"\x01deadbeef ping\n");
    mocks::SERIAL_OUT.lock().unwrap().clear();
    for i in 0..11 {
        crate::typical_ops::on_cycle_end(&mut st);
        mocks::set_tick((i+1)*200);
        crate::typical_ops::on_cycle_end(&mut st);
    }
    assert!(!st.ops.active); pump(&mut st);
    let out=String::from_utf8(host(|h|h.output.clone())).unwrap();
    assert!(out.contains("gs:00000001 GOP1 desktop warm 0 FAILED"),"{out}");
    assert!(out.contains("gs:00000001 GOP1 desktop 0 FAILED"),"{out}");
    assert!(out.contains("gs:00000001 GOP1 desktop p50 FAILED"),"{out}");
    assert!(out.ends_with("gs:00000001 end 0\r\n"),"{out}");
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
    // The synchronous SKIP path must use the same LAN route as well.
    let out=lan(&mut st,b"\x0100000002 ops char 1\n");
    assert_eq!(out,b"gs:00000002 GOP1 char SKIP title\r\ngs:00000002 end 0\r\n");
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
    open_serial(&mut st);
}

#[test]
fn remote_g2_startup_ready_once_per_port() {
    let mut st=setup();
    // setup consumed the serial ready. Reopen serial, then the default LAN.
    super::open(&mut st,0); open_lan(&mut st,0); pump(&mut st);
    assert_eq!(*mocks::SERIAL_OUT.lock().unwrap(),b"gs:! ready v1\r\n");
    assert_eq!(host(|h|h.output.clone()),b"gs:! ready v1\r\n");
    assert!(host(|h|h.opens.iter().any(|s|s.ends_with(" ! 0 15"))));
}
#[test]
fn remote_g2_cui_drains_before_switch_after_config_delay() {
    let mut st=setup(); open_lan(&mut st,0); pump(&mut st);
    host(|h| { h.notice=b"\x0100000001 cui\n".to_vec(); h.output.clear(); });
    poll_lan(&mut st); // Queues the real parser's cui reply/end, without sending.
    assert!(host(|h|h.output.is_empty()));
    mocks::set_file(b"GUI=1\n");
    unsafe extern "C" fn slow_write(_:i32,_:*const u8,n:u32)->i32 {
        mocks::set_tick(120); n as i32
    }
    unsafe extern "C" fn switch_after_flush(_: *const u8)->i32 {
        let pending=super::shutdown_pending(&GuiState {now:120,..GuiState::NEW});
        host(|h| { h.switched_output=Some(h.output.clone()); h.switched_pending=pending; });
        0
    }
    let mut api=unsafe {core::ptr::read(os32api::api())};
    api.sys_write=slow_write; api.sys_switch_shell=switch_after_flush;
    os32api::os32_init(Box::into_raw(Box::new(api)));
    assert!(!crate::switch_cui(&mut st));
    assert_eq!(host(|h|h.switched_output.clone()),
        Some(b"gs:00000001 cui queued\r\ngs:00000001 end 0\r\ngs:! cui done\r\n".to_vec()));
    assert!(!host(|h|h.switched_pending));
    assert!(host(|h|h.opens.iter().any(|s|s.contains(" 00000001 0 "))));
    assert!(host(|h|h.opens.last().unwrap().ends_with(" ! 0 15")));
    assert!(mocks::SERIAL_OUT.lock().unwrap().is_empty());
}
