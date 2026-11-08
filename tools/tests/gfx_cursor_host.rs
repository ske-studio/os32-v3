extern crate self as os32api;
use std::sync::Mutex;
static EVENTS: Mutex<Vec<(i32,i32,i32,i32,i32,i32)>> = Mutex::new(Vec::new());
#[no_mangle] static mut gfx_direct: i32 = 0;
#[no_mangle] extern "C" fn gfx_cmd_get(x:i32,y:i32,w:i32,h:i32,d:*mut u8,p:u32)->i32 {
    EVENTS.lock().unwrap().push((2,x,y,w,h,p as i32));
    for i in 0..160 { unsafe { *d.add(i) = (i + 17) as u8; } } 0
}
#[no_mangle] extern "C" fn gfx_cmd_enqueue_mono(x:i32,y:i32,w:i32,h:i32,b:*const u8,p:u32,c:u8,bg:i32,cx:i32,cy:i32,cw:i32,ch:i32)->i32 {
    assert_eq!((w,h,p,bg,cx,cy,cw,ch),(10,16,2,-1,x,y,w,h));
    let mut events=EVENTS.lock().unwrap();
    events.push((3,x,y,w,h,c as i32));
    for row in 0..h { for col in 0..w {
        if unsafe { *b.add((row as u32*p+col as u32/8) as usize) } & (0x80 >> (col%8)) != 0 {
            events.push((5,x+col,y+row,1,1,c as i32));
        }
    }} 0
}
#[no_mangle] extern "C" fn gfx_cmd_enqueue_put(x:i32,y:i32,w:i32,h:i32,b:*const u8,p:u32,key:i32)->i32 {
    assert_eq!(key,-1);
    let mut events=EVENTS.lock().unwrap();
    events.push((4,x,y,w,h,p as i32));
    for row in 0..h { for col in 0..w {
        events.push((6,x+col,y+row,1,1,unsafe { *b.add((row as u32*p+col as u32) as usize) } as i32));
    }} 0
}
mod ffi { pub unsafe fn gfx_get_pixel(x:i32,y:i32)->u8 { super::EVENTS.lock().unwrap().push((0,x,y,0,0,0)); ((y-4)*10+x-3+17) as u8 } }
pub mod gfx { pub unsafe fn gfx_pixel(x:i32,y:i32,c:u8) { super::EVENTS.lock().unwrap().push((1,x,y,1,1,c as i32)); } }
pub mod gui { pub mod proto { pub const GUI_COLOR_TEXT:u8=1; pub const GUI_COLOR_WINDOW:u8=2; } }
mod fullscreen { pub fn active()->bool { false } }
mod wm {
    use super::cursor::Cursor;
    pub struct GuiState { pub cursor:Cursor,pub screen_w:i32,pub screen_h:i32 }
    #[derive(Clone,Copy)] pub struct Rect;
    impl Rect { pub fn new(_:i32,_:i32,_:i32,_:i32)->Self { Self } pub fn intersects(&self,_:&Self)->bool { true } }
    pub fn queue_present(_: &mut GuiState,_:Rect) {} pub fn flush_present() {}
}
#[path="CURSOR_PATH"] mod cursor;
#[test] fn cursor_paths() {
    let mut st=wm::GuiState { cursor:cursor::Cursor::EMPTY, screen_w:100,screen_h:100 };
    st.cursor.x=3; st.cursor.y=4;
    unsafe { gfx_direct=0; } cursor::show(&mut st); cursor::hide(&mut st);
    let shadow=std::mem::take(&mut *EVENTS.lock().unwrap());
    assert_eq!(shadow.iter().filter(|e|e.0==0).count(),160);
    let pixels:Vec<_>=shadow.iter().filter(|e|e.0==1).map(|e| (e.1,e.2,e.3,e.4,e.5)).collect();
    assert!(pixels.len()>100);
    unsafe { gfx_direct=1; } cursor::show(&mut st); cursor::hide(&mut st);
    let direct=std::mem::take(&mut *EVENTS.lock().unwrap());
    assert_eq!(direct[0],(2,3,4,10,16,10));
    assert_eq!(direct.iter().filter(|e|e.0==3).count(),2);
    assert_eq!(direct.iter().filter(|e|e.0==4).count(),1);
    assert_eq!(*direct.iter().find(|e|e.0==4).unwrap(),(4,3,4,10,16,10));
    let mut shape:Vec<_>=direct.iter().filter(|e|e.0==5).map(|e| (e.1,e.2,e.3,e.4,e.5)).collect();
    let half=pixels.len()/2;
    let mut expected=pixels[..half].to_vec();
    shape.sort(); expected.sort(); assert_eq!(shape,expected);
    let restored:Vec<_>=direct.iter().filter(|e|e.0==6).collect();
    assert_eq!(restored.len(),160);
    for (i,e) in restored.iter().enumerate() { assert_eq!(e.5,(i+17) as u8 as i32); }
}
