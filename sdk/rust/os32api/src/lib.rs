/*
 * os32api — OS32 Rust外部プログラム共通クレート
 *
 * 全Rustプログラムが依存する共通インフラ:
 * - KernelAPI バインディング (kapi_generated.rs, 自動生成)
 * - グローバルアロケータ (mem_alloc/mem_free ベース)
 * - パニックハンドラ
 * - 安全ラッパーマクロ / ヘルパー関数
 */
#![no_std]

extern crate alloc;

/* kapi_rust_gen.py で自動生成されたKernelAPIバインディング */
pub mod kapi_generated;
pub use kapi_generated::*;

/* GUI シェル v1.1 共有定義 (proto / types)。C レーン (C1) 所有。
 * 値の正典は sdk/include/os32/os32_gui_shared.h。 */
pub mod gui;

/* 設定レジストリ (libos32cfg) の ABI 宣言 (票 S4 §4)。
 * シグネチャの正典は userland/lib/cfg/libos32cfg.h。`#[link]` は付けない。 */
pub mod cfg;

/* Host Services (libos32host) の ABI 宣言 (票 N4 §1)。
 * シグネチャの正典は userland/lib/host/libos32host.h。`#[link]` は付けない。 */
pub mod host;
pub mod ls;
pub use ls::os32_ls;

use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::panic::PanicInfo;

/* ================================================================ */
/*  グローバル KernelAPI ポインタ                                     */
/* ================================================================ */
struct ApiHolder(UnsafeCell<*mut KernelAPI>);
unsafe impl Sync for ApiHolder {}

static API: ApiHolder = ApiHolder(UnsafeCell::new(core::ptr::null_mut()));

/// KernelAPIポインタを初期化する (main関数の冒頭で呼ぶこと)
pub fn os32_init(api: *mut KernelAPI) {
    unsafe {
        *API.0.get() = api;
    }
}

/// グローバルKernelAPIへの参照を取得する
///
/// # Safety
/// os32_init() が呼ばれた後にのみ使用可能。
#[inline]
pub unsafe fn api() -> &'static KernelAPI {
    &**API.0.get()
}

/// グローバルKernelAPIへの可変ポインタを取得する
#[inline]
pub unsafe fn api_ptr() -> *mut KernelAPI {
    *API.0.get()
}

/* ================================================================ */
/*  コンソール出力ヘルパー                                           */
/* ================================================================ */

/// コンソールに文字列を出力する (白色)
pub fn print(s: &[u8]) {
    unsafe {
        let a = api();
        (a.kprintf)(ATTR_WHITE, b"%s\0".as_ptr(), s.as_ptr());
    }
}

/// コンソールに文字列を出力する (色指定)
pub fn print_attr(attr: u8, s: &[u8]) {
    unsafe {
        let a = api();
        (a.kprintf)(attr, b"%s\0".as_ptr(), s.as_ptr());
    }
}

/// コンソールに整数を出力する
pub fn print_int(attr: u8, val: i32) {
    unsafe {
        let a = api();
        (a.kprintf)(attr, b"%d\0".as_ptr(), val);
    }
}

/// kprintf を直接呼ぶマクロ (フォーマット文字列 + 引数)
///
/// 使用例:
/// ```
/// kprint!(b"Hello %s, tick=%d\r\n\0", name_ptr, tick);
/// ```
#[macro_export]
macro_rules! kprint {
    ($fmt:expr $(, $arg:expr)*) => {
        unsafe {
            let a = $crate::api();
            (a.kprintf)($crate::ATTR_WHITE, $fmt.as_ptr() $(, $arg)*);
        }
    };
}

/// 色付き kprintf マクロ
#[macro_export]
macro_rules! kprint_attr {
    ($attr:expr, $fmt:expr $(, $arg:expr)*) => {
        unsafe {
            let a = $crate::api();
            (a.kprintf)($attr, $fmt.as_ptr() $(, $arg)*);
        }
    };
}

/* ================================================================ */
/*  キーボード入力ヘルパー                                           */
/* ================================================================ */

// Bound shlib users may park in WAIT_KEY/WAIT_POLL, outside OP_WAIT.
// Reattach before returning to their drawing code, preserving the key result.
fn input_return() {
    if gui::stub::is_bound() { let _ = gui::stub::check_gfx(); }
}

/// キー入力を待つ (ブロッキング)
pub fn wait_key() -> i32 {
    let result = unsafe { (api().kbd_getchar)() };
    input_return();
    result
}

/// キー入力を試みる (ノンブロッキング、入力なし時は -1)
pub fn try_key() -> Option<i32> {
    let k = unsafe { (api().kbd_trygetchar)() };
    if k > 0 {
        input_return();
        Some(k)
    } else { None }
}

/* ================================================================ */
/*  システムヘルパー                                                 */
/* ================================================================ */

/// 現在のティックカウントを取得する (1tick = 10ms)
pub fn get_tick() -> u32 {
    unsafe { (api().get_tick)() }
}

/* ================================================================ */
/*  グローバルアロケータ                                             */
/* ================================================================ */
// OS32 has one cooperative task per address space; these cells need no locks.
struct RetryCell<T>(core::cell::Cell<T>);
unsafe impl<T> Sync for RetryCell<T> {}
static GUI_RETRY: RetryCell<bool> = RetryCell(core::cell::Cell::new(false));
static IN_TRIM: RetryCell<bool> = RetryCell(core::cell::Cell::new(false));
static RETRY_COUNT: RetryCell<u32> = RetryCell(core::cell::Cell::new(0));
static RETRY_OK_COUNT: RetryCell<u32> = RetryCell(core::cell::Cell::new(0));
static HOOK_PAGES_TOTAL: RetryCell<u32> = RetryCell(core::cell::Cell::new(0));

/// Enable one allocation retry after successful GUI OP_INIT.
pub fn retry_enable() { GUI_RETRY.0.set(true); }
/// App-side trampoline brackets its optional hook with this guard.
pub fn trim_enter() { IN_TRIM.0.set(true); }
pub fn trim_leave(pages: u32) {
    HOOK_PAGES_TOTAL.0.set(HOOK_PAGES_TOTAL.0.get().wrapping_add(pages));
    IN_TRIM.0.set(false);
}
/// Observation only: retries, successful retries, hook page hints.
pub fn retry_stats() -> (u32, u32, u32) {
    (RETRY_COUNT.0.get(), RETRY_OK_COUNT.0.get(), HOOK_PAGES_TOTAL.0.get())
}

struct Os32Alloc;

#[repr(C, align(8))]
struct AllocPrefix {
    base: *mut u8,
    payload: *mut u8,
}

unsafe impl GlobalAlloc for Os32Alloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Preserve OS32's size-zero NULL contract, without calling mem_alloc(0).
        if layout.size() == 0 { return core::ptr::null_mut(); }
        let size = match u32::try_from(layout.size()) {
            Ok(n) => n, Err(_) => return core::ptr::null_mut(),
        };
        if !layout.align().is_power_of_two() { return core::ptr::null_mut(); }
        let align = layout.align().max(core::mem::align_of::<AllocPrefix>());
        let align32 = match u32::try_from(align) {
            Ok(n) => n, Err(_) => return core::ptr::null_mut(),
        };
        let prefix = core::mem::size_of::<AllocPrefix>();
        let bytes = match size.checked_add(align32 - 1)
            .and_then(|n| n.checked_add(prefix as u32)) {
            Some(n) => n, None => return core::ptr::null_mut(),
        };
        let mut base = (api().mem_alloc)(bytes);
        if base.is_null() && GUI_RETRY.0.get() && !IN_TRIM.0.get() {
            (api().sys_yield)();
            RETRY_COUNT.0.set(RETRY_COUNT.0.get().wrapping_add(1));
            base = (api().mem_alloc)(bytes);
            if !base.is_null() {
                RETRY_OK_COUNT.0.set(RETRY_OK_COUNT.0.get().wrapping_add(1));
            }
        }
        if base.is_null() { return base; }
        // Offset arithmetic keeps both prefix and payload in the raw block.
        let offset = prefix + (align - ((base as usize + prefix) & (align - 1))) % align;
        let ptr = base.add(offset);
        ptr.sub(prefix).cast::<AllocPrefix>().write(AllocPrefix { base, payload: ptr });
        ptr
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        if ptr.is_null() { return; }
        let prefix = ptr.sub(core::mem::size_of::<AllocPrefix>()).cast::<AllocPrefix>();
        (api().mem_free)((*prefix).base);
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let ptr = self.alloc(layout);
        if !ptr.is_null() { ptr.write_bytes(0, layout.size()); }
        ptr
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        let next_layout = match Layout::from_size_align(new_size, layout.align()) {
            Ok(l) => l, Err(_) => return core::ptr::null_mut(),
        };
        let next = self.alloc(next_layout);
        if !next.is_null() {
            core::ptr::copy_nonoverlapping(ptr, next, layout.size().min(new_size));
            self.dealloc(ptr, layout);
        }
        next
    }
}

#[global_allocator]
static ALLOCATOR: Os32Alloc = Os32Alloc;

/* ================================================================ */
/*  パニックハンドラ                                                 */
/* ================================================================ */
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    unsafe {
        let p = API.0.get();
        if !(*p).is_null() {
            let a = &**p;
            (a.kprintf)(
                ATTR_RED,
                b"[PANIC] Rust program panicked!\r\n\0".as_ptr(),
            );
            /* パニック位置を表示 */
            if let Some(loc) = info.location() {
                /* ファイル名を表示 (Rustの&strは NUL終端でないため %.*s 使用) */
                let file = loc.file();
                let line = loc.line();
                (a.kprintf)(
                    ATTR_RED,
                    b"  at %.*s:%d\r\n\0".as_ptr(),
                    file.len() as i32,
                    file.as_ptr(),
                    line as i32,
                );
            }
            /* ヒープ残量も表示 (OOM判定用) */
            let free = (a.kmalloc_free)();
            (a.kprintf)(
                ATTR_YELLOW,
                b"  kmalloc_free=%d bytes\r\n\0".as_ptr(),
                free as i32,
            );
        }
    }
    loop {}
}

/* ================================================================ */
/*  ファイルI/O ヘルパー                                             */
/* ================================================================ */
pub mod fs {
    use alloc::vec::Vec;

    /* sys_open の mode 定数 */
    pub const O_RDONLY: i32 = 0;

    /* sys_lseek の whence 定数 */
    pub const SEEK_SET: i32 = 0;
    pub const SEEK_END: i32 = 2;

    /// ファイルを開いて全内容を Vec<u8> に読み込む
    ///
    /// パスはNUL終端のバイト列で渡すこと (例: b"/data/font.ttf\0")
    /// 失敗時は None を返す。
    pub fn read_file(path: &[u8]) -> Option<Vec<u8>> {
        unsafe {
            let a = crate::api();

            /* ファイルを開く */
            let fd = (a.sys_open)(path.as_ptr(), O_RDONLY);
            if fd < 0 {
                return None;
            }

            /* ファイルサイズを取得 (SEEK_END → SEEK_SET) */
            let size = (a.sys_lseek)(fd, 0, SEEK_END);
            if size <= 0 {
                (a.sys_close)(fd);
                return None;
            }
            (a.sys_lseek)(fd, 0, SEEK_SET);

            /* バッファを確保して読み込む */
            let size_u = size as usize;
            let mut buf: Vec<u8> = Vec::with_capacity(size_u);
            buf.set_len(size_u);

            let mut total_read: usize = 0;
            while total_read < size_u {
                let chunk = size_u - total_read;
                let n = (a.sys_read)(
                    fd,
                    buf.as_mut_ptr().add(total_read),
                    chunk as u32,
                );
                if n <= 0 {
                    break;
                }
                total_read += n as usize;
            }

            (a.sys_close)(fd);

            if total_read < size_u {
                buf.truncate(total_read);
            }

            Some(buf)
        }
    }
}

/* ================================================================ */
/*  libos32gfx (C) 外部関数宣言                                     */
/* ================================================================ */
pub mod gfx {
    use super::KernelAPI;

    extern "C" {
        pub fn libos32gfx_init(api: *mut KernelAPI);
        pub fn libos32gfx_attach(api: *mut KernelAPI);
        pub fn libos32gfx_shutdown();
        fn libos32gfx_check() -> i32;
        fn libos32gfx_detach();
        fn gfx_present();
        static mut gfx_ready: i32;
        pub fn gfx_clear(color: u8);
        pub fn gfx_pixel(x: i32, y: i32, color: u8);
        pub fn gfx_hline(x: i32, y: i32, w: i32, color: u8);
        pub fn gfx_vline(x: i32, y: i32, h: i32, color: u8);
        pub fn gfx_line(x0: i32, y0: i32, x1: i32, y1: i32, color: u8);
        pub fn gfx_rect(x: i32, y: i32, w: i32, h: i32, color: u8);
        pub fn gfx_fill_rect(x: i32, y: i32, w: i32, h: i32, color: u8);
        pub fn gfx_circle(cx: i32, cy: i32, r: i32, color: u8);
        pub fn gfx_fill_circle(cx: i32, cy: i32, r: i32, color: u8);
        pub fn kcg_set_scale(scale: i32);
        pub fn kcg_draw_utf8(x: i32, y: i32, s: *const u8, fg: u8, bg: u8) -> i32;
    }

    /* GFX 安全ラッパー */

    /// GFXモードを初期化する (400ラインモード)
    pub fn init() {
        unsafe {
            libos32gfx_init(super::api_ptr());
        }
    }

    /// GFXモードを終了しテキスト画面に復帰する
    pub fn shutdown() {
        unsafe {
            let a = super::api();
            libos32gfx_shutdown();
            (a.tvram_clear)();
        }
    }

    /// バックバッファをVRAMに転送する (全画面)
    pub fn present() {
        unsafe {
            gfx_present();
            if core::ptr::read_volatile(core::ptr::addr_of!(gfx_ready)) != 0 {
                (super::api().gfx_present_dirty)();
            }
        }
    }

    /// Check this static instance after a wait / explicit reinitialization.
    pub fn check() -> i32 {
        let rc = unsafe { libos32gfx_check() };
        if rc < 0 { detach(); }
        rc
    }

    /// Release only this static instance's CLIENT view.
    pub fn detach() {
        unsafe { libos32gfx_detach() };
    }

    /// 画面をクリアしてVRAMに転送する
    pub fn clear_and_present(color: u8) {
        unsafe {
            gfx_clear(color);
        }
        present();
    }

    /// テキストを描画する (UTF-8, NUL終端)
    pub fn draw_text(x: i32, y: i32, text: &[u8], fg: u8, bg: u8) {
        unsafe {
            kcg_draw_utf8(x, y, text.as_ptr(), fg, bg);
        }
    }

    /// テキストスケールを設定する (1=通常, 2=倍角)
    pub fn set_text_scale(scale: i32) {
        unsafe {
            kcg_set_scale(scale);
        }
    }
}


pub mod generations;
