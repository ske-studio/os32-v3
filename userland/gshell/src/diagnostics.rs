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
    let mut emit = |args: core::fmt::Arguments<'_>| {
        let mut line = Line { bytes: [0; 200], n: 0 };
        let _ = line.write_fmt(args);
        line.bytes[line.n..line.n+2].copy_from_slice(b"\r\n");
        unsafe { (os32api::api().serial_puts)(line.bytes.as_ptr()); }
    };
    // Private resident CRT symbol; USER heaps and KAPI remain unchanged.
    #[cfg(not(test))]
    {
        unsafe extern "C" { fn os32_resident_sbrk_stats(out: *mut u32); }
        let mut sbrk = [0u32; 3];
        unsafe { os32_resident_sbrk_stats(sbrk.as_mut_ptr()); }
        let mut stat = [0u8; MEMSTAT_SIZE];
        let rc = unsafe { (os32api::api().mem_stat)(0, stat.as_mut_ptr(), MEMSTAT_SIZE as u32) };
        if rc >= MEMSTAT_SIZE as i32 {
            emit_resident(sbrk, &stat, &mut emit);
        } else {
            emit(format_args!("OS32: resident FAIL mem_stat={}", rc));
        }
    }
    dump_with(emit);
}

fn emit_resident(sbrk: [u32; 3], stat: &[u8], mut emit: impl FnMut(core::fmt::Arguments<'_>)) {
    let stack = word(stat, 132);
    emit(format_args!("OS32: resident sbrk_used={} sbrk_peak={} sbrk_capacity={} exec_peak={} exec_capacity={}",
        sbrk[0], sbrk[1], sbrk[2], word(stat, 140), word(stat, 36)));
    emit(format_args!("OS32: diag kstack={} shell_stack={} kheap_peak={} resident_peak={}",
        stack & 0xffff, stack >> 16, word(stat, 136), word(stat, 140)));
}

#[cfg(test)]
mod tests {
    #[test]
    fn separate_resident_peaks_and_h5_words() {
        let mut stat = [0u8; super::MEMSTAT_SIZE];
        for (offset, value) in [(36, 262144u32), (132, (20440 << 16) | 3544),
                                (136, 45176), (140, 131656)] {
            stat[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        let mut lines = std::string::String::new();
        super::emit_resident([20, 40, 150000], &stat, |args| {
            use std::fmt::Write;
            writeln!(lines, "{}", args).unwrap();
        });
        assert!(lines.contains("sbrk_used=20 sbrk_peak=40 sbrk_capacity=150000 exec_peak=131656 exec_capacity=262144"));
        assert!(lines.contains("kstack=3544 shell_stack=20440 kheap_peak=45176 resident_peak=131656"));
    }
}
