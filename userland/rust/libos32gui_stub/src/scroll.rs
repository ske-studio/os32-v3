//! Window-local scrolling through the shared library.
use crate::{client::{GuiResult, ok0}, shcall};
use os32api::gui::{stub as sh, types::Rect};

pub fn scroll(window: u32, rect: Rect, dx: i16, dy: i16) -> GuiResult<()> {
    ok0(shcall!(sh::E_SCROLL, extern "C" fn(u32, Rect, i16, i16) -> i32,
        window, rect, dx, dy))
}
