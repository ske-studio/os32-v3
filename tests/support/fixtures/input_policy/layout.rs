#[path = "../../../../sdk/rust/os32api/src/kbd_policy.rs"]
#[allow(dead_code)]
mod kbd_policy;
use kbd_policy::*;

macro_rules! offsets {
    ($ty:ty, $($field:ident),+) => {{
        let actual = [$(std::mem::offset_of!($ty, $field)),+];
        assert_eq!(std::mem::size_of::<$ty>(), 4 * actual.len());
        for (index, offset) in actual.into_iter().enumerate() {
            assert_eq!(offset, 4 * index);
        }
    }};
}

fn main() {
    offsets!(KbdPolicyAction, action, command_gen, time_ms, value);
    offsets!(KbdPolicyKey, src_generation, map_id, keycode, sc_down_repeat, physical_mods,
             logical_mods, time_ms, map_generation, keysym, kind, action, value);
    let mut wire = Vec::from(*b"IKM1");
    for n in [KBD_POLICY_WIRE_VERSION as u16, KBD_POLICY_ROW_SIZE as u16] { wire.extend(n.to_le_bytes()); }
    for n in [1u32, 0] { wire.extend(n.to_le_bytes()); }
    wire.extend([8, 2, 1, 0]);
    for n in [0x1234u16, 0xabcd] { wire.extend(n.to_le_bytes()); }
    for n in [0x01020304u32, 0x12345678] { wire.extend(n.to_le_bytes()); }
    use std::io::Write;
    std::io::stdout().write_all(&wire).unwrap();
}
