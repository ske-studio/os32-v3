//! ffi.rs — os32api に無い libos32gfx / lib シンボルの extern 宣言。
//!
//! `os32api::gfx` は libos32gfx の描画関数の一部しか宣言していない (SDK 側は
//! C1/C2 の領分)。gshell がマウスカーソルの下地を退避・復元するのに要る
//! `gfx_get_pixel` と、FEP のセル幅判定に要る `unicode_to_ank` (lib/utf8_prog.o、
//! libos32gfx.a に同梱) をここで宣言する。**PM への申し送り**: 恒久的には
//! `sdk/rust/os32api/src/lib.rs` の `gfx` モジュールへ移すのが筋 (W レーンは
//! `userland/gshell/**` 以外を触らない約束なのでここに置いた)。

extern "C" {
    /// Zero when this libos32gfx instance has no drawable framebuffer.
    pub static gfx_ready: i32;

    /// バックバッファの 1 画素を読む (libos32gfx.h)。
    pub fn gfx_get_pixel(x: i32, y: i32) -> u8;

    /// Unicode → ANK (半角) コード。0 なら全角 (`lib/utf8.c`)。
    pub fn unicode_to_ank(cp: u32) -> u8;

    /// 1bpp の字形 (MSB 左、行 `w_bytes` バイト、`h_lines` 行) を `fg` で置く
    /// (透過。画面外だけ切る、libos32gfx.h)。T7b-b3 の `text.rs` が cell と
    /// clip で切った後の mask を渡す口 [HW1]。
    pub fn gfx_draw_font(x: i32, y: i32, pat: *const u8, w_bytes: i32, h_lines: i32, fg: u8);
}

/* ================================================================ */
/*  常駐 shell の内部入口 (T7b-b2、exec/shell_service.h)               */
/*                                                                  */
/*  kernel は entry frame の 4 語目で表の番地を渡し、CRT0 の          */
/*  `_start_c` がそれを `__os32_aux` に控える (app には 0)。KAPI      */
/*  ではない。印 / 版 / 大きさは exec/shell_service.h から build.rs  */
/*  が写す ([C4])。font への接続 (呼ぶ側) は b3 — 呼ぶ関数は戻り値を  */
/*  局所に受けて使ってから返す薄い `#[inline(never)]` 関数にする      */
/*  (尾呼出しにすると kernel の段 A が戻り番地で断る、設計 §3-2)。    */
/*                                                                  */
/*  version 2 (T7b-b3 S2): 末尾に `font_guard_run` (x87 guard の中で */
/*  callback を 1 回呼ぶ)。version < 2 の表は font を開かず KCG だけ  */
/*  (`ShellServiceState::OldShell` → b3 の `KcgOnly(OldShell)`)。     */
/* ================================================================ */

include!(concat!(env!("OUT_DIR"), "/shell_service.rs"));
// OS32_ERR_NOSPC / NOSYS / BUSY (os32_kapi_shared.h から build.rs が写す、[C4])。
include!(concat!(env!("OUT_DIR"), "/kapi_err.rs"));

/// `exec/shell_service.h` の `ShellService` と同じ並び (i386 で 32 バイト、version 2)。
#[repr(C)]
pub struct ShellService {
    pub magic: u32,
    pub version: u32,
    /// shell 登録世代 (0 = 未登録)。
    pub generation: u32,
    pub font_source_open:
        Option<unsafe extern "C" fn(path: *const u8, out_id: *mut u32, out_size: *mut u32) -> i32>,
    pub font_source_read_at:
        Option<unsafe extern "C" fn(id: u32, offset: u32, dst: *mut u8, len: u32) -> i32>,
    pub font_source_validate: Option<unsafe extern "C" fn(id: u32) -> i32>,
    pub font_source_close: Option<unsafe extern "C" fn(id: u32) -> i32>,
    /// version 2: x87 guard の中で `call(ctx)` を 1 回呼びその戻りを返す。
    /// FPU 無しは `OS32_ERR_NOSYS`、再入・IRQ 文脈は `OS32_ERR_BUSY`。
    /// lease の open/close は guard の外で、callback の中では read_at 以外を呼ばない。
    pub font_guard_run: Option<
        unsafe extern "C" fn(call: Option<unsafe extern "C" fn(ctx: *mut u8) -> i32>,
                             ctx: *mut u8) -> i32>,
}

#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<ShellService>() == SHELL_SERVICE_SIZE as usize);

extern "C" {
    /// CRT0 (`sdk/crt/crt0_c.c`) が控えた entry frame の 4 語目。
    static __os32_aux: *const ShellService;
}

/// `__os32_aux` の表の見立て。
pub enum ShellServiceState {
    /// 番地 0 (app)、印違い、または知らない新しい版 — 表を使わない。
    Absent,
    /// 版が `SHELL_SERVICE_VERSION` より古い (欄が足りない) — font は KCG だけ。
    OldShell,
    /// 印・版とも一致。
    Ready(&'static ShellService),
}

/// kernel から受け取った内部入口の表を見立てる。版が古い表は欄を読まない
/// (印と版の 2 語だけ読む)。
pub fn shell_service_state() -> ShellServiceState {
    let p = unsafe { core::ptr::read_volatile(core::ptr::addr_of!(__os32_aux)) };
    if p.is_null() {
        return ShellServiceState::Absent;
    }
    let (magic, version) = unsafe {
        (core::ptr::read_volatile(core::ptr::addr_of!((*p).magic)),
         core::ptr::read_volatile(core::ptr::addr_of!((*p).version)))
    };
    if magic != SHELL_SERVICE_MAGIC {
        return ShellServiceState::Absent;
    }
    if version < SHELL_SERVICE_VERSION {
        return ShellServiceState::OldShell;
    }
    if version != SHELL_SERVICE_VERSION {
        return ShellServiceState::Absent;
    }
    ShellServiceState::Ready(unsafe { &*p })
}

/// kernel から受け取った内部入口の表。番地が 0、印違い、版違い (古い・新しい) は `None`。
pub fn shell_service() -> Option<&'static ShellService> {
    match shell_service_state() {
        ShellServiceState::Ready(t) => Some(t),
        _ => None,
    }
}

#[cfg(test)]
#[path = "../host/shell_service_tests.rs"]
mod shell_service_tests;
