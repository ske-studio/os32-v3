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
fn puts(s: &[u8]) { unsafe { (os32api::api().serial_puts)(s.as_ptr()); } }
fn number(mut n: u32) {
    let mut buf = [0u8; 11];
    let mut i = 10;
    loop {
        i -= 1;
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 { break; }
    }
    puts(&buf[i..]);
}
pub fn dump() {
    for id in multiapp::APP_ID_MIN..=multiapp::APP_ID_MAX {
        let prefix = trim::stat(id);
        let mut full = [0u8; MEMSTAT_SIZE];
        let rc = unsafe { (os32api::api().mem_stat)(id, full.as_mut_ptr(), MEMSTAT_SIZE as u32) };
        let state = unsafe { (os32api::api().exec_app_state)(id) } as u32;
        puts(b"OS32: slot id=\0"); number(id as u32);
        puts(b" state=\0"); number(state);
        puts(b" owner_pages=\0"); number(if rc >= MEMSTAT_SIZE as i32 { word(&full, OWNER_PAGES) } else { 0 });
        puts(b" exec_heap_used=\0"); number(prefix.as_ref().map_or(0, |s| word(s, 76)));
        puts(b" trim_pending=\0"); number(prefix.as_ref().map_or(0, |s| (word(s, 12) >> 3) & 1));
        puts(b"\r\n\0");
    }
}
