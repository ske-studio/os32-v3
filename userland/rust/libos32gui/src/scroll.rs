//! Client-local scrolling; only publish SCROLL after queued drawing completes.
use crate::{client, ffi};
use crate::client::{GuiErr, GuiResult};
use os32api::gui::proto::{GuiRect16, GuiReqScroll, GUI_OP_SCROLL};
use os32api::gui::types::Rect;

/// Scroll a window's local rectangle by (dx, dy). Failed flushes and old WMs
/// repaint the rectangle instead; errors retain the existing GuiResult contract.
pub fn scroll(window: u32, rect: Rect, dx: i16, dy: i16) -> GuiResult<()> {
    if unsafe { ffi::gfx_flush() } < 0 {
        let _ = client::invalidate(window, rect);
        return Err(GuiErr::STALE);
    }
    let req = GuiReqScroll {
        window,
        rect: GuiRect16 { x: rect.x, y: rect.y, w: rect.w, h: rect.h },
        dx,
        dy,
    };
    client::write_req(&req);
    let result = match client::call(GUI_OP_SCROLL, 0) {
        Err(e) if e == GuiErr::NOSYS => client::invalidate(window, rect),
        result => result.map(|_| ()),
    };
    if result.is_ok() && unsafe { ffi::gfx_direct } != 0 {
        // Pending Paint damage describes stale pixels at the source. COPY also
        // moves those pixels, so retain the source and repaint the destination.
        let pending = crate::uistate::s().damage.items;
        let n = crate::uistate::s().damage.len;
        for p in &pending[..n] {
            if p.window == window {
                let r = p.rect.intersect(&rect);
                let x0 = (r.x as i32 + dx as i32).max(rect.x as i32);
                let y0 = (r.y as i32 + dy as i32).max(rect.y as i32);
                let x1 = (r.right() + dx as i32).min(rect.right());
                let y1 = (r.bottom() + dy as i32).min(rect.bottom());
                if !r.is_empty() && x0 < x1 && y0 < y1 {
                    crate::uistate::s().damage.push(window,
                        Rect::new(x0 as i16, y0 as i16, (x1-x0) as i16, (y1-y0) as i16));
                }
            }
        }
    }
    result
}
