//! クリップ (契約 G2)。
//!
//! - **基底クリップは処理中の `Paint` 矩形** (G2 改訂)。C2 のループが `Paint{rect}` を
//!   処理する間、[`set_base_clip`] で基底を固定し、[`push_clip`] はその内側にしか効かない。
//! - クリップは常に「現在のクリップ ∩ サーフェス境界」。深さ 8 (`GUI_MAX_CLIP_DEPTH`)。
//! - 窓のクライアント面は、基底未設定のままでは描画を拒む (`debug_assert` + no-op)。
//!   `Paint` の外で窓面に描く手段は無い (契約 G2)。フルスクリーン/オフスクリーンの
//!   非窓サーフェスは、基底未設定なら暗黙にサーフェス全域を基底とする。
use crate::gstate::{st, SurfaceKind};
use os32api::gui::proto::GUI_MAX_CLIP_DEPTH;
use os32api::gui::types::{Rect, SurfaceId};

/// 処理中の `Paint` 矩形をこのサーフェスの基底クリップに固定する (契約 G2)。
/// クリップスタックを深さ 0 に戻し、`rect ∩ サーフェス境界` を基底に置く。
/// 戻り値: 0 成功、負値 (`OS32_ERR_STALE`) は無効な surface。
pub fn set_base_clip(surface: SurfaceId, rect: Rect) -> i32 {
    let s = st();
    let idx = match s.resolve(surface) {
        Some(i) => i,
        None => return os32api::gui::proto::OS32_ERR_STALE,
    };
    let bounds = s.surfaces[idx].bounds();
    s.active = surface;
    s.have_base = true;
    s.depth = 0;
    s.clip[0] = rect.intersect(&bounds);
    0
}

/// 基底クリップを解除する (Paint の処理が終わったとき)。以後、窓面への描画は拒まれる。
pub fn clear_base_clip() {
    let s = st();
    s.active = SurfaceId::NULL;
    s.have_base = false;
    s.depth = 0;
}

/// 現在のクリップの内側にさらに矩形を掛ける。深さ上限 (8) を超えると失敗 (-1)。
/// 戻り値: 新しい深さ (>=1)、または負値。
pub fn push_clip(rect: Rect) -> i32 {
    let s = st();
    if s.active.is_null() || !s.have_base {
        debug_assert!(false, "push_clip without an active base clip (call set_base_clip first)");
        return os32api::gui::proto::OS32_ERR_INVAL;
    }
    if s.depth + 1 >= GUI_MAX_CLIP_DEPTH {
        return os32api::gui::proto::OS32_ERR_FULL;
    }
    let cur = s.clip[s.depth];
    s.depth += 1;
    s.clip[s.depth] = cur.intersect(&rect);
    s.depth as i32
}

/// [`push_clip`] を 1 段戻す。基底 (深さ 0) は外せない。
pub fn pop_clip() {
    let s = st();
    if s.depth > 0 {
        s.depth -= 1;
    }
}

/// 現在の実効クリップ矩形 (サーフェスローカル座標)。デバッグ/計測用。
pub fn current_clip() -> Rect {
    let s = st();
    if s.have_base {
        s.clip[s.depth]
    } else {
        Rect::EMPTY
    }
}

/* ------------------------------------------------------------------ */
/*  内部: 描画先サーフェスの実効クリップと絶対原点を解決する            */
/* ------------------------------------------------------------------ */

/// 描画時のクリップ解決結果。
pub(crate) struct Target {
    pub idx: usize,
    /// 絶対原点 (Screen サーフェスのみ意味を持つ。Offscreen は 0,0)。
    pub ox: i32,
    pub oy: i32,
    pub offscreen: bool,
    /// 実効クリップ (サーフェスローカル、サーフェス境界内)。
    pub clip: Rect,
}

/// 描画先を解決する。基底未設定の窓面は拒否 (None)。空クリップも None ではなく
/// `clip.is_empty()` で表す (呼び出し側が早期 return する)。
pub(crate) fn resolve_target(surface: SurfaceId) -> Option<Target> {
    let s = st();
    let idx = s.resolve(surface)?;
    let ent = s.surfaces[idx];
    let bounds = ent.bounds();

    let clip = if !s.active.is_null() && s.active.raw() == surface.raw() && s.have_base {
        /* このサーフェスに対して基底が設定済み */
        s.clip[s.depth]
    } else {
        /* 基底未設定 */
        if ent.is_window {
            s.base_violations = s.base_violations.wrapping_add(1);
            debug_assert!(false, "draw to window client surface without an active Paint base clip");
            return None;
        }
        bounds
    };

    let (ox, oy, offscreen) = match ent.kind {
        SurfaceKind::Screen { ox, oy } => (ox, oy, false),
        SurfaceKind::Offscreen { .. } => (0, 0, true),
    };

    Some(Target {
        idx,
        ox,
        oy,
        offscreen,
        clip: clip.intersect(&bounds),
    })
}
