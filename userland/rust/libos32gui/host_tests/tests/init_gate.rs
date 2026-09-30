//! init_gate.rs — `os32gui_shlib_init` **前**に表 101..=104 を呼ばれたときの門。
//!
//! shlib には crt0 が無いので、C の `cfg_backend.c` が見る `kapi` は
//! `os32gui_shlib_init(api)` が入れるまで NULL のまま。そこで `cfg_*` を呼ぶと
//! NULL 参照でアプリが死ぬ (CPL=3 なのでアプリだけだが、原因が見えない)。
//! wrapper は `kapi` が NULL なら **`cfg_open` すら呼ばずに**畳む。
//!
//! `kapi` はプロセスに 1 語しかないので、`src/lib.rs` の単体試験
//! (`fake::reset()` が毎回 init 済みにする) とは**別のプロセス**で見る必要が
//! ある。cargo は `tests/` の各ファイルを別バイナリにするので、ここでは
//! `kapi` が初期値の NULL のまま走る。

use libos32gui_host_tests::cfgro::*;
use libos32gui_host_tests::fake;

#[test]
fn w29_calls_before_shlib_init_never_touch_libos32cfg() {
    assert!(!kapi_ready(), "このプロセスでは shlib_init されていないこと");

    let mut out = [0xAAu8; 16];

    assert_eq!(
        os32gui_cfg_get_int(b"gshell".as_ptr(), 6, b"k".as_ptr(), 1, 7),
        7,
        "get_int は def"
    );
    assert_eq!(
        os32gui_cfg_get_text(b"gshell".as_ptr(), 6, b"k".as_ptr(), 1, out.as_mut_ptr(), 16),
        ERR_INVAL,
        "get_text は INVAL"
    );
    assert_eq!(
        os32gui_cfg_set_int(b"app:filer".as_ptr(), 9, b"k".as_ptr(), 1, 5),
        ERR_INVAL,
        "set_int は INVAL (scope が正しくても通さない)"
    );
    assert_eq!(
        os32gui_cfg_set_text(b"app:filer".as_ptr(), 9, b"k".as_ptr(), 1, b"v".as_ptr(), 1),
        ERR_INVAL,
        "set_text は INVAL"
    );

    assert_eq!(out[0], 0xAA, "out に触らない");
    assert!(
        fake::log().is_empty(),
        "libos32cfg を 1 回も呼んでいないこと: {:?}",
        fake::log()
    );

    /* --- 門が開く側 (同じ試験の続き。`kapi` はプロセスに 1 語しか無いので
     *     別の `#[test]` に分けると並列実行で混ざる) --- */

    /* `os32gui_shlib_init` がするのと同じこと (KAPI を渡す) だけを模す。 */
    let mut dummy = 0u8;
    set_kapi((&raw mut dummy) as *mut std::ffi::c_void);
    assert!(kapi_ready());

    fake::set_get_int(42);
    assert_eq!(
        os32gui_cfg_get_int(b"gshell".as_ptr(), 6, b"k".as_ptr(), 1, 7),
        42,
        "init 後はふつうに読める"
    );
    assert!(!fake::log().is_empty());

    /* 同じプロセスの他の試験に影響させない。 */
    set_kapi(core::ptr::null_mut());
}
