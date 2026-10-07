#![no_std]
#[path = "ls.rs"]
mod ls;
struct Api {
    sys_ls: unsafe extern "C" fn(*const u8, *mut u8, *mut u8) -> i32,
    sys_ls_window: unsafe extern "C" fn(*const u8, u32, *mut u8) -> i32,
}
extern "C" {
    fn window(path: *const u8, skip: u32, out: *mut u8) -> i32;
    fn legacy(path: *const u8, cb: *mut u8, ctx: *mut u8) -> i32;
}
fn api() -> &'static Api {
    static TABLE: Api = Api { sys_ls: legacy, sys_ls_window: window };
    &TABLE
}
#[no_mangle]
pub unsafe extern "C" fn os32_ls(path: *const u8, cb: unsafe extern "C" fn(*const ls::LsEntry, *mut u8), ctx: *mut u8) -> i32 {
    ls::os32_ls(path, cb, ctx)
}
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! { loop {} }
