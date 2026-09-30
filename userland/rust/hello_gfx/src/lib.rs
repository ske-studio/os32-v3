/*
 * hello_gfx — GFX "Hello World!" テストプログラム (Rust)
 *
 * OS32外部プログラムとしてRustで記述した最初のGFXデモ。
 * os32api クレート経由で KernelAPI と libos32gfx (C) を利用する。
 */
#![no_std]
#![no_main]

extern crate os32api;

use os32api::gfx;
use os32api::KernelAPI;

/* ================================================================ */
/*  メイン関数                                                       */
/*                                                                  */
/*  crt0_c.c から main(argc, argv, api) として呼ばれる。            */
/*  GFXモードで "Hello World!" を描画し、キー待ちで終了。           */
/* ================================================================ */
#[no_mangle]
pub extern "C" fn main(
    _argc: i32,
    _argv: *const *const u8,
    api: *mut KernelAPI,
) -> i32 {
    /* os32api の初期化 (グローバルAPIポインタ + アロケータ) */
    os32api::os32_init(api);

    unsafe {
        let a = os32api::api();

        /* GFXモード初期化 (400ラインモード) */
        gfx::libos32gfx_init(api);

        /* 背景をクリア (色0 = 黒) */
        gfx::gfx_clear(0);

        /* 背景装飾: カラーバー */
        gfx::gfx_fill_rect(0, 0,   640, 60, 8);   /* 赤 */
        gfx::gfx_fill_rect(0, 60,  640, 60, 10);  /* 黄 */
        gfx::gfx_fill_rect(0, 120, 640, 60, 11);  /* 緑 */
        gfx::gfx_fill_rect(0, 180, 640, 60, 12);  /* 水色 */
        gfx::gfx_fill_rect(0, 240, 640, 60, 13);  /* 青 */
        gfx::gfx_fill_rect(0, 300, 640, 100, 1);  /* 暗い青 */

        /* 中央に黒背景の矩形パネル */
        gfx::gfx_fill_rect(120, 130, 400, 140, 0);
        gfx::gfx_rect(118, 128, 404, 144, 7);
        gfx::gfx_rect(116, 126, 408, 148, 6);

        /* テキスト描画: "Hello World!" (拡大) */
        gfx::kcg_set_scale(2);
        gfx::kcg_draw_utf8(172, 160, b"Hello World!\0".as_ptr(), 7, 0);

        /* テキスト描画: "Rust on OS32 / PC-9801" (通常サイズ) */
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(208, 220, b"Rust on OS32 / PC-9801\0".as_ptr(), 6, 0);

        /* VRAM転送 */
        (a.gfx_add_dirty_rect)(0, 0, 640, 400);
        (a.gfx_present_dirty)();

        /* キー入力待ち */
        (a.kbd_getchar)();

        /* GFXモード終了 → テキスト画面復帰 */
        (a.gfx_shutdown)();
        (a.tvram_clear)();
    }
    0
}
