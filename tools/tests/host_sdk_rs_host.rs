extern crate alloc;
use os32api::api;
#[path="host.rs"] mod host;
use host::*;
use std::sync::Mutex;
static STATE: Mutex<State> = Mutex::new(State { tick: 0, opens: 0, closes: 0,
    yields: 0, mode: 0, reads: 0, sent: 0, serial: 0, lists: 0, generation: 7,
    stale: false, request: String::new() });
struct State { tick: u32, opens: u32, closes: u32, yields: u32, mode: u32,
    reads: usize, sent: usize, serial: u32, lists: u32, generation: u32,
    stale: bool, request: String }
unsafe extern "C" fn tick() -> u32 { STATE.lock().unwrap().tick }
unsafe extern "C" fn yield_now() -> i32 { let mut s=STATE.lock().unwrap(); s.tick=s.tick.wrapping_add(1); s.yields+=1; 0 }
unsafe extern "C" fn open(p:*const u8,n:u32)->i32 {
    let mut s=STATE.lock().unwrap(); s.opens+=1;
    if s.mode==1 { return -13; }
    if s.mode==2 && s.opens==1 { return -14; }
    if s.mode==10 { return -10; }
    if s.mode==11 { return -11; }
    s.request=String::from_utf8(std::slice::from_raw_parts(p,n as usize).to_vec()).unwrap();
    if s.request=="LIST" { s.lists+=1; }
    s.reads=0; 3
}
fn body(s:&State)->Vec<u8> {
    if s.request=="LIST" {
        if s.mode==9 { return b"generation 7\nabc 1 65535 builtin\n".to_vec(); }
        let mut rows = format!("generation {}\nother 1 4 plugin\nabc 1 {} builtin\n",s.generation,if s.generation==7 {23} else {51}).into_bytes();
        if s.mode==12 { rows.extend_from_slice("other 1 4 plugin\n".repeat(5000).as_bytes()); }
        return rows;
    }
    b"abcde".to_vec()
}
unsafe extern "C" fn status(h:i32,p:*mut u32,n:*mut u32)->i32 {
    assert_eq!(h,3); let s=STATE.lock().unwrap();
    if s.mode==3 { return -14; } if s.mode==4 { return -18; }
    *p=200; *n=body(&s).len() as u32; 0
}
unsafe extern "C" fn read(h:i32,p:*mut u8,cap:u32)->i32 {
    assert_eq!(h,3); let mut s=STATE.lock().unwrap();
    if s.mode==5 { return -14; } if s.mode==6 { return -11; }
    let b=body(&s); let n=(b.len()-s.reads).min(cap as usize).min(if s.mode==12 {1400} else {2});
    std::ptr::copy_nonoverlapping(b[s.reads..].as_ptr(),p,n); s.reads+=n; n as i32
}
unsafe extern "C" fn write(h:i32,_p:*const u8,n:u32)->i32 {
    assert_eq!(h,3); assert!(n<=1400); let mut s=STATE.lock().unwrap();
    if s.mode==7 { return -14; } let n=n.min(2); s.sent+=n as usize; n as i32
}
unsafe extern "C" fn close(h:i32)->i32 { assert_eq!(h,3); let mut s=STATE.lock().unwrap(); s.closes+=1; if s.mode==8 {-1} else {0} }
unsafe extern "C" fn serial()->u32 { STATE.lock().unwrap().serial }
unsafe extern "C" fn notice(g:u32,id:u32,p:*mut u8)->i32 {
    let mut s=STATE.lock().unwrap();
    if s.stale { s.stale=false; s.generation=8; return -11; }
    if g!=s.generation || id!=if g==7 {23} else {51} { return -1; }
    *p=99; -14
}
fn setup(mode:u32) {
    let mut s=STATE.lock().unwrap(); s.tick=0; s.opens=0;s.closes=0;s.yields=0;s.sent=0;s.mode=mode;
}
#[test]
fn contracts() {
    let mut a=os32api::mock_api(); a.get_tick=tick; a.sys_yield=yield_now;
    a.host_open=open;a.host_status=status;a.host_read=read;a.host_write=write;
    a.host_close=close;a.host_notice_serial=serial;a.host_notice_read=notice;
    os32api::os32_init(&mut a);
    let mut out=[0;256];
    setup(2); assert_eq!(host_call("x GET",b"123456",&mut out,1000),5);
    assert_eq!(&out[..5],b"abcde");
    {let s=STATE.lock().unwrap();assert_eq!(s.sent,6);assert_eq!(s.closes,1);assert_eq!(s.yields,1);}
    setup(0); assert_eq!(host_call("x",&[],&mut out[..4],1000),-4);
    assert_eq!(STATE.lock().unwrap().closes,1);
    for mode in [1,3,5,7] {
        setup(mode); assert_eq!(host_call("x",b"xx",&mut out,20),-19);
        assert_eq!(STATE.lock().unwrap().closes,if mode==1 {0} else {1});
        setup(mode); assert_eq!(host_call("x",b"xx",&mut out,0),if mode==1 {-13} else {-14});
        assert_eq!(STATE.lock().unwrap().yields,0);
        setup(mode); assert_eq!(host_call("x",b"xx",&mut out,2000),-18);
    }
    setup(1); STATE.lock().unwrap().tick=u32::MAX-1;
    assert_eq!(host_call("x",&[],&mut out,30),-19);
    for (mode,error) in [(4,-18),(6,-11),(8,-1),(10,-10),(11,-18)] {
        setup(mode);assert_eq!(host_call("x",&[],&mut out,1000),error);
    }
    setup(0);assert_eq!(host_call("",&[],&mut out,1000),-9);
    assert_eq!(STATE.lock().unwrap().opens,0);
    assert_eq!(notice_read("abc",&mut out),-14);assert_eq!(out[0],99);
    assert_eq!(notice_read("abc",&mut out),-14);assert_eq!(STATE.lock().unwrap().lists,1);
    assert_eq!(notice_read("missing",&mut out),-2);
    STATE.lock().unwrap().stale=true;
    assert_eq!(notice_read("abc",&mut out),-14);assert_eq!(STATE.lock().unwrap().lists,2);
    STATE.lock().unwrap().serial+=1;
    assert_eq!(notice_read("abc",&mut out),-14);assert_eq!(STATE.lock().unwrap().lists,3);
    STATE.lock().unwrap().serial+=1;STATE.lock().unwrap().mode=9;
    assert_eq!(notice_read("abc",&mut out),-1);
    { let mut s=STATE.lock().unwrap();s.serial+=1;s.mode=12; }
    let before=STATE.lock().unwrap().lists;
    assert_eq!(os32_host_notice_read("abc",&mut out),-14);
    assert_eq!(STATE.lock().unwrap().lists,before+2);
    setup(0);
    macro_rules! request {
        ($expected:expr, $call:expr) => {{
            assert_eq!($call,5);
            assert_eq!(STATE.lock().unwrap().request,$expected);
        }};
    }
    request!("os32.time GET",os32_host_time(&mut out,1000));
    request!("os32.http GET url",os32_host_get("url",&mut out,1000));
    request!("os32.clip GET",os32_host_clip_get(&mut out,1000));
    request!("os32.clip PUT 2",os32_host_clip_put(b"xx",&mut out,1000));
    request!("os32.print OPEN name text",os32_host_print_open("name",&mut out,1000));
    request!("os32.print DATA 3 2",os32_host_print_data(3,b"xx",&mut out,1000));
    request!("os32.print CLOSE 3",os32_host_print_close(3,&mut out,1000));
}
