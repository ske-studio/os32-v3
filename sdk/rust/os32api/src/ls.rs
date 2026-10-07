//! Value enumeration; callbacks run after the syscall has returned.
// Mirrors os32_ls.h and os32_kapi_shared.h; check-ls-client-host checks the
// C/Rust packet layout and the TRUSTED error/fallback against the C constants.
pub const OS32_LS_BATCH: usize = 14;
pub const OS32_LS_NAME_SIZE: usize = 256;
const OS32_ERR_INVAL: i32 = -9;
#[repr(C)]
pub struct LsEntry {
    pub name: [u8; OS32_LS_NAME_SIZE],
    pub size: u32,
    pub ftype: u8,
    reserved: [u8; 3],
}
#[repr(C)]
struct LsPacket {
    count: u32,
    result: i32,
    done: u32,
    entries: [LsEntry; OS32_LS_BATCH],
}
/// The entry pointer is valid only until the callback returns.
/// As with sys_ls, the void callback cannot stop enumeration.
/// TRUSTED callers fall back to the legacy slot on the first INVAL.
pub unsafe fn os32_ls(path: *const u8,
    cb: unsafe extern "C" fn(*const LsEntry, *mut u8), ctx: *mut u8) -> i32 {
    let mut snapshot = [0u8; OS32_LS_NAME_SIZE + 1];
    if !path.is_null() {
        for i in 0..OS32_LS_NAME_SIZE {
            snapshot[i] = *path.add(i);
            if snapshot[i] == 0 { break; }
        }
    }
    let mut skip = 0u32;
    let mut storage = core::mem::MaybeUninit::<LsPacket>::uninit();
    loop {
        let rc = (crate::api().sys_ls_window)(snapshot.as_ptr(), skip, storage.as_mut_ptr().cast());
        if rc == OS32_ERR_INVAL && skip == 0 {
            return (crate::api().sys_ls)(path, cb as *const () as *mut u8, ctx);
        }
        if rc < 0 { return rc; }
        let packet = storage.assume_init_ref();
        if packet.count as usize > OS32_LS_BATCH { return OS32_ERR_INVAL; }
        for i in 0..packet.count as usize { cb(&packet.entries[i], ctx); }
        // Match slot 12: an FS error with done=0 still has another window.
        if packet.done != 0 { return packet.result; }
        if packet.count == 0 { return OS32_ERR_INVAL; }
        skip = match skip.checked_add(packet.count) { Some(n) => n, None => return OS32_ERR_INVAL };
    }
}
