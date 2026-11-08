// Exercise the actual stub bodies against the actual shared-library exports.
macro_rules! shcall {
    ($idx:expr, $ty:ty $(, $arg:expr)* $(,)?) => {{
        let ptr = match $idx {
            122 => crate::os32gui_scroll as *const (),
            123 => crate::os32gui_w_textarea_scroll as *const (),
            _ => panic!("unexpected scroll entry"),
        };
        let function: $ty = unsafe { core::mem::transmute(ptr) };
        function($($arg),*)
    }};
}
extern crate self as libos32gui;
use os32api::gui::types::{Rect, Style, SurfaceId};
use widget::WidgetId;
use doc::VRow;
const W: usize = 180;
const H: usize = 140;
static mut PIXELS: [u8; W*H] = [0; W*H];
static mut CLIP: Rect = Rect::new(0,0,W as i16,H as i16);
static mut SCROLL_RC: bool = true;
static mut SCROLLS: usize = 0;
mod ffi { pub static mut gfx_direct: i32 = 0; }
mod client {
    #[derive(Debug, Clone, Copy, PartialEq, Eq)] pub struct GuiErr(pub i32);
    impl GuiErr { pub const STALE: Self=Self(-1); pub const INVAL: Self=Self(-2); pub const FULL: Self=Self(-3); }
    pub type GuiResult<T> = Result<T,GuiErr>;
    pub fn ok0(r:i32)->GuiResult<()> {if r<0 {Err(GuiErr(r))} else {Ok(())}}
    pub fn invalidate(_:u32,_:super::Rect)->GuiResult<()> { Ok(()) }
    pub use crate::utf8core::{utf8_truncate, utf8_seq_len};
    pub fn win_set_text_cursor(_:u32,_:i16,_:i16,_:bool)->GuiResult<()> { Ok(()) }
}
// Only the WM COPY boundary is faked. Widget drawing and state are real.
pub mod scroll {
    use super::*;
    pub fn scroll(_:u32, r:Rect, dx:i16, dy:i16)->client::GuiResult<()> {
        unsafe {
            SCROLLS += 1;
            if !SCROLL_RC { return Err(client::GuiErr::STALE); }
            let before = PIXELS;
            for y in r.y as i32..r.bottom() { for x in r.x as i32..r.right() {
                let (sx,sy)=(x-dx as i32,y-dy as i32);
                if r.contains(sx,sy) { PIXELS[y as usize*W+x as usize]=before[sy as usize*W+sx as usize]; }
            }}
        }
        Ok(())
    }
}
// Deterministic cells isolate row/background/frame/caret placement from fonts.
mod draw {
    use super::*;
    fn put(x:i32,y:i32,c:u8) { unsafe {
        if CLIP.contains(x,y) && x>=0 && y>=0 && x<W as i32 && y<H as i32 { PIXELS[y as usize*W+x as usize]=c; }
    }}
    pub fn fill_rect(_:SurfaceId,r:Rect,s:Style) {
        for y in r.y as i32..r.bottom() { for x in r.x as i32..r.right() { put(x,y,s.bg); }}
    }
    pub fn hline(_:SurfaceId,x:i32,y:i32,w:i32,s:Style) {
        for i in 0..w { if !s.has(os32api::gui::proto::GUI_STYLE_DOTTED) || i%2==0 { put(x+i,y,s.fg); } }
    }
    pub fn vline(_:SurfaceId,x:i32,y:i32,h:i32,s:Style) {
        for i in 0..h { if !s.has(os32api::gui::proto::GUI_STYLE_DOTTED) || i%2==0 { put(x,y+i,s.fg); } }
    }
    pub fn draw_rect(sf:SurfaceId,r:Rect,s:Style) {
        hline(sf,r.x as i32,r.y as i32,r.w as i32,s);hline(sf,r.x as i32,r.bottom()-1,r.w as i32,s);
        vline(sf,r.x as i32,r.y as i32,r.h as i32,s);vline(sf,r.right()-1,r.y as i32,r.h as i32,s);
    }
    pub fn line(_:SurfaceId,_:i32,_:i32,_:i32,_:i32,_:Style) {}
    pub fn measure_text(t:&[u8])->(i32,i32) { (t.len() as i32*8,16) }
    pub fn text(sf:SurfaceId,x:i32,y:i32,t:&[u8],s:Style)->i32 {
        for (i,b) in t.iter().enumerate() {
            let r=Rect::new((x+i as i32*8) as i16,y as i16,8,16);
            fill_rect(sf,r,s);
            for yy in 1..15 { for xx in 1..7 { if (xx+yy+*b as i32)%3==0 { put(x+i as i32*8+xx,y+yy,s.fg); } }}
        }
        t.len() as i32*8
    }
}
fn reset() { unsafe {
    *uistate::s() = uistate::UiState::NEW;
    PIXELS.fill(42); CLIP=Rect::new(0,0,W as i16,H as i16); SCROLL_RC=true; SCROLLS=0;
}}
fn setup(kind:u8, rows:i16, focused:bool)->WidgetId {
    let id=if kind==uistate::WK_LISTBOX {widget::listbox().unwrap()} else {widget::textarea().unwrap()};
    let i=widget::resolve(id).unwrap();
    let height=if kind==uistate::WK_LISTBOX {18} else {16};
    uistate::s().widgets[i].window=7;
    uistate::s().widgets[i].rect=Rect::new(7,9,100,rows*height+7);
    let win=&mut uistate::s().windows[0];win.used=true;win.id=7;win.focus=i as u16+1;win.focused=focused;
    id
}
fn paint(id:WidgetId, damage_only:bool, focused:bool) {
    let i=widget::resolve(id).unwrap();
    let areas=if damage_only { uistate::s().damage.items[..uistate::s().damage.len].iter().map(|p|p.rect).collect::<Vec<_>>() }
        else { vec![widget::rect(id)] };
    for r in areas { unsafe { CLIP=r; } widget::draw_tree(SurfaceId::NULL,i as u16+1,i as u16+1,focused,r); }
    uistate::s().damage.clear();
}
fn check_pixels(id:WidgetId, focused:bool, label:&str) {
    paint(id,true,focused);
    let got=unsafe {PIXELS};
    paint(id,false,focused);
    assert!(got==unsafe {PIXELS},"{label}");
}
#[test] fn list_pixels() {
    for focused in [false,true] { for fail in [false,true] { unsafe {
        reset(); let id=setup(uistate::WK_LISTBOX,4,focused);
        ffi::gfx_direct=0;
        for t in [b"aa",b"bb",b"cc",b"dd",b"ee",b"ff",b"gg",b"hh"] {widget::list_add(id,t).unwrap();}
        widget::list_set_selection(id,2).unwrap();paint(id,false,focused);
        uistate::s().damage.clear();
        ffi::gfx_direct=1;SCROLL_RC=!fail;
        // Both old and new selections remain in the new view.
        widget::list_set_selection(id,4).unwrap();
        check_pixels(id,focused,"list pixels");
        widget::list_set_selection(id,0).unwrap();check_pixels(id,focused,"list pixels upward");
        ffi::gfx_direct=0;let calls=SCROLLS;
        widget::list_set_selection(id,5).unwrap();check_pixels(id,focused,"list SHADOW pixels");assert_eq!(SCROLLS,calls);
    }}}
}
#[test] fn textarea_pixels() {
    for focused in [false,true] { for fail in [false,true] { unsafe {
        reset();let id=setup(uistate::WK_TEXTAREA,5,focused);ffi::gfx_direct=0;
        for t in [b"aa",b"bb",b"cc",b"dd",b"ee"] {widget::textarea_add_row(id,t).unwrap();}
        widget::textarea_set_caret(id,2,1).unwrap();paint(id,false,focused);uistate::s().damage.clear();
        ffi::gfx_direct=1;SCROLL_RC=!fail;
        widget::textarea_scroll(id,1,&[b"bb",b"cc",b"dd",b"ee",b"ff"],1,1).unwrap();
        check_pixels(id,focused,"textarea pixels");
        widget::textarea_scroll(id,-1,&[b"aa",b"bb",b"cc",b"dd",b"ee"],2,1).unwrap();
        check_pixels(id,focused,"textarea pixels backward");
        widget::textarea_scroll(id,1,&[b"XX",b"cc",b"dd",b"ee",b"ff"],1,1).unwrap();
        check_pixels(id,focused,"textarea pixels edited");
        ffi::gfx_direct=0;let calls=SCROLLS;
        widget::textarea_scroll(id,1,&[b"cc",b"dd",b"ee",b"ff"],-1,0).unwrap();
        check_pixels(id,focused,"textarea SHADOW pixels");assert_eq!(SCROLLS,calls);
    }}}
}
static mut DOC: doc::Doc=doc::Doc::NEW;
fn doc()->&'static mut doc::Doc { unsafe { &mut *core::ptr::addr_of_mut!(DOC) } }
struct Editor { area:WidgetId, top:usize }
impl Editor {fn update_status(&mut self) {}}
#[test] fn editor_pixels() { unsafe {
    reset();let id=setup(uistate::WK_TEXTAREA,5,false);
    doc().reset();doc().insert(b"aa\nbb\ncc\ndd\nee\nff\ngg");doc().cy=0;doc().cx=1;
    let mut e=Editor {area:id,top:0};ffi::gfx_direct=0;e.refresh();paint(id,false,false);uistate::s().damage.clear();
    ffi::gfx_direct=1;doc().cy=5;e.refresh();assert!(SCROLLS>0,"editor must scroll");check_pixels(id,false,"editor pixels");
    SCROLL_RC=false;doc().cy=6;e.refresh();check_pixels(id,false,"editor pixels failure");
}}
const ROW_H:i32=18;const ROWS_Y0:i32=18;
struct Filer {right_pad:WidgetId,list_top:usize,list_sel:usize,dirty:std::cell::RefCell<Vec<Rect>>}
impl Filer {
    fn win_id(&self)->u32 {7}
    fn repaint(&self,r:Rect) {if !r.is_empty(){self.dirty.borrow_mut().push(r);}}
    fn repaint_path(&self) {}
    fn render(&self,r:Rect) { unsafe {CLIP=r;}
        let pad=widget::rect(self.right_pad);
        draw::fill_rect(SurfaceId::NULL,pad,Style::new(0,15));
        for row in 0..self.visible_rows(&pad) {
            let index=self.list_top+row;
            let y=pad.y+ROWS_Y0 as i16+row as i16*ROW_H as i16;
            let style=if index==self.list_sel {Style::new(15,2)} else {Style::new(0,15)};
            draw::fill_rect(SurfaceId::NULL,Rect::new(pad.x+1,y,pad.w-2,18),style);
            draw::text(SurfaceId::NULL,pad.x as i32+4,y as i32+1,&[b'a'+index as u8],style);
        }
    }
}
#[test] fn filer_pixels() { unsafe {
    for fail in [false,true] {
        reset();let id=setup(uistate::WK_LISTBOX,5,false);
        let mut f=Filer {right_pad:id,list_top:0,list_sel:2,dirty:Default::default()};
        let pad=widget::rect(id);f.render(pad);SCROLL_RC=!fail;
        f.list_top=1;f.list_sel=4;f.repaint_sel(2,0);
        for r in f.dirty.borrow().iter() {f.render(*r);}
        let got=PIXELS;f.render(pad);assert!(got==PIXELS,"filer pixels");
        assert_eq!(SCROLLS,1,"filer must scroll");
    }
}}

#[test] fn shared_library_scroll_entries() { unsafe {
    reset();let id=setup(uistate::WK_TEXTAREA,5,false);ffi::gfx_direct=0;
    for t in [b"aa",b"bb",b"cc",b"dd",b"ee"] {widget::textarea_add_row(id,t).unwrap();}
    paint(id,false,false);uistate::s().damage.clear();ffi::gfx_direct=1;
    stub_widget::textarea_scroll(id,1,&[b"bb",b"cc",b"dd",b"ee",b"ff"],-1,0).unwrap();
    assert_eq!(SCROLLS,1);check_pixels(id,false,"shared textarea pixels");
    assert!(stub_scroll::scroll(7,Rect::new(8,10,90,70),0,-16).is_ok());
    assert_eq!(SCROLLS,2);
    ffi::gfx_direct=0;
    assert!(stub_scroll::scroll(7,Rect::new(8,10,90,70),0,-16).is_err());
    assert_eq!(SCROLLS,2,"SHADOW must use repaint");
}}

#[test] fn textarea_only_exposed_band() { unsafe {
    reset();let id=setup(uistate::WK_TEXTAREA,5,false);ffi::gfx_direct=0;
    for t in [b"aa",b"bb",b"cc",b"dd",b"ee"] {widget::textarea_add_row(id,t).unwrap();}
    paint(id,false,false);uistate::s().damage.clear();ffi::gfx_direct=1;
    widget::textarea_scroll(id,1,&[b"bb",b"cc",b"dd",b"ee",b"ff"],-1,0).unwrap();
    assert_eq!(uistate::s().damage.len,1,"only exposed band");
    let r=uistate::s().damage.items[0].rect;
    assert!(r==Rect::new(8,74,98,16),"only exposed band");
    check_pixels(id,false,"textarea pixels exposed only");
    // A second move before Paint must preserve the first move's damage.
    widget::textarea_scroll(id,1,&[b"cc",b"dd",b"ee",b"ff",b"gg"],-1,0).unwrap();
    widget::textarea_scroll(id,1,&[b"dd",b"ee",b"ff",b"gg",b"hh"],-1,0).unwrap();
    check_pixels(id,false,"textarea pixels pending damage");
}}
