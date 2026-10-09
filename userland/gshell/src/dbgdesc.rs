//! 読み取り専用のホスト観測契約。番地は ELF の局所記号から解決する。
use core::mem::{offset_of, size_of, size_of_val};
use crate::{multiapp, trim::SentCell, wm::{GuiCell, GuiState, Slot, Win}};
use os32api::gui::proto::{GUI_MAX_WINDOWS, GUI_SLOT_MAX};

// 欄の追加・削除・意味の変更時は VERSION を上げ、読み手の欄表も更新する。
pub const VERSION: u32 = 1;
pub const MAGIC: u32 = u32::from_le_bytes(*b"GSDD");
pub const WORDS: usize = 31;
#[used]
#[no_mangle]
pub static GSHELL_DBG_DESC: [u32; WORDS] = [
    MAGIC, VERSION, WORDS as u32,
    size_of::<GuiCell>() as u32, offset_of!(GuiCell, 0) as u32,
    offset_of!(GuiState, windows) as u32, size_of::<Win>() as u32, GUI_MAX_WINDOWS as u32,
    offset_of!(Win, used) as u32, offset_of!(Win, gen) as u32,
    offset_of!(Win, owner) as u32,
    offset_of!(Win, x) as u32, offset_of!(Win, y) as u32,
    offset_of!(Win, w) as u32, offset_of!(Win, h) as u32,
    offset_of!(Win, title) as u32, size_of_val(&Win::EMPTY.title) as u32,
    offset_of!(Win, visible) as u32, offset_of!(Win, minimized) as u32,
    offset_of!(GuiState, zorder) as u32, offset_of!(GuiState, z_count) as u32,
    size_of::<usize>() as u32,
    offset_of!(GuiState, slots) as u32, size_of::<Slot>() as u32, GUI_SLOT_MAX as u32,
    offset_of!(Slot, used) as u32, offset_of!(Slot, owner) as u32,
    size_of::<SentCell>() as u32, offset_of!(SentCell, 0) as u32,
    multiapp::MAX_APPS as u32, multiapp::APP_ID_MIN as u32,
];
