use super::*;
use std::sync::Mutex;
#[derive(Default)]
struct Mock {
    opens: Vec<String>, output: Vec<u8>, response: Vec<u8>,
    open_rc: i32, status_rc: i32, read_pos: usize, write_limit: usize,
    write_calls: usize, closes: usize, notice: Option<(u32,u16,u16,Vec<u8>)>,
    notice_rc: i32, reads: Vec<(u32,u32)>,
}
static MOCK: Mutex<Option<Mock>> = Mutex::new(None);
fn with<T>(f: impl FnOnce(&mut Mock)->T)->T { f(MOCK.lock().unwrap().as_mut().unwrap()) }
unsafe extern "C" fn boot(p:*mut u8) { core::ptr::copy_nonoverlapping([0xab;16].as_ptr(),p,16); }
unsafe extern "C" fn open(p:*const u8,n:u32)->i32 {
    with(|m| { m.opens.push(String::from_utf8_lossy(core::slice::from_raw_parts(p,n as usize)).into_owned()); m.read_pos=0; m.open_rc })
}
unsafe extern "C" fn status(_:i32,s:*mut u32,n:*mut u32)->i32 {
    with(|m| { *s=200; *n=m.response.len() as u32; m.status_rc })
}
unsafe extern "C" fn read(_:i32,p:*mut u8,cap:u32)->i32 {
    with(|m| { let n=(cap as usize).min(3).min(m.response.len()-m.read_pos);
        core::ptr::copy_nonoverlapping(m.response[m.read_pos..].as_ptr(),p,n); m.read_pos+=n; n as i32 })
}
unsafe extern "C" fn write(_:i32,p:*const u8,n:u32)->i32 {
    with(|m| { m.write_calls+=1; let n=(n as usize).min(m.write_limit);
        if n==0 { return OS32_ERR_AGAIN; }
        m.output.extend_from_slice(core::slice::from_raw_parts(p,n)); n as i32 })
}
unsafe extern "C" fn close(_:i32)->i32 { with(|m|m.closes+=1); 0 }
unsafe extern "C" fn notice(g:u32,s:u32,p:*mut u8)->i32 {
    with(|m| { m.reads.push((g,s));
        if m.notice_rc!=0 { return m.notice_rc; }
        let Some((generation,service,kind,data))=m.notice.take() else { return OS32_ERR_AGAIN; };
        let mut n=Notice {serial:1,dropped:0,generation,service,kind,length:data.len() as u16,reserved:0,data:[0;236]};
        n.data[..data.len()].copy_from_slice(&data); (p as *mut Notice).write(n); 0 })
}
fn setup(now:u32)->Link {
    *MOCK.lock().unwrap()=Some(Mock {response:b"gen 7 svc 65533\n".to_vec(),write_limit:1400,..Mock::default()});
    let mut a=os32api::mock_api();
    a.boot_token_get=boot; a.host_open=open; a.host_status=status; a.host_read=read;
    a.host_write=write; a.host_close=close; a.host_notice_read=notice;
    os32api::os32_init(Box::into_raw(Box::new(a))); Link::new(now)
}
fn poll(l:&mut Link,t:u32)->Vec<Vec<u8>> { let mut lines=Vec::new(); l.poll(t,|b|lines.push(b.to_vec())); lines }
fn ready(l:&mut Link,t:u32) { for i in 0..20 { poll(l,t+i); if l.ready { break; } } assert!(l.ready); }
fn inject(g:u32,s:u16,k:u16,b:&[u8]) { with(|m|m.notice=Some((g,s,k,b.to_vec()))); }
fn command(l:&mut Link,t:u32) {
    inject(7,65533,1,b"\x0100000001 ping\n"); assert_eq!(poll(l,t),vec![b"\x0100000001 ping\n".to_vec()]);
}
#[test]
fn ready_retry_generation_and_isolation() {
    let mut l=setup(u32::MAX-100);
    with(|m|m.open_rc=OS32_ERR_AGAIN);
    poll(&mut l,u32::MAX-100); poll(&mut l,1898);
    assert_eq!(with(|m|m.opens.len()),1);
    with(|m|m.open_rc=0); ready(&mut l,1899);
    assert_eq!(with(|m|m.opens[0].clone()),format!("RSH READY {} gui 1 none idle","ab".repeat(16)));
    inject(7,65534,1,b"\x0100000001 ping\n"); assert!(poll(&mut l,1910).is_empty());
    assert_eq!(with(|m|m.reads.last().copied()),Some((7,65533)));
    inject(8,65533,1,b"\x0100000001 ping\n"); assert!(poll(&mut l,1911).is_empty());
    command(&mut l,1912);
    inject(7,65533,1,b"\x0100000002 ping\n"); assert!(poll(&mut l,1913).is_empty());
    with(|m|m.notice_rc=OS32_ERR_AGAIN); poll(&mut l,1914);
    poll(&mut l,3913); assert_eq!(with(|m|m.opens.len()),2);
    with(|m|m.notice_rc=0); ready(&mut l,3914);
    assert!(with(|m|m.opens.last().unwrap().ends_with("00000001 running")));
    inject(7,65533,1,b"\x0100000001 ping\n"); assert!(poll(&mut l,3930).is_empty());
    with(|m|m.notice_rc=OS32_ERR_STALE); poll(&mut l,3940);
    with(|m| {m.notice_rc=0;m.response=b"gen 8 svc 65533\n".to_vec();}); ready(&mut l,5940);
    assert!(l.last.is_none());
    inject(8,65533,1,b"\x0100000001 ping\n"); assert_eq!(poll(&mut l,5961).len(),1);
}
#[test]
fn gs_out_end_length_partial_and_async() {
    let mut l=setup(0); ready(&mut l,0); command(&mut l,10);
    l.emit(b"00000001",b"gs:00000001 pong\r\n",11);
    poll(&mut l,12); assert_eq!(with(|m|m.opens.len()),1);
    l.emit(b"00000001",b"gs:00000002 end 0\n",13);
    poll(&mut l,14); assert_eq!(with(|m|m.opens.len()),1);
    l.emit(b"00000001",b"gs:00000001 end ",15);
    poll(&mut l,16); assert_eq!(with(|m|m.opens.len()),1);
    l.emit(b"00000001",b"0\r\n",17);
    l.emit(b"!",b"gs:! cui done\r\n",17);
    with(|m| {m.response.clear();m.write_limit=5;});
    for t in 18..70 { let before=with(|m|m.write_calls); poll(&mut l,t); assert!(with(|m|m.write_calls)-before<=1); }
    let output=with(|m|m.output.clone());
    let opens=with(|m|m.opens.clone()); assert_eq!(opens.len(),3);
    let parts:Vec<_>=opens[1].split_whitespace().collect();
    let len:usize=parts.last().unwrap().parse().unwrap();
    assert_eq!(opens[1],format!("GS OUT {} 00000001 0 {}","ab".repeat(16),len));
    assert_eq!(output.len(),len+b"gs:! cui done\r\n".len());
    assert!(output.ends_with(b"gs:! cui done\r\n"));
    assert_eq!(opens[2],format!("GS OUT {} ! 0 15","ab".repeat(16)));
    assert!(l.done && !l.pending());
    inject(7,65533,1,b"\x0100000001 ping\n"); assert!(poll(&mut l,70).is_empty());
}
#[test]
fn overflow_and_cooperative_deadlines() {
    let mut l=setup(0); ready(&mut l,0); command(&mut l,10);
    l.emit(b"00000001",&[b'x';FIFO+1],11);
    assert_eq!(l.drop,1); assert!(!l.frozen); assert_eq!(l.len,FIFO);
    l.emit(b"!",b"gs:! cui done\r\n",12);
    l.emit(b"!",b"gs:! wd expired\r\n",13);
    with(|m| {m.response.clear();m.write_limit=0;});
    poll(&mut l,14); assert!(with(|m|m.opens.last().unwrap().contains(" ! 17 15")));
    poll(&mut l,15); assert!(l.shutdown_pending(1013));
    poll(&mut l,1014); assert!(!l.shutdown_pending(1014)); assert!(with(|m|m.output.is_empty()));
    let mut l=setup(0); ready(&mut l,0);
    l.emit(b"!",b"gs:! cui done\r\n",10); assert!(!l.shutdown_pending(5010));
}
#[test]
fn wrong_ready_service_is_rejected() {
    let mut l=setup(0); with(|m|m.response=b"gen 7 svc 65534\n".to_vec());
    for t in 0..10 {poll(&mut l,t);} assert!(!l.ready);
    poll(&mut l,1999); assert_eq!(with(|m|m.opens.len()),1);
}
#[test]
fn maximum_fifo_chunk_bound_and_inflight_async_overflow() {
    let mut l=setup(0); ready(&mut l,0); command(&mut l,10);
    let end=b"\ngs:00000001 end 0\r\n";
    l.emit(b"00000001",&vec![b'x';FIFO-end.len()],11);
    l.emit(b"00000001",end,12);
    assert!(l.frozen); assert_eq!(l.len,FIFO);
    with(|m|m.response.clear()); poll(&mut l,13);
    assert!(with(|m|m.opens.last().unwrap().ends_with(" 0 8192")));
    for t in 14..30 {
        let before=with(|m|m.output.len()); poll(&mut l,t);
        assert!(with(|m|m.output.len())-before<=LINK_MAX_PAYLOAD as usize);
    }
    assert_eq!(with(|m|m.output.len()),FIFO); assert!(l.done);
    l.emit(b"!",b"gs:! cui done\r\n",31); poll(&mut l,32);
    l.emit(b"!",b"gs:! wd expired\r\n",33);
    assert_eq!(l.h,-1); assert_eq!(l.notice_len,0); assert_eq!(l.notice_drop,17);
    l.emit(b"!",b"gs:! ready v1\r\n",34); poll(&mut l,35);
    assert!(with(|m|m.opens.last().unwrap().contains(" ! 17 ")));
}
#[test]
fn full_open_status_again_and_total_deadline() {
    let mut l=setup(0); with(|m|m.open_rc=OS32_ERR_STALE);
    poll(&mut l,0); poll(&mut l,1999); assert_eq!(with(|m|m.opens.len()),1);
    with(|m|m.open_rc=0); ready(&mut l,2000); command(&mut l,2010);
    l.emit(b"00000001",b"gs:00000001 end 0\n",2011);
    with(|m|m.open_rc=OS32_ERR_FULL); poll(&mut l,2012);
    assert!(l.frozen && !l.ready);
    with(|m|m.open_rc=0); ready(&mut l,4012);
    with(|m| {m.response.clear();m.status_rc=OS32_ERR_AGAIN;m.write_limit=1;});
    poll(&mut l,4022);
    for i in 1..7 { poll(&mut l,4022+i*900); }
    assert_eq!(l.h,-1); assert!(!l.frozen);
    assert!(with(|m|m.output.len())<b"gs:00000001 end 0\n".len());
}

#[test]
fn startup_ready_notification_survives_first_handshake() {
    let mut l=setup(0); l.emit(b"!",b"gs:! ready v1\r\n",0);
    ready(&mut l,0); assert_eq!(l.notice_len,15);
    with(|m|m.response.clear());
    for t in 10..15 { poll(&mut l,t); }
    assert_eq!(with(|m|m.output.clone()),b"gs:! ready v1\r\n");
}

#[test]
fn again_refreshes_ready_in_same_generation() {
    let mut l=setup(u32::MAX-100); ready(&mut l,u32::MAX-100);
    let due=l.retry;
    poll(&mut l,due.wrapping_sub(1)); assert_eq!(with(|m|m.opens.len()),1);
    poll(&mut l,due); ready(&mut l,due.wrapping_add(1));
    assert_eq!(with(|m|m.opens.len()),2);
    assert_eq!(l.generation,7);
    let due=l.retry;
    poll(&mut l,due); ready(&mut l,due+1);
    assert_eq!(with(|m|m.opens.len()),3);
}
#[test]
fn generation_reset_is_confirmed_before_receiving() {
    let mut l=setup(0); ready(&mut l,0); command(&mut l,10);
    inject(7,65533,2,b""); poll(&mut l,11);
    with(|m|m.response=b"gen 8 svc 65533\n".to_vec());
    poll(&mut l,2011);
    for t in 2012..2020 { poll(&mut l,t); if l.h<0 { break; } }
    assert!(!l.ready); assert!(l.last.is_none());
    let reads=with(|m|m.reads.len());
    inject(8,65533,1,b"\x0100000001 ping\n");
    poll(&mut l,2020);
    assert!(with(|m|m.opens.last().unwrap().ends_with("none idle")));
    with(|m|m.status_rc=OS32_ERR_AGAIN); poll(&mut l,2021);
    assert!(!l.ready); assert_eq!(with(|m|m.reads.len()),reads);
    with(|m|m.status_rc=0); ready(&mut l,2022);
    assert_eq!(poll(&mut l,2035).len(),1);
}
#[test]
fn maximum_wire_and_overflow() {
    let mut l=setup(0); ready(&mut l,0);
    let mut bytes=b"\x0100000001 type ".to_vec(); bytes.extend_from_slice(&[b'a';114]); bytes.push(b'\n');
    assert_eq!(bytes.len(),130);
    let mut over=bytes.clone(); over.insert(10,b'x');
    inject(7,65533,1,&over); assert!(poll(&mut l,10).is_empty());
    inject(7,65533,1,&bytes); assert_eq!(poll(&mut l,11),vec![bytes]);
}

#[test]
fn notice_drop_is_reported_once_and_inflight_losses_survive() {
    let mut l=setup(0);
    l.emit(b"!",b"gs:! ready v1\r\n",0);
    l.emit(b"!",b"gs:! ready v1\r\n",0);
    ready(&mut l,0); with(|m|m.response.clear()); poll(&mut l,10);
    assert!(with(|m|m.opens.last().unwrap().contains(" ! 15 15")));
    for t in 11..14 { poll(&mut l,t); }
    l.emit(b"!",b"gs:! cui done\r\n",14); poll(&mut l,15);
    assert!(with(|m|m.opens.last().unwrap().contains(" ! 0 15")));
    l.emit(b"!",b"gs:! wd expired\r\n",16);
    l.emit(b"!",b"gs:! ready v1\r\n",17); poll(&mut l,18);
    assert!(with(|m|m.opens.last().unwrap().contains(" ! 17 15")));
    for t in 19..22 { poll(&mut l,t); }
    l.emit(b"!",b"gs:! cui done\r\n",22); poll(&mut l,23);
    assert!(with(|m|m.opens.last().unwrap().contains(" ! 0 15")));
}
#[test]
fn first_ready_after_stale_has_no_previous_id() {
    let mut l=setup(0); ready(&mut l,0); command(&mut l,10);
    with(|m|m.notice_rc=OS32_ERR_STALE); poll(&mut l,11);
    with(|m| {m.notice_rc=0;m.response=b"gen 8 svc 65533\n".to_vec();});
    poll(&mut l,2011);
    assert!(with(|m|m.opens.last().unwrap().ends_with("none idle")));
}
