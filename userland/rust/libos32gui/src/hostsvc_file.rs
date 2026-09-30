//! hostsvc_file.rs — `hostsvc.rs` の print_file が使うファイル I/O 継ぎ目の
//! **本体** (票 N4 §1)。
//!
//! `hostsvc.rs` は `extern "C"` でこの 3 本を呼ぶだけで os32api を名指ししない。
//! だからホスト TDD (`host_tests`) は `hostsvc.rs` を `#[path]` 取り込みしつつ、
//! ここの代わりに贋物を並べて分岐を固定できる (cfgro.rs の libos32cfg と同じ)。
//!
//! ここは shlib 本体でだけコンパイルされる (os32api の `sys_open/read/close` を
//! 呼ぶ = `os32gui_shlib_init` が渡した KAPI を使う)。
#![allow(clippy::missing_safety_doc)]

use os32api::fs::O_RDONLY;

/// パス (NUL 終端) を RO で開く。fd (>=0) / 負の `OS32_ERR_*`。
#[no_mangle]
pub extern "C" fn hostsvc_file_open(path: *const u8) -> i32 {
    if path.is_null() {
        return -9; /* OS32_ERR_INVAL */
    }
    unsafe { (os32api::api().sys_open)(path, O_RDONLY) }
}

/// `fd` から最大 `cap` バイト読む。読めた数 (>=0、0 = EOF) / 負のエラー。
#[no_mangle]
pub extern "C" fn hostsvc_file_read(fd: i32, buf: *mut u8, cap: u32) -> i32 {
    if buf.is_null() || cap == 0 {
        return 0;
    }
    unsafe { (os32api::api().sys_read)(fd, buf, cap) }
}

/// `fd` を閉じる (`sys_close` は void)。
#[no_mangle]
pub extern "C" fn hostsvc_file_close(fd: i32) {
    unsafe { (os32api::api().sys_close)(fd) }
}
