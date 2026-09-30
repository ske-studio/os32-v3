//! slot.rs — SHM スロットの物理アドレス解決とヘッダ / 要求 / 応答 / リングの
//! 低レベルアクセス (契約 T2 / T2a)。
//!
//! MEM_SHM_GUI_BASE = shm_base + 0x30000 (memmap.h)。スロット N の先頭 =
//! MEM_SHM_GUI_BASE + N * 16KB。gshell は CPL=0 なので SHM を直接読み書きできる。
//! ヘッダの `ring_tail` / `flags` / `dropped` / `seq` は WM (生産者) だけが書く。
//! `ring_head` はアプリ (消費者) だけが書く (契約 T3)。

use core::ptr;

use crate::wm::{GuiState, GUI_SHM_OFFSET};
use os32api::gui::proto::{
    GuiSlotHeader, GUI_PROTO_VERSION, GUI_SLOT_ARGS_OFF, GUI_SLOT_ARGS_SIZE, GUI_SLOT_HDR_OFF,
    GUI_SLOT_REQ_OFF, GUI_SLOT_RESP_OFF, GUI_SLOT_RING_OFF, GUI_SLOT_SIZE,
};

/* ================================================================ */
/*  取り込み tick の記録 (契約 P2)                                    */
/*                                                                  */
/*  「WM は入力を取り込んだ tick を serial ごとに直近 64 件、スロットの */
/*  予備領域 (T2 の約 5KB のうち 256B) に記録する」。引数バッファの直後  */
/*  = 11280 から 256B を 64 エントリ × 4B で使う:                     */
/*      +0 u16 serial / +2 u16 tick の下位 16bit (10ms 粒度)          */
/*  索引は serial % 64。**共有ヘッダには未記載** — C2 の gui_bench が   */
/*  読むときは PM 経由で os32_gui_shared.h へ追記が要る (申し送り)。    */
/* ================================================================ */
pub const GUI_SLOT_TRACE_OFF: usize = GUI_SLOT_ARGS_OFF + GUI_SLOT_ARGS_SIZE; /* 11280 */
pub const GUI_TRACE_ENTRIES: usize = 64;
pub const GUI_SLOT_TRACE_SIZE: usize = GUI_TRACE_ENTRIES * 4; /* 256B */

/* 予備領域に収まることを固定する。 */
const _: () = assert!(GUI_SLOT_TRACE_OFF + GUI_SLOT_TRACE_SIZE <= GUI_SLOT_SIZE);

/// スロット N の先頭バイトポインタ。
#[inline]
pub fn slot_base(st: &GuiState, slot: usize) -> *mut u8 {
    (st.shm_base + GUI_SHM_OFFSET + (slot as u32) * (GUI_SLOT_SIZE as u32)) as *mut u8
}

#[inline]
pub fn header_ptr(st: &GuiState, slot: usize) -> *mut GuiSlotHeader {
    unsafe { slot_base(st, slot).add(GUI_SLOT_HDR_OFF) as *mut GuiSlotHeader }
}

#[inline]
pub fn req_ptr(st: &GuiState, slot: usize) -> *const u8 {
    unsafe { slot_base(st, slot).add(GUI_SLOT_REQ_OFF) as *const u8 }
}

#[inline]
pub fn resp_ptr(st: &GuiState, slot: usize) -> *mut u8 {
    unsafe { slot_base(st, slot).add(GUI_SLOT_RESP_OFF) as *mut u8 }
}

/// リング先頭 (128 × 16B)。
#[inline]
pub fn ring_ptr(st: &GuiState, slot: usize) -> *mut u8 {
    unsafe { slot_base(st, slot).add(GUI_SLOT_RING_OFF) as *mut u8 }
}

/// ヘッダを読む (コピー)。
#[inline]
pub fn read_header(st: &GuiState, slot: usize) -> GuiSlotHeader {
    unsafe { ptr::read_unaligned(header_ptr(st, slot)) }
}

/// ヘッダを書く。
#[inline]
pub fn write_header(st: &GuiState, slot: usize, h: &GuiSlotHeader) {
    unsafe { ptr::write_unaligned(header_ptr(st, slot), *h) }
}

/// スロットヘッダを初期化する (INIT 時)。ring_head/tail を 0 に、proto を入れる。
pub fn init_header(st: &GuiState, slot: usize) {
    let h = GuiSlotHeader {
        proto_version: GUI_PROTO_VERSION,
        flags: 0,
        seq: 0,
        ring_head: 0,
        ring_tail: 0,
        dropped: 0,
        reserved: 0,
    };
    write_header(st, slot, &h);
}

/// 要求ブロックから型 T を読む (先頭から)。
///
/// # Safety
/// アプリが要求ブロックへ正しい構造体を書いていることを前提にする。
#[inline]
pub unsafe fn read_req<T: Copy>(st: &GuiState, slot: usize) -> T {
    ptr::read_unaligned(req_ptr(st, slot) as *const T)
}

/// 応答ブロックへ型 T を書く (先頭から)。
#[inline]
pub fn write_resp<T: Copy>(st: &GuiState, slot: usize, v: T) {
    unsafe { ptr::write_unaligned(resp_ptr(st, slot) as *mut T, v) }
}

/// 入力を取り込んだ tick を serial ごとに記録する (契約 P2)。
pub fn record_trace(st: &GuiState, slot: usize, serial: u16, tick: u32) {
    let idx = (serial as usize) % GUI_TRACE_ENTRIES;
    unsafe {
        let p = slot_base(st, slot).add(GUI_SLOT_TRACE_OFF + idx * 4) as *mut u16;
        ptr::write_unaligned(p, serial);
        ptr::write_unaligned(p.add(1), (tick & 0xFFFF) as u16);
    }
}
