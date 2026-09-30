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

/// キー入力を待つ (ブロッキング)
pub fn wait_key() -> i32 {
    unsafe { (api().kbd_getchar)() }
}

/// キー入力を試みる (ノンブロッキング、入力なし時は -1)
pub fn try_key() -> Option<i32> {
    let k = unsafe { (api().kbd_trygetchar)() };
    if k > 0 { Some(k) } else { None }
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
struct Os32Alloc;

unsafe impl GlobalAlloc for Os32Alloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let api = &**API.0.get();
        let ptr = (api.mem_alloc)(layout.size() as u32);
        if ptr.is_null() {
            (api.kprintf)(
                ATTR_RED,
                b"[OOM] alloc failed: size=%d, align=%d\r\n\0".as_ptr(),
                layout.size() as i32,
                layout.align() as i32,
            );
        }
        ptr
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        let api = &**API.0.get();
        (api.mem_free)(ptr);
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
        pub fn libos32gfx_shutdown();
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
            (a.gfx_shutdown)();
            (a.tvram_clear)();
        }
    }

    /// バックバッファをVRAMに転送する (全画面)
    pub fn present() {
        unsafe {
            let a = super::api();
            (a.gfx_add_dirty_rect)(0, 0, 640, 400);
            (a.gfx_present_dirty)();
        }
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

