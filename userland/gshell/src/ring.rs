//! ring.rs — イベントリング (128 × 16B、契約 T3)。
//!
//! WM は `ring_tail` だけを進め (生産者)、アプリのライブラリが `ring_head` を
//! 進める (消費者)。空きが無ければ `append` は false を返す (呼び出し側が
//! Pointer を落とす / カーネル待ち行列から取り込まない、を決める)。連続する
//! `Pointer` は最新 1 件へ畳む。

use core::ptr;

use crate::slot;
use crate::wm::GuiState;
use os32api::gui::proto::{
    GuiEvent, GuiRect16, GUI_EV_POINTER, GUI_HDR_FLAG_OVERFLOW, GUI_RING_CAPACITY,
};

#[inline]
pub fn count(head: u16, tail: u16) -> u16 {
    tail.wrapping_sub(head)
}

/// リングに溜まっている未読件数 (= tail − head)。
pub fn pending(st: &GuiState, slot: usize) -> u16 {
    let h = slot::read_header(st, slot);
    count(h.ring_head, h.ring_tail)
}

/// 空き件数。
pub fn space(st: &GuiState, slot: usize) -> u16 {
    (GUI_RING_CAPACITY as u16).saturating_sub(pending(st, slot))
}

#[inline]
fn write_entry(st: &GuiState, slot: usize, ring_index: u16, ev: &GuiEvent) {
    let base = slot::ring_ptr(st, slot);
    unsafe {
        let p = base.add((ring_index as usize) * 16) as *mut GuiEvent;
        ptr::write_unaligned(p, *ev);
    }
}

#[inline]
fn read_entry(st: &GuiState, slot: usize, ring_index: u16) -> GuiEvent {
    let base = slot::ring_ptr(st, slot);
    unsafe {
        let p = base.add((ring_index as usize) * 16) as *const GuiEvent;
        ptr::read_unaligned(p)
    }
}

/// イベントを追記する。Pointer は末尾の Pointer に畳む。返り値 false = 満杯。
pub fn append(st: &GuiState, slot: usize, ev: &GuiEvent) -> bool {
    let mut h = slot::read_header(st, slot);
    let cnt = count(h.ring_head, h.ring_tail);

    /* Pointer の畳み込み: 末尾が同じ窓の Pointer なら上書き。 */
    if ev.kind == GUI_EV_POINTER && cnt > 0 {
        let last_idx = h.ring_tail.wrapping_sub(1) % (GUI_RING_CAPACITY as u16);
        let last = read_entry(st, slot, last_idx);
        if last.kind == GUI_EV_POINTER && last.window == ev.window {
            write_entry(st, slot, last_idx, ev);
            return true;
        }
    }

    if cnt >= GUI_RING_CAPACITY as u16 {
        return false;
    }
    let idx = h.ring_tail % (GUI_RING_CAPACITY as u16);
    write_entry(st, slot, idx, ev);
    h.ring_tail = h.ring_tail.wrapping_add(1);
    slot::write_header(st, slot, &h);
    true
}

/// OVERFLOW フラグを立て、dropped に n を足す (契約 T3)。
pub fn add_dropped(st: &GuiState, slot: usize, n: u16) {
    if n == 0 {
        return;
    }
    let mut h = slot::read_header(st, slot);
    h.dropped = h.dropped.saturating_add(n);
    h.flags |= GUI_HDR_FLAG_OVERFLOW;
    slot::write_header(st, slot, &h);
}

/* ================================================================ */
/*  GuiEvent ビルダ (ペイロード 8B をバイト列で構築)                 */
/* ================================================================ */

#[inline]
fn payload_zero() -> [u8; 8] {
    [0u8; 8]
}

fn payload_rect(r: GuiRect16) -> [u8; 8] {
    let mut p = [0u8; 8];
    p[0..2].copy_from_slice(&r.x.to_le_bytes());
    p[2..4].copy_from_slice(&r.y.to_le_bytes());
    p[4..6].copy_from_slice(&r.w.to_le_bytes());
    p[6..8].copy_from_slice(&r.h.to_le_bytes());
    p
}

pub fn ev_simple(kind: u8, sub: u8, window: u32) -> GuiEvent {
    GuiEvent { kind, sub, serial: 0, window, payload: payload_zero() }
}

pub fn ev_rect(kind: u8, window: u32, r: GuiRect16) -> GuiEvent {
    GuiEvent { kind, sub: 0, serial: 0, window, payload: payload_rect(r) }
}

pub fn ev_key(down: bool, window: u32, scan: u8, ch: u8, mods: u8, serial: u16) -> GuiEvent {
    let mut p = [0u8; 8];
    p[0] = scan;
    p[1] = ch;
    p[2] = mods;
    GuiEvent { kind: os32api::gui::proto::GUI_EV_KEY, sub: down as u8, serial, window, payload: p }
}

pub fn ev_text(window: u32, utf8: [u8; 8], sub: u8, serial: u16) -> GuiEvent {
    GuiEvent { kind: os32api::gui::proto::GUI_EV_TEXT, sub, serial, window, payload: utf8 }
}

pub fn ev_pointer(window: u32, x: i16, y: i16, buttons: u8, serial: u16) -> GuiEvent {
    let mut p = [0u8; 8];
    p[0..2].copy_from_slice(&x.to_le_bytes());
    p[2..4].copy_from_slice(&y.to_le_bytes());
    GuiEvent { kind: GUI_EV_POINTER, sub: buttons, serial, window, payload: p }
}

pub fn ev_button(down: bool, window: u32, x: i16, y: i16, button: u8, serial: u16) -> GuiEvent {
    let mut p = [0u8; 8];
    p[0..2].copy_from_slice(&x.to_le_bytes());
    p[2..4].copy_from_slice(&y.to_le_bytes());
    p[4] = button;
    GuiEvent { kind: os32api::gui::proto::GUI_EV_BUTTON, sub: down as u8, serial, window, payload: p }
}

pub fn ev_timer(window: u32, id: u8) -> GuiEvent {
    GuiEvent { kind: os32api::gui::proto::GUI_EV_TIMER, sub: id, serial: 0, window, payload: payload_zero() }
}

/// `Modal{dialog, result}` (契約 U4 / U2)。`window` は親ウィンドウ。
pub fn ev_modal(window: u32, dialog: u16, result: i16) -> GuiEvent {
    let mut p = [0u8; 8];
    p[0..2].copy_from_slice(&dialog.to_le_bytes());
    p[2..4].copy_from_slice(&result.to_le_bytes());
    GuiEvent { kind: os32api::gui::proto::GUI_EV_MODAL, sub: 0, serial: 0, window, payload: p }
}
