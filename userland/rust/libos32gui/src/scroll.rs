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
    match client::call(GUI_OP_SCROLL, 0) {
        Err(e) if e == GuiErr::NOSYS => client::invalidate(window, rect),
        result => result.map(|_| ()),
    }
}
