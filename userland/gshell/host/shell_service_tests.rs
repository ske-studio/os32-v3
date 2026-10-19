//! T7b-b2 S3a: `__os32_aux` の受取 (ffi::shell_service) — 番地 0・印違い・版違いは None。
use super::*;
use crate::mocks;

unsafe extern "C" fn fake_validate(id: u32) -> i32 {
    id as i32
}

fn table(magic: u32, version: u32) -> ShellService {
    ShellService {
        magic,
        version,
        generation: 7,
        font_source_open: None,
        font_source_read_at: None,
        font_source_validate: Some(fake_validate),
        font_source_close: None,
    }
}

fn set_aux(p: *const ShellService) {
    unsafe { mocks::__os32_aux = p as *const core::ffi::c_void };
}

#[test]
fn shell_service_aux_is_checked_before_use() {
    set_aux(core::ptr::null());
    assert!(shell_service().is_none(), "app (aux 0) gets no table");

    let bad_magic = table(SHELL_SERVICE_MAGIC ^ 1, SHELL_SERVICE_VERSION);
    set_aux(&bad_magic);
    assert!(shell_service().is_none(), "wrong magic is rejected");

    let bad_version = table(SHELL_SERVICE_MAGIC, SHELL_SERVICE_VERSION + 1);
    set_aux(&bad_version);
    assert!(shell_service().is_none(), "wrong version is rejected");

    let good = table(SHELL_SERVICE_MAGIC, SHELL_SERVICE_VERSION);
    set_aux(&good);
    let t = shell_service().expect("valid table is accepted");
    assert_eq!(t.generation, 7);
    assert_eq!(unsafe { (t.font_source_validate.unwrap())(5) }, 5);
    set_aux(core::ptr::null());
}

#[test]
fn shell_service_constants_come_from_the_kernel_header() {
    assert_eq!(SHELL_SERVICE_MAGIC, u32::from_le_bytes(*b"SVS1"));
    assert_eq!(SHELL_SERVICE_VERSION, 1);
    assert_eq!(SHELL_SERVICE_SIZE, 28);
}
