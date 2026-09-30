//! cfgro.rs — 設定レジストリ (`/etc/settings.db`) への **OS 側で完結する** 窓口。
//!
//! 票 `docs/archive/settings/TASK_S2.md` §3 (ユーザー決裁 2026-09-13、2 回目)。
//!
//! ```text
//!   アプリ ──(ジャンプ表 101..=104)──> ここ ──(extern "C")──> libos32cfg.a (C)
//! ```
//!
//! 鉄則 (票 §3):
//! - **`CfgDb` も接続も呼び手に渡さない**。1 呼び出しの中で
//!   get: `cfg_open(&db,0)` → `cfg_get_*` → `cfg_close`
//!   set: `cfg_open(&db,1)` → `cfg_begin` → `cfg_set_*` → `cfg_commit`
//!        (失敗なら `cfg_rollback`) → `cfg_close`
//!   を完結させる。間に yield する呼び出しを置かない (協調型なので、これで
//!   接続は構造的に同時 1 本になる = FOUNDATION §2-4 の直列化)。
//! - **set は `app:[a-z0-9_]+` scope だけ**。`system` / `gshell` / `user` は
//!   S4 の設定 UI と `cfg` コマンドの領分なので拒否する。get は全 scope 可。
//! - 文字列は表の他のエントリと同じ **ptr + len** で受け、ここで private な
//!   NUL 終端バッファへ写してから C へ渡す (C 側は `const char *`)。
//!   呼び手が NUL を付け忘れても走査が飛ばない。
//!
//! **限界 (票 §3 に明記)**: 「自分の `app:` scope にしか書けない」束縛は無い。
//! アプリが自分の ID / 名前を知る KAPI が無いため、別アプリの `app:` scope にも
//! 書ける。scope を wrapper が取らない版へ寄せるかは S4 / S5 で決める。
//!
//! このファイルは **クレート内の他のモジュールに依存しない**。ホスト TDD
//! (`userland/rust/libos32gui/host_tests`) が `#[path]` で直に取り込み、
//! `cfg_*` の贋物を並べて分岐を固定するため。
#![allow(dead_code)]

/* ================================================================ */
/*  エラー番号 (正典: sdk/include/os32/os32_kapi_shared.h)            */
/*                                                                  */
/*  ここは自己完結のための写し。`shlib.rs` に os32api::gui::proto と  */
/*  一致することを見る const assert を置いてある (ずれたらビルドが    */
/*  止まる)。                                                        */
/* ================================================================ */

/// 入出力エラー。`cfg_close` が失敗したときに返す (`OS32_ERR_IO`)。
pub const ERR_IO: i32 = -1;
/// 値が無い (`OS32_ERR_NOTFOUND`)。`cfg_get_text` の素通し。
pub const ERR_NOTFOUND: i32 = -2;
/// 引数不正 (`OS32_ERR_INVAL`)。
pub const ERR_INVAL: i32 = -9;

/// scope 違反 (「OS32_ERR_PERM 相当」、票 §3)。
///
/// OS32 の `OS32_ERR_*` に **PERM は無い** (-1〜-14 が埋まり -15 以降は
/// ネットワーク予約)。新しい番号を勝手に取らない ([ABI2] と同じ精神) ので、
/// ここでは `ERR_INVAL` を充てる。番号を足すかは PM の決裁事項。
pub const ERR_PERM: i32 = ERR_INVAL;

/* ================================================================ */
/*  上限 (正典: 票 §1-3 の値の上限)                                   */
/* ================================================================ */

/// scope / key の最大バイト数 (NUL を除く)。
pub const CFG_NAME_MAX: usize = 63;
/// scope / key を写す private バッファ (NUL 込み)。
pub const CFG_NAME_CAP: usize = CFG_NAME_MAX + 1;
/// text 値の最大バイト数 (NUL を除く)。
pub const CFG_TEXT_MAX: usize = 255;
/// text 値を写す private バッファ (NUL 込み)。
pub const CFG_TEXT_CAP: usize = CFG_TEXT_MAX + 1;

/* ================================================================ */
/*  libos32cfg (C, userland/lib/cfg/libos32cfg.h) への宣言           */
/*                                                                  */
/*  **宣言そのものは `os32api::cfg` に 1 か所**へ寄せた (票 S4 §4)。   */
/*  gshell も同じ表を使うので、2 か所に置くと片方だけ直ったときに      */
/*  リンクは通るのに引数がずれる。ここは wrapper が使う 9 本だけを     */
/*  再公開する — 呼び手 (`crate::shlib` の表 101..=104) と            */
/*  ホスト TDD (`host_tests/src/fake.rs` の `crate::cfgro::CfgDb`)     */
/*  から見える名前は S2 のときと 1 つも変わらない。                   */
/*                                                                  */
/*  `crate::cfgabi` はクレート直下の別名:                             */
/*    - 本体 (libos32gui)  `pub use os32api::cfg as cfgabi;`          */
/*    - ホスト TDD         `#[path = ".../os32api/src/cfg.rs"]`       */
/*  こうしてあるので、このファイルは **os32api を名指ししない** =      */
/*  host_tests が os32api を丸ごと組まずに取り込める (S2 と同じ)。     */
/* ================================================================ */

pub use crate::cfgabi::CfgDb;

/* ---------------------------------------------------------------- */
/*  `kapi` — C 側 (cfg_backend.c) が見る KernelAPI ポインタ           */
/*                                                                  */
/*  アプリの .bin では `sdk/crt/crt0_c.c` が定義するが、**shlib には  */
/*  crt0 が無い**。libos32gfx が `libos32gfx_attach(api)` で自前に持つ */
/*  のと同じ理屈で、shlib 側はここで実体を供給する。                  */
/*                                                                  */
/*  shlib の `.data` / `.bss` はアプリごとの物理ページ (K3) なので、   */
/*  この 1 語もアプリごとに別。`os32gui_shlib_init(api)` が入れる。    */
/* ---------------------------------------------------------------- */

/// `cfg_backend.c` の `extern KernelAPI *kapi;` の実体 (shlib 側)。
///
/// 型は不透明ポインタで持つ (このファイルを os32api に依存させないため)。
///
/// **C 側の実名は `os32_kapi_v63`** (生成ヘッダの `#define kapi
/// OS32_KAPI_CRT_SYMBOL`、票 TASK_KAPI_DATA_FIELDS)。`sdk/kapi.json` の
/// `crt_kapi_symbol` と一致していることを `tools/check_kapi_version.py` が見る。
#[allow(non_upper_case_globals)]
#[export_name = "os32_kapi_v63"]
pub static mut kapi: *mut core::ffi::c_void = core::ptr::null_mut();

/// `os32gui_shlib_init` から KAPI を渡す。
#[inline]
pub fn set_kapi(api: *mut core::ffi::c_void) {
    unsafe { kapi = api };
}

/// `shlib_init` が済んでいるか。**済む前に `cfg_*` を呼ぶと NULL 参照**なので、
/// wrapper は必ずここを通してから open する。
#[inline]
pub fn kapi_ready() -> bool {
    unsafe { !kapi.is_null() }
}

pub use crate::cfgabi::{
    cfg_begin, cfg_close, cfg_commit, cfg_get_int, cfg_get_text, cfg_open, cfg_rollback,
    cfg_set_int, cfg_set_text,
};

/* ================================================================ */
/*  純粋部 — 分岐表と検査 (ホスト TDD がここを直接叩く)               */
/* ================================================================ */

/// ptr + len が生の引数として整合しているか (**スライスを作る前**の検査)。
///
/// レビュー往復 1 の ⑮: 検査を `from_raw_parts` の後に置くと
/// (a) `ptr = NULL, len = 1` が空スライスに化けて「空 text で既存値を上書き」
/// まで通り、(b) 非 NULL + 巨大 `len` が 63 / 255B の拒否より前に
/// `from_raw_parts` の前提を破る。だから生の段階で:
///
/// - `len` は `max` 以下 (scope / key は 63、text 値は 255)。
/// - NULL は **`len == 0` のときだけ**許す (「空」の意味。NULL + `len != 0` は
///   呼び手の誤りなので空スライスに化けさせない)。
/// - `ptr + len` が番地空間を折り返さない (`from_raw_parts` の前提)。
#[inline]
pub fn raw_span_ok(ptr: *const u8, len: u32, max: u32) -> bool {
    if len > max {
        return false;
    }
    if ptr.is_null() {
        return len == 0;
    }
    (ptr as usize).checked_add(len as usize).is_some()
}

/// 書き込み先 (`out` + `cap`) が生の引数として整合しているか。
///
/// `cap` は NUL 込みの大きさなので 1 以上。C へ `int` で渡すので `i32` に収まり、
/// `out + cap` が折り返さないことまで見る。
#[inline]
pub fn raw_out_ok(out: *const u8, cap: u32) -> bool {
    if out.is_null() || cap == 0 || cap > i32::MAX as u32 {
        return false;
    }
    (out as usize).checked_add(cap as usize).is_some()
}

/// [`raw_span_ok`] を通してから `&[u8]` を作る。通らなければ `None`。
///
/// `hostsvc.rs` も同じ検査を使うので `pub` (票 N4 §5、host_tests の `#[path]`
/// 取り込みのため)。
///
/// # Safety
/// `ptr` が非 NULL なら `len` バイト読めること (長さと折り返しはここで見る)。
#[inline]
pub unsafe fn checked_slice<'a>(ptr: *const u8, len: u32, max: u32) -> Option<&'a [u8]> {
    if !raw_span_ok(ptr, len, max) {
        return None;
    }
    if ptr.is_null() || len == 0 {
        Some(&[])
    } else {
        Some(core::slice::from_raw_parts(ptr, len as usize))
    }
}

/// scope / key の `max` (`raw_span_ok` へ渡す)。
const NAME_SPAN: u32 = CFG_NAME_MAX as u32;
/// text 値の `max`。
const TEXT_SPAN: u32 = CFG_TEXT_MAX as u32;

/// `src` を NUL 終端して `dst` へ写す。
///
/// 空・`dst` に入らない (NUL の 1 バイトを含めて)・埋め込み NUL は `false`。
/// 空を拒むのは scope / key が空の設定を作らせないため (票 §1-3)。
pub fn copy_cstr(src: &[u8], dst: &mut [u8]) -> bool {
    if src.is_empty() || src.len() >= dst.len() {
        return false;
    }
    let mut i = 0;
    while i < src.len() {
        if src[i] == 0 {
            return false;
        }
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = 0;
    true
}

/// text 値を NUL 終端して写す。**空値は許す** (票 §1-2: 空 text は長さ 0 の値で
/// NULL とは区別する)。埋め込み NUL と長すぎる値は `false`。
pub fn copy_value(src: &[u8], dst: &mut [u8]) -> bool {
    if src.len() >= dst.len() {
        return false;
    }
    let mut i = 0;
    while i < src.len() {
        if src[i] == 0 {
            return false;
        }
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = 0;
    true
}

/// set が受け付ける scope か (`app:[a-z0-9_]+`、票 §3)。
///
/// `system` / `gshell` / `user` はここで落ちる。長さは `CFG_NAME_MAX` まで。
pub fn scope_is_app(scope: &[u8]) -> bool {
    const PREFIX: &[u8] = b"app:";
    if scope.len() <= PREFIX.len() || scope.len() > CFG_NAME_MAX {
        return false;
    }
    let mut i = 0;
    while i < PREFIX.len() {
        if scope[i] != PREFIX[i] {
            return false;
        }
        i += 1;
    }
    while i < scope.len() {
        let c = scope[i];
        let ok = (c >= b'a' && c <= b'z') || (c >= b'0' && c <= b'9') || c == b'_';
        if !ok {
            return false;
        }
        i += 1;
    }
    true
}

/// `os32gui_cfg_get_int` の分岐表。
///
/// open 失敗 / close 失敗のどちらでも `def`。`cfg_get_int` 自身は値か `def` しか
/// 返さない (負値も正当な設定値なので、失敗を戻り値で区別しない)。
#[inline]
pub fn fold_get_int(open_rc: i32, value: i32, close_rc: i32, def: i32) -> i32 {
    if open_rc < 0 || close_rc < 0 {
        def
    } else {
        value
    }
}

/// `os32gui_cfg_get_text` の分岐表。
///
/// **直前の失敗を優先** (票 §3): open が負ならその値、`cfg_get_text` が負
/// (`NOTFOUND` / `NOSPC` / `INVAL`) ならそのまま、どちらも成功して close だけ
/// 失敗したら `ERR_IO`。成功なら長さ。
#[inline]
pub fn fold_get_text(open_rc: i32, get_rc: i32, close_rc: i32) -> i32 {
    if open_rc < 0 {
        return open_rc;
    }
    if get_rc < 0 {
        return get_rc;
    }
    if close_rc < 0 {
        return ERR_IO;
    }
    get_rc
}

/// `os32gui_cfg_set_*` の分岐表。
///
/// `work_rc` は begin / set / commit のうち**最初に失敗した**コード (すべて成功
/// なら 0)。**直前の失敗を優先** (票 §3): open → work → close の順に見る。
/// MISSING / CORRUPT / VERSION は `cfg_begin` が `INVAL` を返すのでここに乗る。
#[inline]
pub fn fold_set(open_rc: i32, work_rc: i32, close_rc: i32) -> i32 {
    if open_rc < 0 {
        return open_rc;
    }
    if work_rc < 0 {
        return work_rc;
    }
    if close_rc < 0 {
        return ERR_IO;
    }
    0
}

/* ================================================================ */
/*  101..=104: ジャンプ表のエントリ                                   */
/* ================================================================ */

/// RO で開く。`0` 以上なら `db` は非 NULL であることまで見る。
///
/// # Safety
/// `cfg_open` を呼ぶ。
#[inline]
unsafe fn open_checked(db: *mut *mut CfgDb, writable: i32) -> i32 {
    let rc = cfg_open(db, writable);
    if rc >= 0 && (*db).is_null() {
        /* 0 を返しながら NULL を置くのはライブラリの契約違反。閉じる相手が
         * 無いのでここで打ち切る。 */
        return ERR_IO;
    }
    rc
}

/// `os32gui_cfg_get_int(scope, key, def)` — 1 呼び出しで open → get → close。
///
/// 戻り: 設定値。open 失敗 / 未設定 / close 失敗のどれでも `def` (票 §3)。
#[no_mangle]
pub extern "C" fn os32gui_cfg_get_int(
    scope: *const u8,
    scope_len: u32,
    key: *const u8,
    key_len: u32,
    def: i32,
) -> i32 {
    if !kapi_ready() {
        /* `shlib_init` 前 — C の backend は NULL の kapi を辿ってしまう。 */
        return def;
    }
    let (sc, k) = match unsafe {
        (
            checked_slice(scope, scope_len, NAME_SPAN),
            checked_slice(key, key_len, NAME_SPAN),
        )
    } {
        (Some(a), Some(b)) => (a, b),
        _ => return def,
    };
    let mut sbuf = [0u8; CFG_NAME_CAP];
    let mut kbuf = [0u8; CFG_NAME_CAP];
    if !copy_cstr(sc, &mut sbuf) || !copy_cstr(k, &mut kbuf) {
        return def;
    }
    let mut db: *mut CfgDb = core::ptr::null_mut();
    let orc = unsafe { open_checked(&mut db, 0) };
    if orc < 0 {
        return fold_get_int(orc, def, 0, def);
    }
    let v = unsafe { cfg_get_int(db, sbuf.as_ptr(), kbuf.as_ptr(), def) };
    let crc = unsafe { cfg_close(db) };
    fold_get_int(orc, v, crc, def)
}

/// `os32gui_cfg_get_text(scope, key, out, cap)` — 1 呼び出しで open → get → close。
///
/// 戻り: 長さ (NUL を除く) / 負。`cap` は NUL を含む `out` の大きさ。
#[no_mangle]
pub extern "C" fn os32gui_cfg_get_text(
    scope: *const u8,
    scope_len: u32,
    key: *const u8,
    key_len: u32,
    out: *mut u8,
    cap: u32,
) -> i32 {
    if !kapi_ready() || !raw_out_ok(out, cap) {
        return ERR_INVAL;
    }
    let (sc, k) = match unsafe {
        (
            checked_slice(scope, scope_len, NAME_SPAN),
            checked_slice(key, key_len, NAME_SPAN),
        )
    } {
        (Some(a), Some(b)) => (a, b),
        _ => return ERR_INVAL,
    };
    let mut sbuf = [0u8; CFG_NAME_CAP];
    let mut kbuf = [0u8; CFG_NAME_CAP];
    if !copy_cstr(sc, &mut sbuf) || !copy_cstr(k, &mut kbuf) {
        return ERR_INVAL;
    }
    let mut db: *mut CfgDb = core::ptr::null_mut();
    let orc = unsafe { open_checked(&mut db, 0) };
    if orc < 0 {
        return fold_get_text(orc, 0, 0);
    }
    let n = unsafe { cfg_get_text(db, sbuf.as_ptr(), kbuf.as_ptr(), out, cap as i32) };
    let crc = unsafe { cfg_close(db) };
    fold_get_text(orc, n, crc)
}

/// begin → set → commit / rollback を 1 つの txn で閉じ、最初の失敗コードを返す。
///
/// `db` は `cfg_open(.., 1)` が成功して返した接続であること (呼び出しは
/// このファイルの中だけ)。`set` は `cfg_set_*` を 1 本呼ぶ。
fn write_txn<F>(db: *mut CfgDb, set: F) -> i32
where
    F: FnOnce(*mut CfgDb) -> i32,
{
    let brc = unsafe { cfg_begin(db) };
    if brc < 0 {
        /* BEGIN していないので rollback しない (MISSING / CORRUPT / VERSION
         * と読み専用接続はここで INVAL)。 */
        return brc;
    }
    let src = set(db);
    if src < 0 {
        unsafe { cfg_rollback(db) };
        return src;
    }
    let crc = unsafe { cfg_commit(db) };
    if crc < 0 {
        /* 失敗コードは rollback を実行する**前**の値 (票 §1-4)。 */
        unsafe { cfg_rollback(db) };
        return crc;
    }
    0
}

/// `os32gui_cfg_set_int(scope, key, v)` — 1 呼び出しで open → begin → set →
/// commit → close。`app:[a-z0-9_]+` scope のみ。
///
/// 戻り: 0 / 負 (票 §3)。
#[no_mangle]
pub extern "C" fn os32gui_cfg_set_int(
    scope: *const u8,
    scope_len: u32,
    key: *const u8,
    key_len: u32,
    v: i32,
) -> i32 {
    if !kapi_ready() {
        return ERR_INVAL;
    }
    let sc = match unsafe { checked_slice(scope, scope_len, NAME_SPAN) } {
        Some(a) => a,
        None => return ERR_INVAL,
    };
    if !scope_is_app(sc) {
        return ERR_PERM;
    }
    let k = match unsafe { checked_slice(key, key_len, NAME_SPAN) } {
        Some(a) => a,
        None => return ERR_INVAL,
    };
    let mut sbuf = [0u8; CFG_NAME_CAP];
    let mut kbuf = [0u8; CFG_NAME_CAP];
    if !copy_cstr(sc, &mut sbuf) || !copy_cstr(k, &mut kbuf) {
        return ERR_INVAL;
    }
    let mut db: *mut CfgDb = core::ptr::null_mut();
    let orc = unsafe { open_checked(&mut db, 1) };
    if orc < 0 {
        return fold_set(orc, 0, 0);
    }
    let work = write_txn(db, |d| unsafe {
        cfg_set_int(d, sbuf.as_ptr(), kbuf.as_ptr(), v)
    });
    let crc = unsafe { cfg_close(db) };
    fold_set(orc, work, crc)
}

/// `os32gui_cfg_set_text(scope, key, s)` — `set_int` と同じ道筋の text 版。
///
/// `s` は ptr + len。空値は許す (NULL とは違う値として入る)。
#[no_mangle]
pub extern "C" fn os32gui_cfg_set_text(
    scope: *const u8,
    scope_len: u32,
    key: *const u8,
    key_len: u32,
    s: *const u8,
    s_len: u32,
) -> i32 {
    if !kapi_ready() {
        return ERR_INVAL;
    }
    let sc = match unsafe { checked_slice(scope, scope_len, NAME_SPAN) } {
        Some(a) => a,
        None => return ERR_INVAL,
    };
    if !scope_is_app(sc) {
        return ERR_PERM;
    }
    /* 値は NULL + len 0 だけが「空値」。NULL + len != 0 はここで落とす
     * (空スライスに化けると既存値を空 text で上書きしてしまう)。 */
    let (k, val) = match unsafe {
        (
            checked_slice(key, key_len, NAME_SPAN),
            checked_slice(s, s_len, TEXT_SPAN),
        )
    } {
        (Some(a), Some(b)) => (a, b),
        _ => return ERR_INVAL,
    };
    let mut sbuf = [0u8; CFG_NAME_CAP];
    let mut kbuf = [0u8; CFG_NAME_CAP];
    let mut vbuf = [0u8; CFG_TEXT_CAP];
    if !copy_cstr(sc, &mut sbuf) || !copy_cstr(k, &mut kbuf) || !copy_value(val, &mut vbuf) {
        return ERR_INVAL;
    }
    let mut db: *mut CfgDb = core::ptr::null_mut();
    let orc = unsafe { open_checked(&mut db, 1) };
    if orc < 0 {
        return fold_set(orc, 0, 0);
    }
    let work = write_txn(db, |d| unsafe {
        cfg_set_text(d, sbuf.as_ptr(), kbuf.as_ptr(), vbuf.as_ptr())
    });
    let crc = unsafe { cfg_close(db) };
    fold_set(orc, work, crc)
}
