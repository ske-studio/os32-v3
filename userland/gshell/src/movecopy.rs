//! 窓移動の複写 (§3-5)。呼出し元の結線は W2。
//! index は解決済みの窓。座標の制限・最大化解除は呼出し元が行う。
use crate::{cursor, damage, kbdnav, visible, wm};
use crate::wm::{GuiState, Rect, RectSet, MAX_VIS};

extern "C" {
    fn gfx_flush() -> i32;
    // L6 が SHADOW の memmove と DIRECT の COPY を振り分ける。
    fn gfx_copy_rect(dx: i32, dy: i32, sx: i32, sy: i32, w: i32, h: i32) -> i32;
}

fn intersect(a: &RectSet, b: &RectSet) -> (RectSet, bool) {
    let mut out = RectSet::EMPTY;
    let mut complete = true;
    for r in &a.rects[..a.len] {
        for s in &b.rects[..b.len] {
            if !out.push(r.intersect(s)) { complete = false; }
        }
    }
    (out, complete)
}

/// y の境界で帯に分け、移動方向の後端から複写する。
/// 高さの異なる断片も、先の COPY が次の複写元を壊さない。
fn copy_valid(ok: &RectSet, old: (i32, i32), new: (i32, i32)) -> bool {
    let mut edges = [0; MAX_VIS * 2];
    for (i, r) in ok.rects[..ok.len].iter().enumerate() {
        edges[2 * i] = r.y;
        edges[2 * i + 1] = r.bottom();
    }
    let n = ok.len * 2;
    edges[..n].sort_unstable();
    let mut rects = *ok;
    rects.rects[..rects.len].sort_unstable_by_key(|r| r.x);
    for step in 1..n {
        let k = if new.1 > old.1 { n - step } else { step };
        let y = edges[k - 1];
        let h = edges[k] - y;
        if h == 0 { continue; }
        for j in 0..rects.len {
            let i = if new.0 > old.0 { rects.len - 1 - j } else { j };
            let r = rects.rects[i];
            if y >= r.y && y + h <= r.bottom() {
                if unsafe { gfx_copy_rect(new.0 + r.x, new.1 + y,
                    old.0 + r.x, old.1 + y, r.w, h) } < 0 { return false; }
            }
        }
    }
    true
}

pub fn move_window(st: &mut GuiState, index: usize, x: i32, y: i32) {
    let old_outer = st.windows[index].outer();
    let old_org = st.windows[index].client_origin();
    let old_vis = st.windows[index].vis;
    cursor::hide(st);
    // wm::drop_drag_frame と同じ終了処理。既存の公開口だけを使う。
    let frame = st.drag_frame;
    st.drag_index = -1;
    st.drag_frame = Rect::EMPTY;
    kbdnav::drop_kmove(st);
    if !frame.is_empty() {
        let t = 2;
        for edge in [Rect::new(frame.x, frame.y, frame.w, t),
            Rect::new(frame.x, frame.bottom() - t, frame.w, t),
            Rect::new(frame.x, frame.y, t, frame.h),
            Rect::new(frame.right() - t, frame.y, t, frame.h)] {
            st.dirty_screen(edge);
            for win in &mut st.windows {
                if win.used && win.visible {
                    let (ox, oy) = win.client_origin();
                    damage::add_dirty_band(win, edge.translate(-ox, -oy));
                }
            }
        }
    }
    let flushed = unsafe { gfx_flush() } >= 0;
    let mut ok = RectSet::EMPTY;
    let mut complete = flushed;
    if flushed {
        wm::flush_present();
        let (valid, a) = visible::region_subtract_region(&old_vis, &st.windows[index].dirty);
        let (valid, b) = visible::region_subtract_region(&valid, &st.windows[index].issued);
        ok = valid;
        complete &= a && b;
        for top in wm::overlay_rects(st) {
            let (next, all) = visible::region_subtract_rect(&ok, top.translate(-old_org.0, -old_org.1));
            ok = next;
            complete &= all;
        }
        // 画面外には保存された画素が無い。COPY の削りとは別の有効性判定。
        let mut screen = RectSet::EMPTY;
        screen.push(Rect::new(-old_org.0, -old_org.1, st.screen_w, st.screen_h));
        let (next, all) = intersect(&ok, &screen);
        ok = next;
        complete &= all;
    }
    st.windows[index].x = x;
    st.windows[index].y = y;
    visible::recompute_and_expose(st);
    let new_org = st.windows[index].client_origin();
    let (next, all) = intersect(&ok, &st.windows[index].vis);
    ok = next;
    complete &= all && !st.windows[index].vis_capped;
    if !complete { ok.clear(); }
    if !copy_valid(&ok, old_org, new_org) { ok.clear(); }
    wm::composite_rect(st, old_outer);
    wm::composite_rect(st, st.windows[index].outer());
    let (paint, all) = visible::region_subtract_region(&st.windows[index].vis, &ok);
    if !flushed || !all {
        damage::set_dirty_full(&mut st.windows[index]);
    } else {
        for r in &paint.rects[..paint.len] { damage::add_dirty(&mut st.windows[index], *r); }
    }
    st.dirty_screen(old_outer);
    st.dirty_screen(st.windows[index].outer());
    st.windows[index].configure_pending = true;
    cursor::show(st);
}

#[cfg(test)]
#[path = "../host/movecopy_tests.rs"]
mod tests;
