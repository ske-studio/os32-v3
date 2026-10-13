//! CTRL+GRPH+f10: on-demand serial snapshots, including empty app slots.
use crate::{multiapp, trim};
const MEMSTAT_SIZE: usize = 200;
const OWNER_PAGES: usize = 196;
pub fn shortcut(scan: u8, mods: u32) -> bool {
    scan == 0x6b && mods & 0x19 == 0x18
}
fn word(out: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(out[offset..offset + 4].try_into().unwrap())
}
pub fn dump_with(mut emit: impl FnMut(core::fmt::Arguments<'_>)) {
    for id in multiapp::APP_ID_MIN..=multiapp::APP_ID_MAX {
        let prefix = trim::stat(id);
        let mut full = [0u8; MEMSTAT_SIZE];
        let rc = unsafe { (os32api::api().mem_stat)(id, full.as_mut_ptr(), MEMSTAT_SIZE as u32) };
        let state = unsafe { (os32api::api().exec_app_state)(id) } as u32;
        emit(format_args!("OS32: slot id={} state={} owner_pages={} exec_heap_used={} trim_pending={}",
            id, state, if rc >= MEMSTAT_SIZE as i32 { word(&full, OWNER_PAGES) } else { 0 },
            prefix.as_ref().map_or(0, |s| word(s, 76)),
            prefix.as_ref().map_or(0, |s| (word(s, 12) >> 3) & 1)));
    }
}
pub fn dump() {
    use core::fmt::Write;
    struct Line { bytes: [u8; 200], n: usize }
    impl core::fmt::Write for Line {
        fn write_str(&mut self, s: &str) -> core::fmt::Result {
            if self.n + s.len() > self.bytes.len() - 3 { return Err(core::fmt::Error); }
            self.bytes[self.n..self.n+s.len()].copy_from_slice(s.as_bytes());
            self.n += s.len(); Ok(())
        }
    }
    dump_with(|args| {
        let mut line = Line { bytes: [0; 200], n: 0 };
        let _ = line.write_fmt(args);
        line.bytes[line.n..line.n+2].copy_from_slice(b"\r\n");
        unsafe { (os32api::api().serial_puts)(line.bytes.as_ptr()); }
    });
}
