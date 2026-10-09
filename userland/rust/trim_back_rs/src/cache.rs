//! Shared guest/host fixture. All buffers go through the actual Rust allocator.
use alloc::boxed::Box;
use core::alloc::Layout;
use os32api::api;

pub const BLOCK_BYTES: usize = 4000;
pub const LIMIT: usize = 64;
pub const CACHE_T: usize = 8;
pub const PAGE: u32 = 4096;
pub const GROW: u32 = 65536;
// MemStat wire fields (KAPI 73), in u32 words; extents are kind minus one.
const WORDS: usize = 132 / 4;
const FLAGS: usize = 3;
const INITIAL: usize = 13;
const ARENA: usize = 15;
const LARGE: usize = 16;
const CUR: usize = 22;
const EXEC_BASE: usize = 26;
const EXEC_SIZE: usize = 27;
const MAP_EXACT: u32 = 1;
const INVALID: u32 = 6; // MEMSTAT_POISONED | MEMSTAT_HEAP_INVALID
const PENDING: u32 = 8;
// Current ILP32 heap wire format: kernel/kmalloc.h and Os32Alloc::AllocPrefix.
const HEADER: u32 = 8;
const PREFIX: u32 = 8;
const ALIGN: u32 = 8;
const RAW_SIZE: u32 = (BLOCK_BYTES as u32 + PREFIX + ALIGN - 1 + ALIGN - 1) & !(ALIGN - 1);
const FREE_MAGIC: u32 = 0xfeeefeee;

#[derive(Clone, Copy)]
pub struct Stat(pub [u32; WORDS]);
impl Stat {
    pub fn read() -> Option<Self> {
        let mut s = Self([0; WORDS]);
        let rc = unsafe { (api().mem_stat)(-1, s.0.as_mut_ptr().cast(), (WORDS * 4) as u32) };
        if rc == (WORDS * 4) as i32 && s.0[FLAGS] & INVALID == 0 { Some(s) } else { None }
    }
    pub fn cur(&self) -> u32 { self.0[CUR] }
    pub fn arenas(&self) -> u32 { self.0[ARENA] }
    pub fn large(&self) -> u32 { self.0[LARGE] }
    pub fn initial(&self) -> u32 { self.0[INITIAL] }
    pub fn pending(&self) -> bool { self.0[FLAGS] & PENDING != 0 }
    pub fn id(&self) -> u32 { self.0[1] }
}

pub fn raw_map(bytes: u32, hint: *mut u8, flags: u32) -> *mut u8 {
    unsafe { (api().mem_map)(bytes, hint, flags) }
}

pub fn block() -> Option<Box<[u8; BLOCK_BYTES]>> {
    // Fallible allocation, followed by Box ownership; OOM must return PREP FAIL.
    let p = unsafe { alloc::alloc::alloc_zeroed(Layout::new::<[u8; BLOCK_BYTES]>()) };
    if p.is_null() { None } else { Some(unsafe { Box::from_raw(p.cast()) }) }
}

pub struct Cache {
    pub blocks: [Option<Box<[u8; BLOCK_BYTES]>>; LIMIT],
    pub count: usize,
    pub keep: usize,
    pub obstacle: u32,
    pub crc: u32,
    pub initial: Stat,
    pub before: Stat,
    pub trimmed: Stat,
    pub armed: bool,
    pub released: bool,
    pub observed: bool,
    pub bad: bool,
    pub hook_calls: u32,
    pub pages: u32,
    arena2_pages: u32,
    pub regrow_rc: i32,
    pub unmap_rc: i32,
}
impl Cache {
    pub const fn new() -> Self {
        Self { blocks: [const { None }; LIMIT], count: 0, keep: 0, obstacle: 0,
            crc: 0, initial: Stat([0; WORDS]), before: Stat([0; WORDS]),
            trimmed: Stat([0; WORDS]), armed: false, released: false, observed: false,
            bad: false, hook_calls: 0, pages: 0, arena2_pages: 0, regrow_rc: -1, unmap_rc: -1 }
    }
    pub fn prepare(&mut self) -> Option<()> {
        self.initial = Stat::read()?;
        let mut stat = self.initial;
        while self.count < LIMIT {
            self.blocks[self.count] = Some(block()?);
            self.count += 1;
            stat = Stat::read()?;
            if self.obstacle == 0 && stat.arenas() == 1 {
                self.obstacle = stat.cur();
                let p = raw_map(PAGE, self.obstacle as *mut u8, MAP_EXACT);
                if p as u32 != self.obstacle { return None; }
            }
            if stat.arenas() == 2 { break; }
        }
        if stat.arenas() != 2 || stat.large() != 0 || self.obstacle == 0 { return None; }
        // Sort by address, not allocation order. INITIAL is retained with ARENA1.
        self.blocks[..self.count].sort_unstable_by_key(|b| b.as_ref().unwrap().as_ptr() as usize);
        let low = self.blocks[..self.count].iter()
            .filter(|b| (b.as_ref().unwrap().as_ptr() as usize) < self.obstacle as usize).count();
        if low <= CACHE_T || low == self.count { return None; }
        self.keep = low - CACHE_T;
        if (self.blocks[self.keep].as_ref()?.as_ptr() as u32) < self.initial.cur() { return None; }
        for (i, b) in self.blocks[..self.keep].iter_mut().enumerate() {
            for (j, byte) in b.as_mut()?.iter_mut().enumerate() { *byte = (i ^ j ^ 0xa5) as u8; }
        }
        self.crc = self.data_crc();
        self.before = stat;
        Some(())
    }
    pub fn data_crc(&self) -> u32 {
        let mut crc = !0u32;
        for b in self.blocks[..self.keep].iter() {
            let Some(b) = b else { return 0; };
            for byte in b.iter() {
                crc ^= *byte as u32;
                for _ in 0..8 { crc = (crc >> 1) ^ (0xedb88320 & 0u32.wrapping_sub(crc & 1)); }
            }
        }
        !crc
    }
    pub fn release(&mut self) -> u32 {
        if !self.armed || self.released { return 0; }
        self.hook_calls += 1;
        let Some(before) = Stat::read() else { self.bad = true; return 0; };
        // Preparation stops at the first Box in the TOPDOWN arena. Inspect its
        // kernel BlkHdr and following free tail while both are mapped. This
        // records the CURRENT extent size (G-2 may have shrunk it to one page).
        // Using global free-page deltas after WAIT would count other apps too.
        let Some(high) = self.blocks[self.count - 1].as_ref() else { self.bad = true; return 0; };
        let base = high.as_ptr() as u32 - HEADER - PREFIX;
        if base < self.obstacle || base % PAGE != 0 { self.bad = true; return 0; }
        let used = unsafe { core::ptr::read_volatile(base as *const u32) };
        if used != RAW_SIZE { self.bad = true; return 0; }
        let tail = base + HEADER + used;
        let size = unsafe { core::ptr::read_volatile(tail as *const u32) };
        let magic = unsafe { core::ptr::read_volatile((tail + 4) as *const u32) };
        let bytes = HEADER + used + HEADER + size;
        if magic != FREE_MAGIC || bytes > GROW || bytes % PAGE != 0 { self.bad = true; return 0; }
        self.arena2_pages = bytes / PAGE;
        self.before = before; // G-2's unarmed DONE may already have trimmed natural tails.
        for b in self.blocks[self.keep..self.count].iter_mut() { drop(b.take()); }
        self.released = true;
        self.bad |= self.data_crc() != self.crc;
        // Drop frees heap blocks. Kernel returns pages only in subsequent DONE;
        // thus this hook's page hint is zero, actual pages are observed separately.
        0
    }
    pub fn observe_done(&mut self) -> bool {
        if !self.released || self.observed { return !self.bad; }
        let Some(s) = Stat::read() else { self.bad = true; return false; };
        self.observed = true;
        self.trimmed = s;
        self.pages = self.before.cur().saturating_sub(s.cur()) / PAGE + self.arena2_pages;
        self.bad |= s.pending() || self.before.arenas() != 2 || s.arenas() != 1 || s.large() != 0 ||
            s.cur() >= self.before.cur() || s.initial() != self.initial.initial() ||
            s.0[EXEC_BASE] != self.initial.0[EXEC_BASE] || s.0[EXEC_SIZE] != self.initial.0[EXEC_SIZE] ||
            self.data_crc() != self.crc || self.pages == 0;
        !self.bad
    }
    pub fn regrow(&mut self) -> bool {
        if !self.observed || self.regrow_rc != -1 { self.bad = true; return false; }
        self.regrow_rc = 1;
        self.unmap_rc = unsafe { (api().mem_unmap)(self.obstacle as *mut u8, PAGE) };
        if self.unmap_rc != 0 { self.bad = true; return false; }
        let Some(b) = block() else { self.bad = true; return false; };
        let Some(s) = Stat::read() else { self.bad = true; return false; };
        // Kernel header + actual Os32Alloc prefix: user pointer is 16 bytes in.
        let ok = b.as_ptr() as u32 == self.trimmed.cur() + HEADER + PREFIX &&
            s.cur() == self.trimmed.cur() + GROW && s.arenas() == 1 && s.large() == 0 && self.data_crc() == self.crc;
        self.blocks[self.keep] = Some(b);
        self.regrow_rc = if ok { 0 } else { 1 };
        self.bad |= !ok;
        ok
    }
    pub fn passed(&self) -> bool {
        !self.bad && self.observed && self.released && self.hook_calls == 1 && self.regrow_rc == 0 && self.pages > 0
    }
}
