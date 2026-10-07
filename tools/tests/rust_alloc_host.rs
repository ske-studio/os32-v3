use core::alloc::{GlobalAlloc, Layout};
use std::sync::Mutex;
static LOCK: Mutex<()> = Mutex::new(());
static mut LAST_BYTES: u32 = 0;
static mut FAIL: bool = false;
static mut FREED: usize = 0;
static mut BAD_FREE: bool = false;
static mut BLOCKS: [usize; 8] = [0; 8];
unsafe extern "C" fn mem_alloc(bytes: u32) -> *mut u8 {
    LAST_BYTES = bytes;
    if FAIL { return core::ptr::null_mut(); }
    let raw = std::alloc::alloc(Layout::from_size_align(bytes as usize, 8).unwrap());
    // Deliberately only 8-aligned, even if the system gave us stronger alignment.
    let ptr = raw.add(8);
    // Allocate extra space below via the wrapper installed in setup.
    for i in 0..8 { if BLOCKS[i] == 0 { BLOCKS[i] = ptr as usize; return ptr; } }
    panic!("fixture slots exhausted");
}
unsafe extern "C" fn raw_alloc(bytes: u32) -> *mut u8 {
    // mem_alloc mock retains bytes+8 so the shifted block stays within bounds.
    let p = mem_alloc(bytes + 8);
    LAST_BYTES = bytes;
    if !p.is_null() { p.write_bytes(0xa5, bytes as usize); }
    p
}
unsafe extern "C" fn mem_free(ptr: *mut u8) {
    let i = (0..8).find(|&i| BLOCKS[i] == ptr as usize);
    if i.is_none() { BAD_FREE=true; return; }
    BLOCKS[i.unwrap()] = 0;
    // Leaking fixture storage avoids mirroring adapter size metadata in mock.
    FREED += 1;
}
#[test]
fn allocator_contract() {
    let _guard = LOCK.lock().unwrap();
    let mut api = os32api::mock_api();
    api.mem_alloc = raw_alloc; api.mem_free = mem_free;
    os32api::os32_init(&mut api);
    unsafe {
        // The production allocator is common to app, shlib and gshell.
        {
            for align in [4,8,16,64,4096] {
                let l=Layout::from_size_align(97,align).unwrap();
                let p=Os32Alloc.alloc(l);
                assert!(!p.is_null());
                assert_eq!(p as usize % align,0,"Layout alignment");
                let h=&*p.sub(core::mem::size_of::<AllocPrefix>()).cast::<AllocPrefix>();
                assert_eq!(h.payload,p);
                assert!(p as usize >= h.base as usize + core::mem::size_of::<AllocPrefix>());
                assert!(p as usize + l.size() <= h.base as usize + LAST_BYTES as usize);
                p.write_bytes(0x71,l.size());
                let before=FREED;
                FAIL=true;
                let q=Os32Alloc.realloc(p,l,200);
                assert!(q.is_null());
                assert_eq!(FREED,before,"failed realloc retains old block");
                assert_eq!(*p.add(96),0x71);
                FAIL=false;
                let q=Os32Alloc.realloc(p,l,200);
                assert!(!q.is_null()); assert_eq!(*q.add(96),0x71);
                assert_eq!(q as usize % align,0);
                assert!(!BAD_FREE,"dealloc must use raw base");
                assert_eq!(FREED,before+1);
                Os32Alloc.dealloc(q,Layout::from_size_align(200,align).unwrap());
                assert!(!BAD_FREE,"dealloc must use raw base");
                let z=Os32Alloc.alloc_zeroed(l);
                assert!(!z.is_null());
                assert!((0..97).all(|i| *z.add(i)==0),"explicit zeroing");
                Os32Alloc.dealloc(z,l);
            }
        }
        let old=LAST_BYTES;
        assert!(Os32Alloc.alloc(Layout::from_size_align(0,8).unwrap()).is_null());
        assert_eq!(LAST_BYTES,old);
        FAIL=true;
        for (size,align) in [(u32::MAX as usize,8), (u32::MAX as usize-31,64), (u32::MAX as usize+1,8), (1,1usize<<32)] {
            assert!(Os32Alloc.alloc(Layout::from_size_align(size,align).unwrap()).is_null(),"u32 overflow");
            assert_eq!(LAST_BYTES,old,"overflow must not call mem_alloc");
        }
        FAIL=false;
        let l=Layout::from_size_align(62000,4096).unwrap();
        let p=Os32Alloc.alloc(l); assert!(!p.is_null());
        assert!(l.size()<65536 && LAST_BYTES>=65536,"raw bytes cross 65536");
        Os32Alloc.dealloc(p,l);
        let l=Layout::from_size_align(32,8).unwrap();
        FAIL=true; assert!(Os32Alloc.alloc(l).is_null()); FAIL=false;
        assert!(BLOCKS.iter().all(|&p| p==0));
    }
}
