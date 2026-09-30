//! cfg.rs — 設定レジストリ (`/etc/settings.db`) の C ライブラリ `libos32cfg` への
//! **ABI 宣言の 1 か所** (票 `docs/archive/settings/TASK_S4.md` §4)。
//!
//! シグネチャと定数の**正典は `userland/lib/cfg/libos32cfg.h`** (S2)。ここは
//! その Rust 写しで、値を勝手に決めない。
//!
//! ```text
//!   gshell (Rust)            ─┐
//!   libos32gui::cfgro (shlib) ─┴─> ここ (extern "C") ──> libos32cfg.a (C)
//! ```
//!
//! 置き場をここに集めた理由 (票 §4):
//!
//! - 同じ ABI の宣言を 2 か所 (gshell と libos32gui) に置くと、片方だけ直った
//!   ときに**リンクは通るのに引数がずれる**。宣言が 1 か所なら食い違えない。
//! - `#[link]` 属性は**付けない**。リンクするライブラリを決めるのは
//!   `build/programs.mk` の側 (gshell は `libos32cfg.a` を直接、shlib は
//!   `libos32gui.shlib` の規則で) で、クレートが指図することではない。
//! - 属性の無い未使用の `extern "C"` 宣言は、**呼び出しもアドレス参照も無ければ
//!   未解決シンボルにならない**。だから `libos32cfg.a` を持たないホスト試験
//!   (`tools/tests/os32api_host.py`) や、この表の一部しか使わない利用者でも
//!   そのまま取り込める。
//!
//! このファイルは `os32api` の**他のモジュールに依存しない** — `core` だけを
//! 使う。`libos32gui` のホスト TDD (`host_tests`) が `#[path]` で直に取り込み、
//! `cfg_*` の贋物を並べて分岐を固定するため。

#![allow(dead_code)]

/* ================================================================ */
/*  状態 (`cfg_status`) — libos32cfg.h の CFG_*                      */
/* ================================================================ */

/// 開けて schema も一致した。**書けるのはこの状態のときだけ**。
pub const CFG_OK: i32 = 0;
/// DB が無い (fallback で動作)。
pub const CFG_MISSING: i32 = 1;
/// 0 バイト / NOTADB / CORRUPT / hot journal。
pub const CFG_CORRUPT: i32 = 2;
/// `schema_version` が自分より新しい (読みだけ)。
pub const CFG_VERSION: i32 = 3;
/// I/O 等 (詳細は `cfg_last_sqlite`)。
pub const CFG_ERROR: i32 = 4;

/* ================================================================ */
/*  値の型 (`settings.type`、DESIGN §3)                              */
/* ================================================================ */
pub const CFG_TYPE_INT: i32 = 0;
pub const CFG_TYPE_TEXT: i32 = 1;
pub const CFG_TYPE_BLOB: i32 = 2;
/// `cfg_get_info` の戻りに立つ「値の列が NULL」の印 (格納される型ではない)。
pub const CFG_INFO_NULL: i32 = 0x10;

/// `cfg_get_info` の戻りから宣言型を取り出す。
#[inline]
pub fn cfg_info_type(x: i32) -> i32 {
    x & 0x0F
}
/// `cfg_get_info` の戻りが「行はあるが値が NULL」か。
#[inline]
pub fn cfg_info_is_null(x: i32) -> bool {
    (x & CFG_INFO_NULL) != 0
}

/* ================================================================ */
/*  場所と版 ([C4] の管理元は libos32cfg.h。ここは写し)              */
/* ================================================================ */
/// `/etc/settings.db` (NUL 終端)。
pub const CFG_DB_PATH: &[u8] = b"/etc/settings.db\0";
/// `/etc/settings.tsv` (NUL 終端)。
pub const CFG_TSV_PATH: &[u8] = b"/etc/settings.tsv\0";
/// このビルドが読み書きできる schema の版。
pub const CFG_SCHEMA_VERSION: i32 = 1;

/* ================================================================ */
/*  値の上限 (S0_FOUNDATION §2-5)                                    */
/* ================================================================ */
/// scope の最大バイト数 (NUL を除く)。
pub const CFG_SCOPE_MAX: usize = 63;
/// key の最大バイト数 (NUL を除く)。
pub const CFG_KEY_MAX: usize = 63;
/// text 値の最大バイト数 (NUL を除く)。
pub const CFG_VALUE_TEXT_MAX: usize = 255;
/// blob 値の最大バイト数。
pub const CFG_BLOB_MAX: usize = 4096;

/* ================================================================ */
/*  接続ハンドル                                                     */
/* ================================================================ */

/// C の `CfgDb` (不透明)。中身はライブラリ側にしかない。
///
/// 1 プロセス 1 接続で、実体はライブラリの静的 1 本。**`static mut` に持たない**
/// — `cfg_open` 〜 `cfg_close` は 1 つの事象の中で閉じる (決裁: 同時 1 本、
/// 間に yield しない)。
#[repr(C)]
pub struct CfgDb {
    _opaque: [u8; 0],
}

/* ================================================================ */
/*  libos32cfg.h の関数 (i386 cdecl)                                 */
/*                                                                  */
/*  `const char *` は `*const u8`。C の `const CfgDb *` は `*const`  */
/*  で受ける (`*mut CfgDb` は呼び出し位置でそのまま弱められる)。      */
/* ================================================================ */
extern "C" {
    /* ---- 接続 ---- */
    /// `/etc/settings.db` を開く。0 = 開けた (状態は [`cfg_status`])、
    /// 負 = `OS32_ERR_*` (引数不正・二重 open)。
    pub fn cfg_open(out: *mut *mut CfgDb, writable: i32) -> i32;
    /// 未 commit は rollback してから close。0 / 負。
    pub fn cfg_close(db: *mut CfgDb) -> i32;
    /// 直近の `cfg_close` の失敗コード (0 = 無し)。close 後も読める。
    pub fn cfg_last_close_error() -> i32;
    /// DB が名乗る schema の版。
    pub fn cfg_schema_version(db: *const CfgDb) -> i32;
    /// `CFG_OK` / `CFG_MISSING` / `CFG_CORRUPT` / `CFG_VERSION` / `CFG_ERROR`。
    pub fn cfg_status(db: *const CfgDb) -> i32;
    /// 直近の SQLite コード (`CFG_ERROR` の詳細)。
    pub fn cfg_last_sqlite(db: *const CfgDb) -> i32;

    /* ---- 読み ---- */
    /// 「その key に何が入っているか」。0 以上 = 型 (+ [`CFG_INFO_NULL`])、
    /// 負 = `OS32_ERR_NOTFOUND` / `NOSYS` / `IO` / `INVAL`。
    pub fn cfg_get_info(db: *mut CfgDb, scope: *const u8, key: *const u8) -> i32;
    /// 成否を返す int 取得。0 = 取れた (`*out` に値) / 負。
    pub fn cfg_read_int(db: *mut CfgDb, scope: *const u8, key: *const u8, out: *mut i32) -> i32;
    /// 既定値つきの int 取得。**失敗も未設定も `def`** (負値も正当な設定値なので
    /// 戻り値では区別しない)。区別が要るなら [`cfg_read_int`]。
    pub fn cfg_get_int(db: *mut CfgDb, scope: *const u8, key: *const u8, def: i32) -> i32;
    /// 長さ (NUL 除く) / 負 (`NOTFOUND` / `NOSPC` / `INVAL`)。
    pub fn cfg_get_text(
        db: *mut CfgDb,
        scope: *const u8,
        key: *const u8,
        out: *mut u8,
        cap: i32,
    ) -> i32;
    /// blob 版。
    pub fn cfg_get_blob(
        db: *mut CfgDb,
        scope: *const u8,
        key: *const u8,
        out: *mut u8,
        cap: i32,
    ) -> i32;

    /* ---- 書き (writable かつ CFG_OK のときだけ) ---- */
    pub fn cfg_begin(db: *mut CfgDb) -> i32;
    pub fn cfg_set_int(db: *mut CfgDb, scope: *const u8, key: *const u8, v: i32) -> i32;
    pub fn cfg_set_text(db: *mut CfgDb, scope: *const u8, key: *const u8, s: *const u8) -> i32;
    pub fn cfg_commit(db: *mut CfgDb) -> i32;
    pub fn cfg_rollback(db: *mut CfgDb) -> i32;
}
