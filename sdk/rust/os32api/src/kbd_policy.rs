//! KAPI v92 keyboard policy. Event records are native-endian and IKM1 is little-endian.

pub const KBD_POLICY_CONTEXT_COMMON: u32 = 0;
pub const KBD_POLICY_CONTEXT_LINE_EDIT: u32 = 1;
pub const KBD_POLICY_CONTEXT_RUNNING: u32 = 2;
pub const KBD_POLICY_CONTEXT_BYTE_INPUT: u32 = 3;
pub const KBD_POLICY_CONTEXT_GUI: u32 = 4;
pub const KBD_POLICY_DELIVERY_BYTE: u32 = 0;
pub const KBD_POLICY_DELIVERY_ACTION: u32 = 1;
pub const KBD_POLICY_ACTION_NONE: u32 = 0;
pub const KBD_POLICY_ACTION_IME_ON: u32 = 1;
pub const KBD_POLICY_ACTION_IME_OFF: u32 = 2;
pub const KBD_POLICY_ACTION_INTERRUPT: u32 = 3;
pub const KBD_POLICY_ACTION_CANCEL_LINE: u32 = 4;
pub const KBD_POLICY_ACTION_EOF_INPUT: u32 = 5;
pub const KBD_POLICY_ACTION_KILL_TO_BOL: u32 = 6;
pub const KBD_POLICY_ACTION_KILL_TO_EOL: u32 = 7;
pub const KBD_POLICY_ACTION_REDRAW: u32 = 8;
pub const KBD_POLICY_ACTION_CLEAR_VIEW: u32 = 9;
pub const KBD_POLICY_ACTION_COPY: u32 = 10;
pub const KBD_POLICY_ACTION_CUT: u32 = 11;
pub const KBD_POLICY_ACTION_PASTE: u32 = 12;
pub const KBD_POLICY_ACTION_UNDO: u32 = 13;
pub const KBD_POLICY_ACTION_REDO: u32 = 14;
pub const KBD_POLICY_ACTION_SELECT_ALL: u32 = 15;
pub const KBD_POLICY_ACTION_SWITCH_APP: u32 = 16;
pub const KBD_POLICY_ACTION_CLOSE: u32 = 17;
pub const KBD_POLICY_ACTION_BSD_SPECIAL: u32 = 18;
pub const KBD_POLICY_TERM_VINTR: u32 = 1;
pub const KBD_POLICY_TERM_VKILL: u32 = 2;
pub const KBD_POLICY_TERM_VEOF: u32 = 3;
pub const KBD_POLICY_TERM_KILL_TO_EOL: u32 = 4;
pub const KBD_POLICY_TERM_REDRAW: u32 = 5;
pub const KBD_POLICY_TERM_ERASE_PREV: u32 = 6;
pub const KBD_POLICY_TERM_ACCEPT_LINE: u32 = 7;
pub const KBD_POLICY_TERM_COMPLETE: u32 = 8;
pub const KBD_POLICY_ROW_META: u32 = 1;
pub const KBD_POLICY_ROW_KEYSYM: u32 = 2;
pub const KBD_POLICY_ROW_EXACT: u32 = 3;
pub const KBD_POLICY_ROW_ACCENT: u32 = 4;
pub const KBD_POLICY_ROW_TERM: u32 = 5;
pub const KBD_POLICY_ROW_EXT_COMPAT: u32 = 6;
pub const KBD_POLICY_ROW_FLAGS: u32 = 7;
pub const KBD_POLICY_ROW_KEY_ACTION: u32 = 8;
pub const KBD_POLICY_PROFILE_COMMON: u32 = 0;
pub const KBD_POLICY_PROFILE_PC98_CUI: u32 = 1;
pub const KBD_POLICY_PROFILE_PC98_GUI: u32 = 2;
pub const KBD_POLICY_PROFILE_MAC_CUI: u32 = 3;
pub const KBD_POLICY_PROFILE_MAC_GUI: u32 = 4;
pub const KBD_POLICY_MAP_PC98: u32 = 1;
pub const KBD_POLICY_MAP_HID: u32 = 2;
pub const KBD_POLICY_KIND_CHAR: u32 = 0;
pub const KBD_POLICY_KIND_BSD_SPECIAL: u32 = 1;
pub const KBD_POLICY_KIND_LEGACY_CONTROL: u32 = 2;
pub const KBD_POLICY_META_N_KEYS: u32 = 0;
pub const KBD_POLICY_META_N_ACCS: u32 = 1;
pub const KBD_POLICY_CELL_COMMAND: u32 = 0;
pub const KBD_POLICY_CELL_GROUP1_0: u32 = 1;
pub const KBD_POLICY_CELL_GROUP1_1: u32 = 2;
pub const KBD_POLICY_CELL_GROUP2_0: u32 = 3;
pub const KBD_POLICY_CELL_GROUP2_1: u32 = 4;
pub const KBD_POLICY_EXACT_FLAGS_O: u32 = 0;
pub const KBD_POLICY_EXACT_FLAGS_C: u32 = 1;
pub const KBD_POLICY_EXACT_FLAGS_N: u32 = 2;
pub const KBD_POLICY_EXACT_FLAGS_B: u32 = 3;
pub const KBD_POLICY_ISIG: u32 = 1;
pub const KBD_POLICY_ICANON: u32 = 2;
pub const KBD_POLICY_MOD_SHIFT: u32 = 1;
pub const KBD_POLICY_MOD_CTRL: u32 = 2;
pub const KBD_POLICY_MOD_ALT: u32 = 4;
pub const KBD_POLICY_MOD_COMMAND: u32 = 16;
pub const KBD_POLICY_MOD_MASK: u32 = 23;
pub const KBD_POLICY_MOD_MODE: u32 = 8;
pub const KBD_POLICY_MOD_META: u32 = 32;
pub const KBD_POLICY_LOCK_CAPS: u32 = 0x100;
pub const KBD_POLICY_LOCK_NUM: u32 = 0x200;
pub const KBD_POLICY_LOCK_KANA: u32 = 0x400;
pub const KBD_POLICY_LOCK_SCROLL: u32 = 0x800;
pub const KBD_POLICY_LOCK_ALT: u32 = 0x1000;
pub const KBD_POLICY_SC_DOWN: u32 = 0x100;
pub const KBD_POLICY_SC_REPEAT: u32 = 0x200;
pub const KBD_POLICY_SC_NO_KEYDATA: u32 = 0x400;
pub const KBD_POLICY_SC_IS_MODIFIER: u32 = 0x800;
pub const KBD_POLICY_KEYSYM_VOID: u32 = 0xf500;
pub const KBD_POLICY_COMPAT_SELECTOR: u32 = 0xffff;
pub const KBD_POLICY_WIRE_VERSION: u32 = 2;
pub const KBD_POLICY_HEADER_SIZE: u32 = 16;
pub const KBD_POLICY_ROW_SIZE: u32 = 16;
pub const KBD_POLICY_MAX_ROWS: u32 = 0x400;
pub const KBD_POLICY_MAX_WIRE: u32 = KBD_POLICY_HEADER_SIZE + KBD_POLICY_MAX_ROWS * KBD_POLICY_ROW_SIZE;
pub const KBD_POLICY_READ_MAX: u32 = 0x80;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct KbdPolicyAction {
    pub action: u32,
    pub command_gen: u32,
    pub time_ms: u32,
    pub value: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct KbdPolicyKey {
    pub src_generation: u32,
    pub map_id: u32,
    pub keycode: u32,
    pub sc_down_repeat: u32,
    pub physical_mods: u32,
    pub logical_mods: u32,
    pub time_ms: u32,
    pub map_generation: u32,
    pub keysym: u32,
    pub kind: u32,
    pub action: u32,
    pub value: u32,
}
