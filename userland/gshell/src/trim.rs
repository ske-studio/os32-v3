//! T2g: 保留 1 件に EV_TRIM を 1 件だけ配る。X3 / 単独ループ専用。
//! 消費後も pending bit が立つ間は再配送しない。順序は既存 multiapp に任せる。

use crate::{multiapp, ring, wm::GuiState};
use core::cell::UnsafeCell;

// KAPI 73 の MemStat (sdk/include/os32/os32_kapi_shared.h)。Rust 束縛は
// 生バッファなので、132B と末尾欄の位置を gshell 内へ同値で写す ([C4])。
const MEMSTAT_SIZE: usize = 132;
const PENDING_OFFSET: usize = 124;
const EPOCH_OFFSET: usize = 128;

pub(crate) struct SentCell(pub(crate) UnsafeCell<[bool; multiapp::MAX_APPS]>);
unsafe impl Sync for SentCell {}
// OS32 はシングルタスク・協調切替。配送中に park / yield はしない。
pub(crate) static SENT: SentCell = SentCell(UnsafeCell::new([false; multiapp::MAX_APPS]));

/// ゲスト観測用。gshell ELF から名前を解決する。
#[no_mangle]
pub static mut gshell_trim_delivered: u32 = 0;
#[no_mangle]
pub static mut gshell_trim_skipped_full: u32 = 0;

fn stat(id: i32) -> Option<[u8; MEMSTAT_SIZE]> {
    let mut out = [0u8; MEMSTAT_SIZE];
    let rc = unsafe { (os32api::api().mem_stat)(id, out.as_mut_ptr(), MEMSTAT_SIZE as u32) };
    if rc < 0 { None } else { Some(out) }
}

fn word(out: &[u8; MEMSTAT_SIZE], offset: usize) -> u32 {
    u32::from_le_bytes([out[offset], out[offset + 1], out[offset + 2], out[offset + 3]])
}

pub fn deliver(st: &GuiState) {
    let global = match stat(0) {
        Some(out) => out,
        None => return,
    };
    let mask = word(&global, PENDING_OFFSET);
    let sent = unsafe { &mut *SENT.0.get() };
    for i in 0..multiapp::MAX_APPS {
        let id = multiapp::APP_ID_MIN + i as i32;
        if mask & (1u32 << id) == 0 {
            sent[i] = false;
            continue;
        }
        if sent[i] || !multiapp::is_tracked(id) || id == st.front_owner() {
            continue;
        }
        let slot = match st.slot_of_owner(id) {
            Some(slot) => slot,
            None => continue,
        };
        if unsafe { (os32api::api().exec_app_state)(id) } != multiapp::APP_STATE_PARKED {
            continue;
        }
        let app = match stat(id) {
            Some(out) => out,
            None => continue,
        };
        let ev = ring::ev_trim(word(&app, EPOCH_OFFSET));
        if ring::append(st, slot, &ev) {
            sent[i] = true;
            unsafe { gshell_trim_delivered = gshell_trim_delivered.wrapping_add(1); }
        } else {
            unsafe { gshell_trim_skipped_full = gshell_trim_skipped_full.wrapping_add(1); }
        }
    }
}

pub fn forget(id: i32) {
    if id >= multiapp::APP_ID_MIN && id <= multiapp::APP_ID_MAX {
        unsafe { (*SENT.0.get())[(id - multiapp::APP_ID_MIN) as usize] = false; }
    }
}

#[cfg(test)]
pub fn reset() {
    unsafe {
        *SENT.0.get() = [false; multiapp::MAX_APPS];
        gshell_trim_delivered = 0;
        gshell_trim_skipped_full = 0;
    }
}
