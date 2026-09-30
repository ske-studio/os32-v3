//! damage.rs — 損傷矩形 (契約 G4 の dirty / issued、32px 境界、隣接結合)。
//!
//! 座標はすべてクライアントローカル。`add_dirty` は 32px グリッドへ丸めてから
//! 既存 dirty と重なる/隣接するものを結合する (既存 `gfx_add_dirty_rect` と同じ規則、
//! 上限 8/ウィンドウ)。上限を超える場合は外接矩形へ潰す (過剰申告は安全側)。

use crate::wm::{GuiState, Rect, RectSet, Win, DAMAGE_SNAP, MAX_DMG, MAX_VIS};
use os32api::gui::proto::GUI_MAX_WINDOWS;

/// 32px グリッドへ拡張する。
fn snap32(r: Rect) -> Rect {
    if r.is_empty() {
        return r;
    }
    let x0 = (r.x / DAMAGE_SNAP) * DAMAGE_SNAP;
    let y0 = (r.y / DAMAGE_SNAP) * DAMAGE_SNAP;
    let x1 = ((r.right() + DAMAGE_SNAP - 1) / DAMAGE_SNAP) * DAMAGE_SNAP;
    let y1 = ((r.bottom() + DAMAGE_SNAP - 1) / DAMAGE_SNAP) * DAMAGE_SNAP;
    Rect::new(x0, y0, x1 - x0, y1 - y0)
}

/// 2 矩形が重なる、または 32px 以内で隣接しているか (結合判定)。
fn near(a: &Rect, b: &Rect) -> bool {
    let ax0 = a.x - DAMAGE_SNAP;
    let ay0 = a.y - DAMAGE_SNAP;
    let aw = a.w + DAMAGE_SNAP * 2;
    let ah = a.h + DAMAGE_SNAP * 2;
    Rect::new(ax0, ay0, aw, ah).intersects(b)
}

/// dirty へ 1 矩形を足す (32px 丸め + 隣接結合、上限 8)。
pub fn add_dirty(win: &mut Win, rect_local: Rect) {
    /* クライアント矩形にクランプ。 */
    let (cw, ch) = win.client_size();
    let clamped = rect_local.intersect(&Rect::new(0, 0, cw, ch));
    if clamped.is_empty() {
        return;
    }
    let mut r = snap32(clamped);
    r = r.intersect(&Rect::new(0, 0, cw, ch));
    if r.is_empty() {
        return;
    }

    /* 既存と結合できるなら union する。 */
    let mut i = 0;
    while i < win.dirty.len {
        if near(&win.dirty.rects[i], &r) {
            let merged = win.dirty.rects[i].union(&r);
            /* 結合後の重複を畳むため、この要素を除いて再帰的に足し直す。 */
            remove_at(&mut win.dirty, i);
            add_dirty(win, merged);
            return;
        }
        i += 1;
    }

    if win.dirty.len < MAX_DMG {
        win.dirty.push(r);
    } else {
        /* 上限超過: 先頭と union して数を保つ (過剰申告=安全)。 */
        let merged = win.dirty.rects[0].union(&r);
        win.dirty.rects[0] = merged;
    }
}

/// dirty へ**細い帯** (ドラッグ枠の縁) を 1 本足す。32px 丸めは [`add_dirty`] と
/// 同じだが、**外接矩形が面積を増やす結合はしない**。
///
/// [`add_dirty`] は 32px 以内の矩形を外接矩形へ再帰的に畳むので、角で接する縦横の
/// 帯 (枠の上辺と左辺) が 1 枚になり、窓の内側まで丸ごと Paint になる (Codex
/// レビュー P2、Help の例で旧枠 1 回 = 96×158 px)。ドラッグは 1 回の移動ごとに
/// これを出すので、386 では帯だけを描き直させる。結合するのは外接矩形の面積が
/// 2 枚の面積の和を超えない (= 同じ線上で重なる / 接する) ときだけ。上限 8 枚に
/// 達したら、面積の増えが最小の 1 枚へ畳む (過剰申告 = 安全側)。
pub fn add_dirty_band(win: &mut Win, rect_local: Rect) {
    let (cw, ch) = win.client_size();
    let client = Rect::new(0, 0, cw, ch);
    let clamped = rect_local.intersect(&client);
    if clamped.is_empty() {
        return;
    }
    let r = snap32(clamped).intersect(&client);
    if r.is_empty() {
        return;
    }
    let area = |a: &Rect| (a.w as i64) * (a.h as i64);
    let mut i = 0;
    while i < win.dirty.len {
        let d = win.dirty.rects[i];
        let u = d.union(&r);
        if area(&u) <= area(&d) + area(&r) && (d.intersects(&r) || near(&d, &r)) {
            win.dirty.rects[i] = u;
            return;
        }
        i += 1;
    }
    if win.dirty.len < MAX_DMG {
        win.dirty.push(r);
        return;
    }
    let mut best = 0;
    let mut best_grow = i64::MAX;
    let mut k = 0;
    while k < win.dirty.len {
        let d = win.dirty.rects[k];
        let grow = area(&d.union(&r)) - area(&d);
        if grow < best_grow {
            best_grow = grow;
            best = k;
        }
        k += 1;
    }
    win.dirty.rects[best] = win.dirty.rects[best].union(&r);
}

/// dirty をクライアント全面 1 枚にする (露出計算の容量超過フォールバック等)。
pub fn set_dirty_full(win: &mut Win) {
    let (cw, ch) = win.client_size();
    win.dirty.clear();
    if cw > 0 && ch > 0 {
        win.dirty.push(Rect::new(0, 0, cw, ch));
    }
}

/// 生きている全ウィンドウを全面 dirty にする (不具合 W-1)。
///
/// バックバッファを WM が丸ごと捨てたとき (`gfx_init` は VRAM の両ページを
/// ゼロクリアする) に使う。**クライアント面を持っているのはアプリだけ**
/// (契約 G4) なので、消した画を取り戻す道は「本人に `Paint` を出して描き
/// 直させる」しかない。park 中のアプリも `damage::has_deliverable_paint` が
/// 真になって導出群の ready に入り、top-level の `multiapp::pick` が起こす。
pub fn invalidate_all_clients(st: &mut GuiState) {
    let mut i = 0;
    while i < GUI_MAX_WINDOWS {
        if st.windows[i].used {
            set_dirty_full(&mut st.windows[i]);
        }
        i += 1;
    }
}

fn remove_at(set: &mut RectSet, i: usize) {
    if i >= set.len {
        return;
    }
    let mut j = i;
    while j + 1 < set.len {
        set.rects[j] = set.rects[j + 1];
        j += 1;
    }
    set.len -= 1;
}

/// dirty のうち index 番目を可視領域でクリップした断片 (Paint の矩形候補)。
pub fn clip_to_vis(win: &Win, dirty_rect: Rect) -> RectSet {
    let mut out = RectSet::EMPTY;
    let mut i = 0;
    while i < win.vis.len {
        let piece = dirty_rect.intersect(&win.vis.rects[i]);
        if !piece.is_empty() {
            out.push(piece);
        }
        i += 1;
    }
    out
}

/// dirty ∩ 可視領域 = **配送できる `Paint` の候補**。`(添字 = 元の dirty,
/// 断片)` を最大 `MAX_VIS` 件。
///
/// **起床判定 (`OP_WAIT`) も配送 (`OP_POLL`) も必ずこれを通す。** 2 か所で
/// 別々に書くと必ず食い違い、「起こされないと配れない / 配れないと起きられない」
/// で止まる (v1.2 G3 で実測: 打鍵の結果がマウスを動かすまで画面に出ない)。
/// 戻り値の 3 つ目は **`cand` が MAX_VIS で打ち切られたか**。
///
/// 打ち切られると、まだ見ていない dirty 矩形には候補が 1 つも入らない。
/// 呼び出し側がそれを「不可視だから候補が無い」と取り違えると、**可視なのに
/// 描かれていない領域を捨てる** (2026-09-10 のレビュー指摘 P2。可視 8 本 ×
/// dirty 3 本 = 24 > 16 で到達する)。捨ててよいのは「見たうえで断片が 0 だった」
/// dirty だけなので、打ち切りの有無を呼び出し側へ渡す。
pub fn deliverable_cand(win: &Win) -> ([(usize, Rect); MAX_VIS], usize, bool) {
    let mut cand = [(0usize, Rect::EMPTY); MAX_VIS];
    let mut n = 0;
    if win.dirty.is_empty() || win.vis.is_empty() {
        return (cand, n, false);
    }
    let mut i = 0;
    let mut capped = false;
    while i < win.dirty.len {
        if n >= MAX_VIS {
            /* まだ見ていない dirty が残っている */
            capped = true;
            break;
        }
        let pieces = clip_to_vis(win, win.dirty.rects[i]);
        let mut k = 0;
        while k < pieces.len {
            if n >= MAX_VIS {
                /* この dirty の断片すら入り切っていない */
                capped = true;
                break;
            }
            cand[n] = (i, pieces.rects[k]);
            n += 1;
            k += 1;
        }
        i += 1;
    }
    (cand, n, capped)
}

/// 配送できる `Paint` があるか (契約 T3 の `OP_WAIT` 起床条件)。
/// 完全に隠れた窓 (可視領域が空) では常に false — 露出で初めて配送対象になる
/// (契約 G4)。
pub fn has_deliverable_paint(win: &Win) -> bool {
    if win.dirty.is_empty() {
        return false;
    }
    /* 可視領域が打ち切られている窓 (契約 G4「超過分は次の周」) の `vis` は
     * 真の可視領域の**部分集合**でしかなく、断片を入れ替えるのは `OP_POLL` の
     * 中の `page_vis` だけ。ここで部分集合だけを見て「配送できない」と決めると、
     * 入れ替えの機会そのものが来ない。dirty があるなら起こして `OP_POLL` に
     * 判断させる (そこで配れなければ `emit_paints_win` が dirty を掃除する)。 */
    if win.vis_capped {
        return true;
    }
    deliverable_cand(win).1 > 0
}
