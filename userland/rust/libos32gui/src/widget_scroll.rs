//! Widget scrolling. Display snapshots stay in the existing widget/row storage.
use super::*;

fn damage(idx: usize, rect: Rect) {
    s().damage.push(s().widgets[idx].window, rect);
}

fn row_damage(idx: usize, row: i32, height: i16, cap: i16) {
    if row >= 0 && row < cap as i32 {
        let r = s().widgets[idx].rect;
        damage(idx, Rect::new(r.x + 1, r.y + 1 + row as i16 * height, r.w - 2, height));
    }
}

/// Positive rows advance the view (pixels move upward). Never touch CLIENT RAM.
fn copy_rows(idx: usize, rows: i32, height: i16, cap: i16) -> bool {
    let w = s().widgets[idx];
    let area = Rect::new(w.rect.x + 1, w.rect.y + 1, w.rect.w - 2, cap * height);
    if rows == 0 || rows <= -(cap as i32) || rows >= cap as i32 || area.is_empty() {
        return false;
    }
    let dy = -(rows * height as i32) as i16;
    if crate::scroll::scroll(w.window, area, 0, dy).is_err() {
        return false;
    }
    // Pending widget damage may not have reached Paint yet. Its copied pixels
    // need repainting at their destination as well as at the original position.
    let pending = s().damage.items;
    let n = s().damage.len;
    for p in &pending[..n] {
        if p.window == w.window {
            let r = p.rect.intersect(&area);
            if !r.is_empty() {
                damage(idx, Rect::new(r.x, r.y + dy, r.w, r.h).intersect(&area));
            }
        }
    }
    let exposed = if dy < 0 {
        Rect::new(area.x, (area.bottom() + dy as i32) as i16, area.w, -dy)
    } else {
        Rect::new(area.x, area.y, area.w, dy)
    };
    damage(idx, exposed);
    // The dotted frame is fixed to the widget. Repair its old and copied
    // horizontal strokes, and its stationary rectangle. Row heights are even,
    // so translated vertical dots retain their phase.
    let focused = s().windows.iter().any(|win| win.used && win.id == w.window
        && win.focused && win.focus == idx as u16 + 1);
    if focused {
        let ring = Rect::new(w.rect.x + 2, w.rect.y + 2, w.rect.w - 4, w.rect.h - 4);
        damage(idx, ring);
        for y in [ring.y, (ring.bottom() - 1) as i16] {
            damage(idx, Rect::new(area.x, y + dy, area.w, 1).intersect(&area));
        }
    }
    true
}

pub(super) fn list_damage(idx: usize, old_top: i16, old_sel: i16) {
    let w = s().widgets[idx];
    if unsafe { crate::ffi::gfx_direct } == 0 || old_top == w.top
        || !copy_rows(idx, w.top as i32 - old_top as i32, LIST_ROW_H, visible_rows(idx)) {
        invalidate(idx);
        return;
    }
    // Selection pixels from the old view have moved to these new row indices.
    for sel in [old_sel, w.sel] {
        row_damage(idx, sel as i32 - w.top as i32, LIST_ROW_H, visible_rows(idx));
    }
}

/// Replace the visible snapshot while scrolling by `rows` (positive = forward).
/// Retained rows must match before copying; edits/rewrapping fall back to Paint.
/// `lines` and caret coordinates describe the NEW view, and are copied now.
pub fn textarea_scroll(id: WidgetId, rows: i32, lines: &[&[u8]], crow: i32, ccol: i32) -> GuiResult<()> {
    let idx = resolve(id).ok_or(GuiErr::STALE)?;
    if s().widgets[idx].kind != WK_TEXTAREA {
        return Err(GuiErr::INVAL);
    }
    let cap = ta_capacity(idx);
    if lines.len() > cap as usize || lines.len() > GUI_MAX_TEXTAREA_ROWS {
        return Err(GuiErr::FULL);
    }
    let owner = idx as u16 + 1;
    let available = s().rows.iter().filter(|r| !r.used || r.owner == owner).count();
    if lines.len() > available { return Err(GuiErr::FULL); }
    let old = s().widgets[idx];
    let mut same = true;
    for row in 0..cap as i32 {
        let source = row as i64 + rows as i64;
        if source >= 0 && source < cap as i64 {
            let before = ta_row_slot(idx, source as i16);
            let after = lines.get(row as usize);
            same &= match (before, after) {
                (Some(k), Some(text)) => {
                    let n = crate::utf8core::utf8_truncate(text, GUI_TEXTAREA_ROW_CAP);
                    s().rows[k].len as usize == n && s().rows[k].text[..n] == text[..n]
                }
                (None, None) => true,
                _ => false,
            };
        }
    }
    let copied = unsafe { crate::ffi::gfx_direct } != 0 && same
        && copy_rows(idx, rows, TEXTAREA_ROW_H, cap);
    for r in &mut s().rows {
        if r.used && r.owner == owner { *r = TextRow::EMPTY; }
    }
    for (index, text) in lines.iter().enumerate() {
        let r = s().rows.iter_mut().find(|r| !r.used).unwrap();
        let n = crate::utf8core::utf8_truncate(text, GUI_TEXTAREA_ROW_CAP);
        r.used = true;
        r.owner = owner;
        r.index = index as i16;
        r.len = n as u16;
        r.text[..n].copy_from_slice(&text[..n]);
    }
    s().widgets[idx].ta_rows = lines.len() as i16;
    s().widgets[idx].ta_caret_row = crow as i16;
    s().widgets[idx].ta_caret_col = if ccol < 0 { 0 } else { ccol as i16 };
    if copied {
        if old.ta_caret_row >= 0 {
            let moved = old.ta_caret_row as i64 - rows as i64;
            if moved >= 0 && moved < cap as i64 {
                row_damage(idx, moved as i32, TEXTAREA_ROW_H, cap);
            }
        }
        row_damage(idx, crow, TEXTAREA_ROW_H, cap);
    } else {
        invalidate(idx);
    }
    send_text_cursor(idx);
    Ok(())
}
