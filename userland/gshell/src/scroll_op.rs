//! GUI_OP_SCROLL: §3-6 の有効画素だけを複写し、残りを Paint に戻す。
use crate::{cursor, damage, fep, input, modal, reqs, slot, startmenu, taskbar, visible, wm};
use crate::wm::{GuiState, Rect, RectSet, MAX_VIS};

extern "C" {
    #[cfg_attr(test, link_name = "gshell_scroll_test_flush")]
    fn gfx_flush() -> i32;
    #[cfg_attr(test, link_name = "gshell_scroll_test_copy")]
    fn gfx_copy_rect(dx: i32, dy: i32, sx: i32, sy: i32, w: i32, h: i32) -> i32;
}

fn clip(set: &RectSet, rect: Rect) -> RectSet {
    let mut out = RectSet::EMPTY;
    for r in set.as_slice() { out.push(r.intersect(&rect)); }
    out
}

// y の境界で帯に分ける。高さの異なる断片同士でも複写元を壊さない。
fn copy(cp: &RectSet, org: (i32, i32), dx: i32, dy: i32) -> bool {
    let mut edges = [0; MAX_VIS * 2];
    for (i, r) in cp.as_slice().iter().enumerate() {
        edges[2*i] = r.y;
        edges[2*i+1] = r.bottom();
    }
    let n = cp.len * 2;
    edges[..n].sort_unstable();
    let mut sorted = *cp;
    sorted.rects[..sorted.len].sort_unstable_by_key(|r| r.x);
    for step in 1..n {
        let k = if dy > 0 { n-step } else { step };
        let y = edges[k-1];
        let h = edges[k]-y;
        if h == 0 { continue; }
        for j in 0..sorted.len {
            let i = if dx > 0 { sorted.len-1-j } else { j };
            let r = sorted.rects[i];
            if y >= r.y && y+h <= r.bottom() {
                if unsafe { gfx_copy_rect(org.0+r.x, org.1+y,
                    org.0+r.x-dx, org.1+y-dy, r.w, h) } < 0 { return false; }
            }
        }
    }
    true
}

pub(super) fn request(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqScroll = unsafe { slot::read_req(st, slot_no) };
    let idx = match super::owned_index(st, owner, req.window) {
        Ok(i) => i,
        Err(e) => return e,
    };
    super::op_commit(st, owner, slot_no, req.window);
    let rect = Rect::new(req.rect.x as i32, req.rect.y as i32,
                         req.rect.w as i32, req.rect.h as i32);
    scroll(st, idx, rect, req.dx as i32, req.dy as i32);
    0
}

fn scroll(st: &mut GuiState, idx: usize, rect: Rect, dx: i32, dy: i32) {
    let org = st.windows[idx].client_origin();
    let rv = clip(&st.windows[idx].vis, rect);
    if rv.is_empty() { return; }
    cursor::hide(st);
    // SHADOW でも元画素が VRAM に出てから複写する。DIRECT の失敗もここで拾う。
    wm::flush_present();
    let flushed = unsafe { gfx_flush() } >= 0;
    let (mut valid, mut complete) = visible::region_subtract_region(&rv, &st.windows[idx].dirty);
    let (next, all) = visible::region_subtract_region(&valid, &st.windows[idx].issued);
    valid = next;
    complete &= all && flushed && !st.windows[idx].vis_capped;
    let touched = rect.translate(org.0, org.1);
    let edges = input::live_frame_edges_hit(st, touched);
    for top in [modal::rect(), taskbar::rect(st), startmenu::rect(), fep::rect()]
        .into_iter().chain(edges.into_iter().flatten()) {
        let (next, all) = visible::region_subtract_rect(&valid, top.translate(-org.0, -org.1));
        valid = next;
        complete &= all;
    }
    // 画面外の元画素には保存内容がない。
    valid = clip(&valid, Rect::new(-org.0, -org.1, st.screen_w, st.screen_h));
    let mut cp = RectSet::EMPTY;
    if complete && dx.abs() < rect.w && dy.abs() < rect.h {
        for r in valid.as_slice() {
            let dst = r.translate(dx, dy).intersect(&rect);
            for vis in st.windows[idx].vis.as_slice() {
                complete &= cp.push(dst.intersect(vis));
            }
        }
    }
    if !complete { cp.clear(); }
    let (mut paint, all) = visible::region_subtract_region(&rv, &cp);
    // 容量超過は部分複写を始める前に全域再描画へ落とす。
    if !all { cp.clear(); paint = rv; }
    if !copy(&cp, org, dx, dy) { cp.clear(); paint = rv; }
    for r in cp.as_slice() { wm::queue_present(st, r.translate(org.0, org.1)); }
    for r in paint.as_slice() { damage::add_dirty(&mut st.windows[idx], *r); }
    if edges.is_some() { input::draw_live_outline(st); }
    let overlays = wm::overlays_to_refresh(st, &[touched]);
    wm::refresh_overlays(st, &overlays);
    cursor::show(st);
    wm::queue_present(st, cursor::rect(st));
    wm::flush_present();
}

#[cfg(test)]
#[path = "../host/scroll_tests.rs"]
mod tests;
