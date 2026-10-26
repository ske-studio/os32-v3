//! Value-only reader of the kernel's ver/About lines. No device formatting here.
//! Constants and repr(C) layout mirror sdk/include/os32/os32_ver_info.h;
//! tools/tests/test_about_info.py checks both against the real C ABI.
pub const OS32_VER_LINES: usize = 20;
pub const OS32_VER_TEXT: usize = 64;
pub const OS32_VER_COLS: usize = 52;
pub const OS32_VER_KIND_MASK: u32 = 0xff;
pub const OS32_VER_HEAD: u32 = 0;
pub const OS32_VER_DEVICE: u32 = 1;
pub const OS32_VER_IDENTITY: u32 = 2;
pub const OS32_VER_DEBUG: u32 = 3;
pub const OS32_VER_INDENT: u32 = 0x100;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VerLine {
    pub kind: u32,
    pub text: [u8; OS32_VER_TEXT],
}

impl VerLine {
    pub fn as_bytes(&self) -> &[u8] {
        let n = self.text.iter().position(|&c| c == 0).unwrap_or(OS32_VER_TEXT);
        &self.text[..n]
    }

    pub fn group(&self) -> u32 {
        self.kind & OS32_VER_KIND_MASK
    }
}

#[repr(C)]
pub struct VerInfo {
    pub count: u32,
    pub lines: [VerLine; OS32_VER_LINES],
}

impl VerInfo {
    pub const fn new() -> Self {
        Self { count: 0, lines: [VerLine { kind: 0, text: [0; OS32_VER_TEXT] }; OS32_VER_LINES] }
    }

    pub fn rows(&self) -> &[VerLine] {
        &self.lines[..core::cmp::min(self.count as usize, OS32_VER_LINES)]
    }
}

const _: () = assert!(core::mem::size_of::<VerLine>() == 68);
const _: () = assert!(core::mem::size_of::<VerInfo>() == 1364);
