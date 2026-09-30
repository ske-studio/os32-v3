//! ffi.rs — os32api に無い libos32gfx / lib シンボルの extern 宣言。
//!
//! `os32api::gfx` は libos32gfx の描画関数の一部しか宣言していない (SDK 側は
//! C1/C2 の領分)。gshell がマウスカーソルの下地を退避・復元するのに要る
//! `gfx_get_pixel` と、FEP のセル幅判定に要る `unicode_to_ank` (lib/utf8_prog.o、
//! libos32gfx.a に同梱) をここで宣言する。**PM への申し送り**: 恒久的には
//! `sdk/rust/os32api/src/lib.rs` の `gfx` モジュールへ移すのが筋 (W レーンは
//! `userland/gshell/**` 以外を触らない約束なのでここに置いた)。

extern "C" {
    /// バックバッファの 1 画素を読む (libos32gfx.h)。
    pub fn gfx_get_pixel(x: i32, y: i32) -> u8;

    /// Unicode → ANK (半角) コード。0 なら全角 (`lib/utf8.c`)。
    pub fn unicode_to_ank(cp: u32) -> u8;
}
