//! fake.rs — C の `libos32cfg` の贋物 (ホスト TDD 専用)。
//!
//! `cfgro.rs` が `extern "C"` で宣言している 9 本をここで定義して、試験の
//! リンクを閉じる。**実 DB も SQLite も出てこない** — 記録するのは呼び順と
//! 引数、返すのは試験が仕込んだ戻り値だけ。
//!
//! 状態は `thread_local!` なので、cargo の並列試験でも試験どうしが混ざらない。

use std::cell::RefCell;
use std::sync::Once;

/// `cfgro` 側と同じ不透明ハンドル型 (宣言と定義の食い違いを作らない)。
use crate::cfgro::CfgDb as Db;

/// `cfgro` が libos32cfg を叩いた記録。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Call {
    /// `cfg_open(&db, writable)`
    Open(i32),
    GetInt,
    GetText,
    Begin,
    SetInt,
    SetText,
    Commit,
    Rollback,
    Close,
}

#[derive(Default)]
struct State {
    /* 仕込み */
    open_rc: i32,
    open_null: bool,
    get_int_rc: Option<i32>,
    get_int_def: bool,
    get_text_val: Option<Vec<u8>>,
    get_text_rc: Option<i32>,
    begin_rc: i32,
    set_rc: i32,
    commit_rc: i32,
    rollback_rc: i32,
    close_rc: i32,
    /* 記録 */
    log: Vec<Call>,
    last_scope: Vec<u8>,
    last_key: Vec<u8>,
    last_text: Vec<u8>,
    last_int: i32,
    last_cap: i32,
}

thread_local! {
    static ST: RefCell<State> = RefCell::new(State::default());
}

fn with<R>(f: impl FnOnce(&mut State) -> R) -> R {
    ST.with(|s| f(&mut s.borrow_mut()))
}

/* ---------------- 試験から使う口 ---------------- */

/// 仕込みと記録を白紙に戻す。各試験の先頭で呼ぶ。
///
/// ついでに wrapper の「`shlib_init` 前の門」を開ける (`kapi` を非 NULL に
/// しておく)。**init 前の分岐は別プロセスの `tests/init_gate.rs`** で見る —
/// `kapi` はスレッドをまたぐ 1 語なので、並列試験の中で NULL に戻せない。
///
/// `kapi` への書きは [`Once`] で **プロセスに 1 回だけ**にする。毎回書くと、
/// 同じ値でも並列試験どうしが同期なしで共有 `static mut` を叩く競合になる
/// (レビュー往復 2 の non-blocker)。`Once` は書きを直列化し、他のスレッドは
/// その完了を待ってから読む。
pub fn reset() {
    static KAPI_ONCE: Once = Once::new();
    KAPI_ONCE.call_once(|| {
        crate::cfgro::set_kapi((&raw mut DUMMY_DB) as *mut std::ffi::c_void);
    });
    with(|s| *s = State::default());
}

pub fn set_open_rc(rc: i32) {
    with(|s| s.open_rc = rc);
}
/// `cfg_open` が 0 を返しながら NULL を置く (ライブラリの契約違反) 模型。
pub fn set_open_null(v: bool) {
    with(|s| s.open_null = v);
}
pub fn set_get_int(v: i32) {
    with(|s| s.get_int_rc = Some(v));
}
/// 未設定 / MISSING / CORRUPT — `cfg_get_int` は呼び手の `def` をそのまま返す。
pub fn set_get_int_passthrough_def(v: bool) {
    with(|s| s.get_int_def = v);
}
pub fn set_get_text(val: &[u8]) {
    with(|s| s.get_text_val = Some(val.to_vec()));
}
pub fn set_get_text_rc(rc: i32) {
    with(|s| s.get_text_rc = Some(rc));
}
pub fn set_begin_rc(rc: i32) {
    with(|s| s.begin_rc = rc);
}
pub fn set_set_rc(rc: i32) {
    with(|s| s.set_rc = rc);
}
pub fn set_commit_rc(rc: i32) {
    with(|s| s.commit_rc = rc);
}
pub fn set_close_rc(rc: i32) {
    with(|s| s.close_rc = rc);
}

pub fn log() -> Vec<Call> {
    with(|s| s.log.clone())
}
pub fn last_scope() -> Vec<u8> {
    with(|s| s.last_scope.clone())
}
pub fn last_key() -> Vec<u8> {
    with(|s| s.last_key.clone())
}
pub fn last_text() -> Vec<u8> {
    with(|s| s.last_text.clone())
}
pub fn last_int() -> i32 {
    with(|s| s.last_int)
}
pub fn last_cap() -> i32 {
    with(|s| s.last_cap)
}

/* ---------------- C 側の贋物 ---------------- */

/// `cfg_open` が返す「接続」。中身は見ないので番地だけあればよい。
static mut DUMMY_DB: u8 = 0;

unsafe fn cstr(p: *const u8) -> Vec<u8> {
    if p.is_null() {
        return Vec::new();
    }
    let mut v = Vec::new();
    let mut i = 0isize;
    while *p.offset(i) != 0 {
        v.push(*p.offset(i));
        i += 1;
        assert!(i < 4096, "NUL 終端されていない文字列を渡された");
    }
    v
}

unsafe fn record_names(scope: *const u8, key: *const u8) {
    let (sc, k) = (cstr(scope), cstr(key));
    with(|s| {
        s.last_scope = sc;
        s.last_key = k;
    });
}

#[no_mangle]
pub unsafe extern "C" fn cfg_open(out: *mut *mut Db, writable: i32) -> i32 {
    let (rc, null) = with(|s| {
        s.log.push(Call::Open(writable));
        (s.open_rc, s.open_null)
    });
    if !out.is_null() {
        *out = if rc < 0 || null {
            core::ptr::null_mut()
        } else {
            (&raw mut DUMMY_DB) as *mut Db
        };
    }
    rc
}

#[no_mangle]
pub unsafe extern "C" fn cfg_close(_db: *mut Db) -> i32 {
    with(|s| {
        s.log.push(Call::Close);
        s.close_rc
    })
}

#[no_mangle]
pub unsafe extern "C" fn cfg_get_int(
    _db: *mut Db,
    scope: *const u8,
    key: *const u8,
    def: i32,
) -> i32 {
    record_names(scope, key);
    with(|s| {
        s.log.push(Call::GetInt);
        if s.get_int_def {
            def
        } else {
            s.get_int_rc.unwrap_or(def)
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn cfg_get_text(
    _db: *mut Db,
    scope: *const u8,
    key: *const u8,
    out: *mut u8,
    cap: i32,
) -> i32 {
    record_names(scope, key);
    let (rc, val) = with(|s| {
        s.log.push(Call::GetText);
        s.last_cap = cap;
        (s.get_text_rc, s.get_text_val.clone())
    });
    /* 負を仕込まれていれば out は書かない (NOTFOUND / NOSPC の契約)。 */
    if let Some(rc) = rc {
        return rc;
    }
    let val = val.unwrap_or_default();
    if (val.len() as i32) >= cap {
        return -4; /* OS32_ERR_NOSPC */
    }
    for (i, b) in val.iter().enumerate() {
        *out.add(i) = *b;
    }
    *out.add(val.len()) = 0;
    val.len() as i32
}

#[no_mangle]
pub unsafe extern "C" fn cfg_begin(_db: *mut Db) -> i32 {
    with(|s| {
        s.log.push(Call::Begin);
        s.begin_rc
    })
}

#[no_mangle]
pub unsafe extern "C" fn cfg_set_int(
    _db: *mut Db,
    scope: *const u8,
    key: *const u8,
    v: i32,
) -> i32 {
    record_names(scope, key);
    with(|s| {
        s.log.push(Call::SetInt);
        s.last_int = v;
        s.set_rc
    })
}

#[no_mangle]
pub unsafe extern "C" fn cfg_set_text(
    _db: *mut Db,
    scope: *const u8,
    key: *const u8,
    text: *const u8,
) -> i32 {
    record_names(scope, key);
    let t = cstr(text);
    with(|s| {
        s.log.push(Call::SetText);
        s.last_text = t;
        s.set_rc
    })
}

#[no_mangle]
pub unsafe extern "C" fn cfg_commit(_db: *mut Db) -> i32 {
    with(|s| {
        s.log.push(Call::Commit);
        s.commit_rc
    })
}

#[no_mangle]
pub unsafe extern "C" fn cfg_rollback(_db: *mut Db) -> i32 {
    with(|s| {
        s.log.push(Call::Rollback);
        s.rollback_rc
    })
}
