//! lease.rs — パレットのリース (契約 G8、票 W2 の B)。
//!
//! フォーカスを持つウィンドウの所有者だけが、バックエンドが許す範囲
//! (`gfx_screen_info()` の `lease_mask` / `lease_first` / `lease_count`) の
//! パレット項目を自分の色に置き換えられる。リースは**ウィンドウに紐づき、
//! フォーカスに従って**入れ替わる:
//!
//! ```text
//!   LEASE_PALETTE(first, count, rgb)  … 窓にリースを記録 (X1: 状態更新だけ)
//!   フォーカスが移る / リースが変わる … reconcile() が
//!        旧: gfx_set_palette でシステム色へ戻す + Palette{active:0}
//!        新: gfx_lease_palette でアプリ色を入れる + Palette{active:1}
//!   リース中は WM のクロームが 2 色 + パターンになる (chrome.rs / desktop.rs)
//! ```
//!
//! アプリは範囲を分けて何度でも呼ぶ (`lease_test` は 1〜6 と 8〜15 の 2 回) ので、
//! 窓は「借りている index のビット集合 (`Win::lease_mask`) + 16 色分の色」で
//! 保持し、適用時に**連続する区間ごとに** `gfx_lease_palette` を呼ぶ。
//!
//! 実際に `gfx_lease_palette` を呼ぶのは **X2 (COMMIT) / X3 (WAIT)** で、
//! `gui_call` のハンドラ (X1) は記録だけを行う (契約 T8 の「X1 で KAPI を
//! 呼ばない」)。遅れは高々 1 周で、アプリから見た色は最初の commit から正しい。

use crate::wm::{self, GuiState, Rect};
use crate::ring;
use os32api::gui::proto::{GuiRgb, GUI_EV_PALETTE, OS32_ERR_INVAL};

/// 16 色機で貸せる最大項目数。
pub const MAX_LEASE: usize = 16;

/* ================================================================ */
/*  要求の受理 (X1)                                                  */
/* ================================================================ */

/// `LEASE_PALETTE` の中身を検証してフォーカス窓へ記録する。
///
/// - フォーカス (最前面の可視窓) が `owner` のものでなければ `OS32_ERR_INVAL`。
/// - `count == 0` は**全部返却** (次の [`reconcile`] でシステム色へ戻る)。
/// - 範囲は `gfx_screen_info()` の値で弾く (H1 側でも二重に弾かれる)。
pub fn request(st: &mut GuiState, owner: i32, first: u16, count: u16, rgb: &[GuiRgb; 16]) -> i32 {
    let index = match st.front_index() {
        Some(i) if st.windows[i].owner == owner => i,
        _ => return OS32_ERR_INVAL,
    };
    if count == 0 {
        st.windows[index].lease_mask = 0;
        st.lease_dirty = true;
        return 0;
    }
    if count as usize > MAX_LEASE {
        return OS32_ERR_INVAL;
    }
    if !range_ok(st, first, count) {
        return OS32_ERR_INVAL;
    }
    let mut i = 0;
    while i < count {
        let idx = (first + i) as usize;
        st.windows[index].lease_rgb[idx] = rgb[i as usize];
        st.windows[index].lease_mask |= 1u16 << idx;
        i += 1;
    }
    st.lease_dirty = true;
    0
}

/// 貸せる範囲か (契約 G8。16 色機は `lease_mask`、256 色機は連続範囲)。
fn range_ok(st: &GuiState, first: u16, count: u16) -> bool {
    if st.cap_lease_count > 0 {
        /* 256 色機: [lease_first, lease_first + lease_count)。v1 の WM は
         * 16 色分しか保持しないので、そのまま通す範囲だけを受ける。 */
        first >= st.cap_lease_first
            && (first as u32) + (count as u32)
                <= (st.cap_lease_first as u32) + (st.cap_lease_count as u32)
            && (first as usize) + (count as usize) <= MAX_LEASE
    } else {
        /* 16 色機: lease_mask のビットが立つ index だけ */
        if (first as u32) + (count as u32) > 16 {
            return false;
        }
        let mut i = first;
        while i < first + count {
            if (st.cap_lease_mask & (1u16 << i)) == 0 {
                return false;
            }
            i += 1;
        }
        true
    }
}

/* ================================================================ */
/*  適用 / 返却 (X2 / X3)                                            */
/* ================================================================ */

/// いま適用されているべきリース元ウィンドウの完全な id (0 = リース無し)。
pub fn desired(st: &GuiState) -> u32 {
    match st.front_index() {
        Some(i) if st.windows[i].used && st.windows[i].lease_mask != 0 => st.windows[i].id(i),
        _ => 0,
    }
}

/// リース中か (= WM のクロームを 2 色で描くか)。
#[inline]
pub fn mono(st: &GuiState) -> bool {
    st.lease_applied != 0
}

/// 望まれる状態と実際の状態を合わせる。差が無ければ即戻る (毎周呼んでよい)。
pub fn reconcile(st: &mut GuiState) {
    let want = desired(st);
    let cur = st.lease_applied;
    if want == cur && !st.lease_dirty {
        return;
    }
    st.lease_dirty = false;

    if cur != 0 && cur != want {
        /* 旧リース元からシステム色へ戻す。 */
        wm::install_system_palette();
        notify(st, cur, false);
    }
    if want != 0 {
        apply(st, want);
        if cur != want {
            notify(st, want, true);
        }
    }

    let mono_before = cur != 0;
    let mono_after = want != 0;
    st.lease_applied = want;
    if mono_before != mono_after {
        /* クロームとデスクトップの描き方が変わるので全面を積み直す。 */
        let whole = Rect::new(0, 0, st.screen_w, st.screen_h);
        st.dirty_screen(whole);
    }
}

/// フルスクリーン GFX からの復帰などで、いまのリースを入れ直す。
pub fn reapply(st: &mut GuiState) {
    if st.lease_applied != 0 {
        apply(st, st.lease_applied);
    }
}

/// 借りている index の**連続区間ごと**に `gfx_lease_palette` を呼ぶ。
fn apply(st: &GuiState, win_id: u32) {
    let index = match st.win_by_id(win_id) {
        Some(i) => i,
        None => return,
    };
    let w = &st.windows[index];
    if w.lease_mask == 0 {
        return;
    }
    let a = unsafe { os32api::api() };
    let mut i = 0usize;
    while i < MAX_LEASE {
        if (w.lease_mask & (1u16 << i)) == 0 {
            i += 1;
            continue;
        }
        let start = i;
        while i < MAX_LEASE && (w.lease_mask & (1u16 << i)) != 0 {
            i += 1;
        }
        /* GuiRgb は u8 × 3 (align 1) なので配列は 3B/項目でそのまま渡せる。 */
        let p = unsafe { (w.lease_rgb.as_ptr() as *const u8).add(start * 3) };
        unsafe {
            (a.gfx_lease_palette)(start as i32, (i - start) as i32, p);
        }
    }
}

/// `Palette{active}` をリース元のアプリへ届ける (契約 G8 / U2)。
fn notify(st: &mut GuiState, win_id: u32, active: bool) {
    let index = match st.win_by_id(win_id) {
        Some(i) => i,
        None => return,
    };
    let owner = st.windows[index].owner;
    if let Some(slot) = st.slot_of_owner(owner) {
        let ev = ring::ev_simple(GUI_EV_PALETTE, active as u8, win_id);
        ring::append(st, slot, &ev);
    }
}

/// ウィンドウが消える / 破棄されるときにリースを落とす。
pub fn release_window(st: &mut GuiState, index: usize) {
    if st.windows[index].lease_mask == 0 {
        return;
    }
    st.windows[index].lease_mask = 0;
    st.lease_dirty = true;
}
