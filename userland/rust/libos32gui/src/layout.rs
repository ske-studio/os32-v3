//! layout.rs — 箱レイアウト 1 種類 (契約 U7)。票 C2 作業 6。
//!
//! `row` / `column` コンテナの子は `Fixed(px)` / `Flex(weight)` / `Absolute(rect)`。
//! `min` と `padding` (と子の間隔 `gap`) を持つ。計算は**整数のみ**、`Configure` を
//! 受けたときに再計算する。400 / 480 行で同じアプリが崩れないこと。
//!
//! 主軸 = `row` なら x、`column` なら y。交差軸の大きさは `cross`(>0) があれば
//! それ、無ければ親の内側いっぱい。どちらも `min_w` / `min_h` で下限を守る。

use crate::uistate::{is_container, s, WK_ROW, GUI_NONE};
use os32api::gui::types::Rect;

/// 子の主軸の決め方 (契約 U7)。
#[derive(Clone, Copy)]
pub enum SizeSpec {
    /// 固定 px。
    Fixed(i16),
    /// 余りを重みで分ける。
    Flex(u16),
    /// 絶対座標配置 (親の内側原点からの相対矩形)。フローから外れる。
    Absolute(Rect),
}

/* ================================================================ */
/*  再計算                                                           */
/* ================================================================ */

/// ウィンドウの根から木全体のレイアウトを引き直す。
/// `client` はクライアント面の大きさ (原点は 0,0 のローカル)。
pub fn layout_window(root: u16, client: Rect) {
    if root == GUI_NONE {
        return;
    }
    place(root as usize - 1, Rect::new(0, 0, client.w, client.h));
}

/// 1 つのウィジェットを矩形に置き、コンテナなら子を並べる。
pub fn place(idx: usize, rect: Rect) {
    {
        let st = s();
        st.widgets[idx].rect = rect;
        if !is_container(st.widgets[idx].kind) {
            return;
        }
    }
    layout_children(idx);
}

fn layout_children(idx: usize) {
    let (rect, pad, gap, row) = {
        let st = s();
        let w = &st.widgets[idx];
        (w.rect, w.pad as i32, w.gap as i32, w.kind == WK_ROW)
    };
    let inner = Rect::new(
        (rect.x as i32 + pad) as i16,
        (rect.y as i32 + pad) as i16,
        clamp_i16(rect.w as i32 - pad * 2),
        clamp_i16(rect.h as i32 - pad * 2),
    );

    /* --- 1 周目: 子を数え、固定分と重みを集める。Absolute はここで置く。 --- */
    let mut n_flow = 0i32;
    let mut fixed_total = 0i32;
    let mut weight_total = 0i32;

    let mut child = first_child(idx);
    while let Some(ci) = child {
        let (size, min_main) = child_main(ci, row);
        match size {
            SizeSpec::Absolute(r) => {
                let ar = Rect::new(
                    (inner.x as i32 + r.x as i32) as i16,
                    (inner.y as i32 + r.y as i32) as i16,
                    r.w,
                    r.h,
                );
                place(ci, ar);
            }
            SizeSpec::Fixed(px) => {
                let m = if (px as i32) < min_main { min_main } else { px as i32 };
                fixed_total += m;
                n_flow += 1;
            }
            SizeSpec::Flex(w) => {
                fixed_total += min_main;
                weight_total += w as i32;
                n_flow += 1;
            }
        }
        child = next_sibling(ci);
    }

    if n_flow == 0 {
        return;
    }

    let main_total = if row { inner.w as i32 } else { inner.h as i32 };
    let gaps = gap * (n_flow - 1);
    let mut avail = main_total - fixed_total - gaps;
    if avail < 0 {
        avail = 0;
    }

    /* --- 2 周目: 主軸の長さを決めて順に置く。 --- */
    let mut used_flex = 0i32;
    let mut seen_flex = 0i32;
    let mut cursor = if row { inner.x as i32 } else { inner.y as i32 };

    let mut child = first_child(idx);
    while let Some(ci) = child {
        let (size, min_main) = child_main(ci, row);
        let main = match size {
            SizeSpec::Absolute(_) => {
                child = next_sibling(ci);
                continue;
            }
            SizeSpec::Fixed(px) => {
                if (px as i32) < min_main {
                    min_main
                } else {
                    px as i32
                }
            }
            SizeSpec::Flex(w) => {
                seen_flex += w as i32;
                /* 端数は最後の Flex にまとめる (整数のみ、合計が avail に一致)。 */
                let upto = if weight_total > 0 { avail * seen_flex / weight_total } else { 0 };
                let share = upto - used_flex;
                used_flex = upto;
                min_main + share
            }
        };

        let cross_avail = if row { inner.h as i32 } else { inner.w as i32 };
        let cross = child_cross(ci, row, cross_avail);

        let r = if row {
            Rect::new(cursor as i16, inner.y, clamp_i16(main), clamp_i16(cross))
        } else {
            Rect::new(inner.x, cursor as i16, clamp_i16(cross), clamp_i16(main))
        };
        place(ci, r);

        cursor += main + gap;
        child = next_sibling(ci);
    }
}

/* ================================================================ */
/*  小道具                                                           */
/* ================================================================ */

#[inline]
fn clamp_i16(v: i32) -> i16 {
    if v < 0 {
        0
    } else if v > 0x7FFF {
        0x7FFF
    } else {
        v as i16
    }
}

#[inline]
fn first_child(idx: usize) -> Option<usize> {
    let c = s().widgets[idx].first_child;
    if c == GUI_NONE {
        None
    } else {
        Some(c as usize - 1)
    }
}

#[inline]
fn next_sibling(idx: usize) -> Option<usize> {
    let c = s().widgets[idx].next_sibling;
    if c == GUI_NONE {
        None
    } else {
        Some(c as usize - 1)
    }
}

/// 子の主軸の指定と主軸方向の下限。
fn child_main(idx: usize, row: bool) -> (SizeSpec, i32) {
    let st = s();
    let w = &st.widgets[idx];
    let min = if row { w.min_w as i32 } else { w.min_h as i32 };
    (w.size, min)
}

/// 子の交差軸の長さ (0 = 親いっぱい)。下限を守る。
fn child_cross(idx: usize, row: bool, avail: i32) -> i32 {
    let st = s();
    let w = &st.widgets[idx];
    let want = w.cross as i32;
    let min = if row { w.min_h as i32 } else { w.min_w as i32 };
    let mut v = if want > 0 { want } else { avail };
    if v < min {
        v = min;
    }
    v
}
