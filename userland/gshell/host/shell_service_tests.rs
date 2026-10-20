//! T7b-b2 S3a: `__os32_aux` の受取 (ffi::shell_service) — 番地 0・印違い・版違いは None。
//! T7b-b3 S2: version 2 (末尾に font_guard_run)、古い版は OldShell (KCG だけ)。
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
        font_guard_run: Some(fake_guard),
    }
}

unsafe extern "C" fn fake_guard(call: Option<unsafe extern "C" fn(*mut u8) -> i32>,
                                ctx: *mut u8) -> i32 {
    match call {
        Some(f) => f(ctx) + 1000,
        None => -1,
    }
}

unsafe extern "C" fn fake_callback(ctx: *mut u8) -> i32 {
    ctx as usize as i32
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
    assert!(matches!(shell_service_state(), ShellServiceState::Absent),
            "an unknown newer table is not used");

    let old = table(SHELL_SERVICE_MAGIC, SHELL_SERVICE_VERSION - 1);
    set_aux(&old);
    assert!(shell_service().is_none(), "old version is not used as v2");
    assert!(matches!(shell_service_state(), ShellServiceState::OldShell),
            "version < 2 is OldShell (KCG only), not Absent");

    let bad_magic_old = table(SHELL_SERVICE_MAGIC ^ 1, SHELL_SERVICE_VERSION - 1);
    set_aux(&bad_magic_old);
    assert!(matches!(shell_service_state(), ShellServiceState::Absent),
            "magic is checked before the version");

    let good = table(SHELL_SERVICE_MAGIC, SHELL_SERVICE_VERSION);
    set_aux(&good);
    let t = shell_service().expect("valid table is accepted");
    assert_eq!(t.generation, 7);
    assert_eq!(unsafe { (t.font_source_validate.unwrap())(5) }, 5);
    assert_eq!(unsafe { (t.font_guard_run.unwrap())(Some(fake_callback), 7 as *mut u8) }, 1007,
               "font_guard_run is the last field and passes call/ctx through");
    assert!(matches!(shell_service_state(), ShellServiceState::Ready(_)));
    set_aux(core::ptr::null());
    assert!(matches!(shell_service_state(), ShellServiceState::Absent));
}

#[test]
fn shell_service_constants_come_from_the_kernel_header() {
    assert_eq!(SHELL_SERVICE_MAGIC, u32::from_le_bytes(*b"SVS1"));
    assert_eq!(SHELL_SERVICE_VERSION, 2);
    assert_eq!(SHELL_SERVICE_SIZE, 32);
    // i386 の 32 バイト = u32 3 語 + 関数番地 5 語 (ホストの番地幅で同じ並びを確かめる)。
    let ptr = core::mem::size_of::<usize>();
    assert_eq!(core::mem::size_of::<ShellService>(), 3 * 4 + 5 * ptr + (if ptr == 8 { 4 } else { 0 }));
    assert_eq!(core::mem::offset_of!(ShellService, font_guard_run), 3 * 4 + 4 * ptr
               + (if ptr == 8 { 4 } else { 0 }), "font_guard_run is appended last");
}
