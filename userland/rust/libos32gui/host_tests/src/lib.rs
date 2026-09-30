//! libos32gui のホスト TDD (票 S2-W、`tools/tests/s2_tdd.md` §W)。
//!
//! `../../src/cfgro.rs` をそのまま取り込み、C の `libos32cfg` の代わりに
//! 贋物を並べて **wrapper の分岐だけ**を固定する。SQLite も実 DB も出てこない
//! (それは S2-C の `test_cfg.py` の領分)。
//!
//! 見るもの:
//!   - get_int … open 失敗 / 未設定 / close 失敗 → すべて def
//!   - get_text … open 失敗はその値、NOTFOUND / NOSPC はそのまま、
//!     close だけ失敗なら IO、**直前の失敗を優先**
//!   - set_* … `app:` scope 以外は PERM で open すらしない、
//!     begin 失敗で rollback しない、set / commit 失敗で rollback する、
//!     close 失敗は IO、**直前の失敗を優先**

/// `libos32cfg` の ABI 宣言。本体では `pub use os32api::cfg as cfgabi;` で、
/// ここでは os32api を丸ごと組まずに**実ファイルを直に**取り込む (票 S4 §4)。
/// `cfgro.rs` が見るのはどちらでも `crate::cfgabi` の 1 名だけ。
#[path = "../../../../../sdk/rust/os32api/src/cfg.rs"]
pub mod cfgabi;

#[path = "../../src/cfgro.rs"]
pub mod cfgro;

pub mod fake;

/* ---- Host Services (票 N4 §1) の分岐を固定する道具 ----
 * `hostsvc.rs` は os32api を名指しせず `crate::hostabi` (= os32api::host の
 * ABI 宣言) 経由で libos32host を呼び、`crate::cfgro` / `crate::utf8core` の
 * 検査・境界計算を共用する。ここでは実ファイルを直に取り込み、host_* と
 * ファイル継ぎ目 hostsvc_file_* を `host_fake` の贋物で閉じる。 */
#[path = "../../../../../sdk/rust/os32api/src/host.rs"]
pub mod hostabi;

#[path = "../../src/utf8core.rs"]
pub mod utf8core;

#[path = "../../src/hostsvc.rs"]
pub mod hostsvc;

pub mod host_fake;

#[cfg(test)]
mod tests {
    use super::cfgro::*;
    use super::fake::{self, Call};

    /* ---------------- 小道具 ---------------- */

    fn get_int(scope: &[u8], key: &[u8], def: i32) -> i32 {
        os32gui_cfg_get_int(
            scope.as_ptr(),
            scope.len() as u32,
            key.as_ptr(),
            key.len() as u32,
            def,
        )
    }

    fn get_text(scope: &[u8], key: &[u8], out: &mut [u8]) -> i32 {
        os32gui_cfg_get_text(
            scope.as_ptr(),
            scope.len() as u32,
            key.as_ptr(),
            key.len() as u32,
            out.as_mut_ptr(),
            out.len() as u32,
        )
    }

    fn set_int(scope: &[u8], key: &[u8], v: i32) -> i32 {
        os32gui_cfg_set_int(
            scope.as_ptr(),
            scope.len() as u32,
            key.as_ptr(),
            key.len() as u32,
            v,
        )
    }

    fn set_text(scope: &[u8], key: &[u8], s: &[u8]) -> i32 {
        os32gui_cfg_set_text(
            scope.as_ptr(),
            scope.len() as u32,
            key.as_ptr(),
            key.len() as u32,
            s.as_ptr(),
            s.len() as u32,
        )
    }

    /* ================================================================ */
    /*  純粋部: 分岐表と scope 検査                                      */
    /* ================================================================ */

    #[test]
    fn w01_fold_get_int_is_def_on_any_failure() {
        assert_eq!(fold_get_int(0, 5, 0, 7), 5, "全成功なら値");
        assert_eq!(fold_get_int(ERR_IO, 5, 0, 7), 7, "open 失敗 → def");
        assert_eq!(fold_get_int(0, 5, ERR_IO, 7), 7, "close 失敗 → def");
        assert_eq!(fold_get_int(0, -12345, 0, 7), -12345, "負の設定値は値として通す");
    }

    #[test]
    fn w02_fold_get_text_prefers_earlier_failure() {
        assert_eq!(fold_get_text(0, 4, 0), 4);
        assert_eq!(fold_get_text(ERR_INVAL, 0, 0), ERR_INVAL, "open 失敗はその値");
        assert_eq!(fold_get_text(0, ERR_NOTFOUND, 0), ERR_NOTFOUND);
        assert_eq!(fold_get_text(0, 0, ERR_IO), ERR_IO, "close だけ失敗 → IO");
        assert_eq!(
            fold_get_text(0, ERR_NOTFOUND, ERR_IO),
            ERR_NOTFOUND,
            "直前の失敗を優先"
        );
    }

    #[test]
    fn w03_fold_set_prefers_earlier_failure() {
        assert_eq!(fold_set(0, 0, 0), 0);
        assert_eq!(fold_set(ERR_IO, 0, 0), ERR_IO);
        assert_eq!(fold_set(0, ERR_INVAL, 0), ERR_INVAL);
        assert_eq!(fold_set(0, 0, ERR_IO), ERR_IO);
        assert_eq!(fold_set(0, ERR_INVAL, ERR_IO), ERR_INVAL, "直前の失敗を優先");
    }

    #[test]
    fn w04_scope_is_app_table() {
        assert!(scope_is_app(b"app:a"));
        assert!(scope_is_app(b"app:filer"));
        assert!(scope_is_app(b"app:my_app9"));
        assert!(!scope_is_app(b"app:"), "本体が空");
        assert!(!scope_is_app(b"system"));
        assert!(!scope_is_app(b"gshell"));
        assert!(!scope_is_app(b"user"));
        assert!(!scope_is_app(b""));
        assert!(!scope_is_app(b"App:x"), "大文字の接頭辞は不可");
        assert!(!scope_is_app(b"app:X"), "大文字の本体は不可");
        assert!(!scope_is_app(b"app:a-b"), "[a-z0-9_] 以外は不可");
        assert!(!scope_is_app(b"app:a/b"));
        assert!(!scope_is_app(b"xapp:a"));
        let mut long = b"app:".to_vec();
        long.resize(CFG_NAME_MAX, b'a');
        assert!(scope_is_app(&long), "63B ちょうどは通る");
        long.push(b'a');
        assert!(!scope_is_app(&long), "64B は不可");
    }

    #[test]
    fn w05_copy_cstr_and_copy_value() {
        let mut b = [0u8; CFG_NAME_CAP];
        assert!(copy_cstr(b"abc", &mut b));
        assert_eq!(&b[..4], b"abc\0");
        assert!(!copy_cstr(b"", &mut b), "空の scope / key は拒否");
        assert!(!copy_cstr(b"a\0b", &mut b), "埋め込み NUL は拒否");
        assert!(copy_cstr(&[b'x'; CFG_NAME_MAX], &mut b), "63B は通る");
        assert!(!copy_cstr(&[b'x'; CFG_NAME_CAP], &mut b), "64B は入らない");

        let mut v = [0u8; CFG_TEXT_CAP];
        assert!(copy_value(b"", &mut v), "空の**値**は許す (NULL とは別)");
        assert_eq!(v[0], 0);
        assert!(copy_value(&[b'x'; CFG_TEXT_MAX], &mut v), "255B は通る");
        assert!(!copy_value(&[b'x'; CFG_TEXT_CAP], &mut v), "256B は入らない");
        assert!(!copy_value(b"a\0b", &mut v));
    }

    /* ================================================================ */
    /*  get_int                                                         */
    /* ================================================================ */

    #[test]
    fn w06_get_int_reads_value_and_closes() {
        fake::reset();
        fake::set_get_int(42);
        assert_eq!(get_int(b"gshell", b"desktop/color", 7), 42);
        assert_eq!(
            fake::log(),
            vec![Call::Open(0), Call::GetInt, Call::Close],
            "open(RO) → get → close で閉じきる"
        );
        assert_eq!(fake::last_scope(), b"gshell");
        assert_eq!(fake::last_key(), b"desktop/color");
    }

    #[test]
    fn w07_get_int_open_failure_is_def() {
        fake::reset();
        fake::set_open_rc(ERR_IO);
        assert_eq!(get_int(b"gshell", b"k", 7), 7);
        assert_eq!(fake::log(), vec![Call::Open(0)], "open が負なら close しない");
    }

    #[test]
    fn w08_get_int_open_null_is_def() {
        fake::reset();
        fake::set_open_null(true);
        assert_eq!(get_int(b"gshell", b"k", 7), 7);
        assert_eq!(fake::log(), vec![Call::Open(0)], "NULL を掴まされたら触らない");
    }

    #[test]
    fn w09_get_int_notfound_is_def() {
        fake::reset();
        /* MISSING / CORRUPT / 未設定では cfg_get_int が def を返す契約。 */
        fake::set_get_int_passthrough_def(true);
        assert_eq!(get_int(b"gshell", b"k", 7), 7);
        assert_eq!(fake::log(), vec![Call::Open(0), Call::GetInt, Call::Close]);
    }

    #[test]
    fn w10_get_int_close_failure_is_def() {
        fake::reset();
        fake::set_get_int(42);
        fake::set_close_rc(ERR_IO);
        assert_eq!(get_int(b"gshell", b"k", 7), 7, "close 失敗なら読めた値も捨てる");
        assert_eq!(fake::log(), vec![Call::Open(0), Call::GetInt, Call::Close]);
    }

    #[test]
    fn w11_get_int_bad_name_never_opens() {
        fake::reset();
        fake::set_get_int(42);
        assert_eq!(get_int(b"", b"k", 7), 7, "空 scope");
        assert_eq!(get_int(b"gshell", b"", 7), 7, "空 key");
        assert_eq!(get_int(&[b'x'; 64], b"k", 7), 7, "64B の scope");
        assert_eq!(get_int(b"gshell", b"a\0b", 7), 7, "埋め込み NUL");
        assert!(fake::log().is_empty(), "検査で落ちたら DB を開かない");
    }

    /* ================================================================ */
    /*  get_text                                                        */
    /* ================================================================ */

    #[test]
    fn w12_get_text_reads_value() {
        fake::reset();
        fake::set_get_text(b"blue");
        let mut out = [0xAAu8; 16];
        assert_eq!(get_text(b"gshell", b"theme", &mut out), 4);
        assert_eq!(&out[..5], b"blue\0");
        assert_eq!(fake::log(), vec![Call::Open(0), Call::GetText, Call::Close]);
        assert_eq!(fake::last_cap(), 16);
    }

    #[test]
    fn w13_get_text_open_failure_passes_code_through() {
        fake::reset();
        fake::set_open_rc(ERR_INVAL);
        let mut out = [0u8; 16];
        assert_eq!(get_text(b"gshell", b"theme", &mut out), ERR_INVAL);
        assert_eq!(fake::log(), vec![Call::Open(0)]);
    }

    #[test]
    fn w14_get_text_notfound_and_nospc_pass_through() {
        fake::reset();
        fake::set_get_text_rc(ERR_NOTFOUND);
        let mut out = [0xAAu8; 16];
        assert_eq!(get_text(b"gshell", b"theme", &mut out), ERR_NOTFOUND);
        assert_eq!(out[0], 0xAA, "未設定なら out は書かない");

        fake::reset();
        const ERR_NOSPC: i32 = -4;
        fake::set_get_text_rc(ERR_NOSPC);
        assert_eq!(get_text(b"gshell", b"theme", &mut out), ERR_NOSPC);
        assert_eq!(out[0], 0xAA, "cap 不足でも out は書かない");
    }

    #[test]
    fn w15_get_text_close_failure_is_io() {
        fake::reset();
        fake::set_get_text(b"blue");
        fake::set_close_rc(ERR_IO);
        let mut out = [0u8; 16];
        assert_eq!(get_text(b"gshell", b"theme", &mut out), ERR_IO);
    }

    #[test]
    fn w16_get_text_get_failure_wins_over_close_failure() {
        fake::reset();
        fake::set_get_text_rc(ERR_NOTFOUND);
        fake::set_close_rc(ERR_IO);
        let mut out = [0u8; 16];
        assert_eq!(
            get_text(b"gshell", b"theme", &mut out),
            ERR_NOTFOUND,
            "直前の失敗を優先"
        );
    }

    #[test]
    fn w17_get_text_bad_args_never_open() {
        fake::reset();
        fake::set_get_text(b"blue");
        let mut out = [0u8; 16];
        assert_eq!(
            os32gui_cfg_get_text(b"gshell".as_ptr(), 6, b"k".as_ptr(), 1, core::ptr::null_mut(), 16),
            ERR_INVAL,
            "out が NULL"
        );
        assert_eq!(
            os32gui_cfg_get_text(b"gshell".as_ptr(), 6, b"k".as_ptr(), 1, out.as_mut_ptr(), 0),
            ERR_INVAL,
            "cap 0"
        );
        assert_eq!(get_text(b"", b"k", &mut out), ERR_INVAL, "空 scope");
        assert_eq!(get_text(b"gshell", b"", &mut out), ERR_INVAL, "空 key");
        assert!(fake::log().is_empty());
    }

    /* ================================================================ */
    /*  set_int / set_text                                              */
    /* ================================================================ */

    #[test]
    fn w18_set_refuses_os_scopes_without_opening() {
        fake::reset();
        for scope in [
            &b"system"[..],
            &b"gshell"[..],
            &b"user"[..],
            &b"app:"[..],
            &b"app:BAD"[..],
            &b""[..],
        ] {
            assert_eq!(set_int(scope, b"k", 1), ERR_PERM, "{:?}", scope);
            assert_eq!(set_text(scope, b"k", b"v"), ERR_PERM, "{:?}", scope);
        }
        assert!(fake::log().is_empty(), "拒否した要求で DB を開かない");
    }

    #[test]
    fn w19_set_int_writes_in_one_transaction() {
        fake::reset();
        assert_eq!(set_int(b"app:filer", b"pane/width", 120), 0);
        assert_eq!(
            fake::log(),
            vec![
                Call::Open(1),
                Call::Begin,
                Call::SetInt,
                Call::Commit,
                Call::Close
            ],
            "open(RW) → begin → set → commit → close を 1 呼び出しで閉じる"
        );
        assert_eq!(fake::last_scope(), b"app:filer");
        assert_eq!(fake::last_key(), b"pane/width");
        assert_eq!(fake::last_int(), 120);
    }

    #[test]
    fn w20_set_text_writes_value_including_empty() {
        fake::reset();
        assert_eq!(set_text(b"app:filer", b"last/path", b"/usr/bin"), 0);
        assert_eq!(fake::last_text(), b"/usr/bin");
        assert_eq!(
            fake::log(),
            vec![
                Call::Open(1),
                Call::Begin,
                Call::SetText,
                Call::Commit,
                Call::Close
            ]
        );

        fake::reset();
        assert_eq!(set_text(b"app:filer", b"last/path", b""), 0, "空値は書ける");
        assert_eq!(fake::last_text(), b"");
    }

    #[test]
    fn w21_set_text_rejects_oversized_value_after_scope_check() {
        fake::reset();
        let ok = vec![b'x'; CFG_TEXT_MAX];
        assert_eq!(set_text(b"app:filer", b"k", &ok), 0, "255B は通る");
        fake::reset();
        let ng = vec![b'x'; CFG_TEXT_MAX + 1];
        assert_eq!(set_text(b"app:filer", b"k", &ng), ERR_INVAL, "256B は INVAL");
        assert!(fake::log().is_empty(), "値の検査で落ちたら開かない");
    }

    #[test]
    fn w22_set_open_failure_passes_code_through() {
        fake::reset();
        fake::set_open_rc(ERR_IO);
        assert_eq!(set_int(b"app:filer", b"k", 1), ERR_IO);
        assert_eq!(fake::log(), vec![Call::Open(1)]);
    }

    #[test]
    fn w23_set_begin_failure_does_not_rollback() {
        fake::reset();
        /* MISSING / CORRUPT / VERSION は cfg_begin が INVAL を返す契約 (票 §1-1(d))。 */
        fake::set_begin_rc(ERR_INVAL);
        assert_eq!(set_int(b"app:filer", b"k", 1), ERR_INVAL);
        assert_eq!(
            fake::log(),
            vec![Call::Open(1), Call::Begin, Call::Close],
            "BEGIN していないので ROLLBACK しない"
        );
    }

    #[test]
    fn w24_set_failure_rolls_back_and_returns_its_code() {
        fake::reset();
        fake::set_set_rc(ERR_INVAL);
        assert_eq!(set_int(b"app:filer", b"k", 1), ERR_INVAL);
        assert_eq!(
            fake::log(),
            vec![
                Call::Open(1),
                Call::Begin,
                Call::SetInt,
                Call::Rollback,
                Call::Close
            ],
            "set が失敗したら commit せず rollback"
        );
    }

    #[test]
    fn w25_commit_failure_rolls_back_and_returns_its_code() {
        fake::reset();
        fake::set_commit_rc(ERR_IO);
        assert_eq!(set_int(b"app:filer", b"k", 1), ERR_IO);
        assert_eq!(
            fake::log(),
            vec![
                Call::Open(1),
                Call::Begin,
                Call::SetInt,
                Call::Commit,
                Call::Rollback,
                Call::Close
            ]
        );
    }

    #[test]
    fn w26_set_close_failure_is_io() {
        fake::reset();
        fake::set_close_rc(ERR_IO);
        assert_eq!(set_int(b"app:filer", b"k", 1), ERR_IO, "commit 済みでも close 失敗は隠さない");
        assert_eq!(
            fake::log(),
            vec![
                Call::Open(1),
                Call::Begin,
                Call::SetInt,
                Call::Commit,
                Call::Close
            ]
        );
    }

    #[test]
    fn w27_set_earlier_failure_wins_over_close_failure() {
        fake::reset();
        fake::set_set_rc(ERR_INVAL);
        fake::set_close_rc(ERR_IO);
        assert_eq!(
            set_int(b"app:filer", b"k", 1),
            ERR_INVAL,
            "直前の失敗を優先 (close の IO で上書きしない)"
        );
    }

    /* ================================================================ */
    /*  生の ptr + len の検査 (レビュー往復 1 の ⑮)                      */
    /* ================================================================ */

    #[test]
    fn w30_raw_span_ok_table() {
        let p = b"abc".as_ptr();
        assert!(raw_span_ok(p, 3, 63));
        assert!(raw_span_ok(p, 0, 63), "非 NULL + len 0 は空");
        assert!(!raw_span_ok(p, 64, 63), "上限超の len");
        assert!(!raw_span_ok(p, u32::MAX, 63), "巨大 len は slice を作る前に落とす");
        assert!(
            raw_span_ok(core::ptr::null(), 0, 63),
            "NULL + len 0 だけが空として通る"
        );
        assert!(
            !raw_span_ok(core::ptr::null(), 1, 63),
            "NULL + len != 0 を空スライスに化けさせない"
        );
        /* ptr + len が番地空間を折り返す (from_raw_parts の前提を破る)。 */
        let top = (usize::MAX - 3) as *const u8;
        assert!(!raw_span_ok(top, 8, 63));
    }

    #[test]
    fn w31_raw_out_ok_table() {
        let mut b = [0u8; 8];
        let p = b.as_mut_ptr();
        assert!(raw_out_ok(p, 8));
        assert!(!raw_out_ok(core::ptr::null(), 8), "NULL の書き込み先");
        assert!(!raw_out_ok(p, 0), "cap 0");
        assert!(!raw_out_ok(p, i32::MAX as u32 + 1), "C へ int で渡せない cap");
        assert!(!raw_out_ok((usize::MAX - 3) as *const u8, 8), "折り返し");
    }

    #[test]
    fn w32_set_text_null_value_with_nonzero_len_is_rejected() {
        /* 反例 (⑮): 有効な scope / key + `s = NULL, s_len = 1` が空スライスに
         * 化けると、既存値を**空 text で上書きして成功**してしまう。 */
        fake::reset();
        assert_eq!(
            os32gui_cfg_set_text(
                b"app:filer".as_ptr(),
                9,
                b"k".as_ptr(),
                1,
                core::ptr::null(),
                1
            ),
            ERR_INVAL
        );
        assert!(fake::log().is_empty(), "DB を開かない = 上書きしない");
    }

    #[test]
    fn w33_set_text_null_value_with_zero_len_is_the_empty_value() {
        fake::reset();
        assert_eq!(
            os32gui_cfg_set_text(
                b"app:filer".as_ptr(),
                9,
                b"k".as_ptr(),
                1,
                core::ptr::null(),
                0
            ),
            0,
            "NULL + len 0 は空値として書ける"
        );
        assert_eq!(fake::last_text(), b"");
    }

    #[test]
    fn w34_huge_len_is_rejected_before_slicing() {
        /* 非 NULL + 巨大長。素朴な実装だと 63 / 255B の拒否より前に
         * `from_raw_parts` の前提を破る。 */
        fake::reset();
        let sc = b"app:filer";
        let k = b"k";
        assert_eq!(
            os32gui_cfg_set_text(sc.as_ptr(), 9, k.as_ptr(), 1, b"v".as_ptr(), u32::MAX),
            ERR_INVAL,
            "値の長さ"
        );
        assert_eq!(
            os32gui_cfg_set_int(sc.as_ptr(), u32::MAX, k.as_ptr(), 1, 1),
            ERR_INVAL,
            "scope の長さ (scope_is_app より前)"
        );
        assert_eq!(
            os32gui_cfg_set_int(sc.as_ptr(), 9, k.as_ptr(), u32::MAX, 1),
            ERR_INVAL,
            "key の長さ"
        );
        assert_eq!(
            os32gui_cfg_get_int(sc.as_ptr(), u32::MAX, k.as_ptr(), 1, 7),
            7,
            "get_int は def"
        );
        let mut out = [0u8; 16];
        assert_eq!(
            os32gui_cfg_get_text(sc.as_ptr(), 9, k.as_ptr(), u32::MAX, out.as_mut_ptr(), 16),
            ERR_INVAL,
            "get_text は INVAL"
        );
        assert!(fake::log().is_empty());
    }

    #[test]
    fn w35_null_name_with_nonzero_len_is_rejected() {
        fake::reset();
        let mut out = [0u8; 16];
        let nul = core::ptr::null();
        assert_eq!(os32gui_cfg_get_int(nul, 1, b"k".as_ptr(), 1, 7), 7);
        assert_eq!(
            os32gui_cfg_get_text(nul, 1, b"k".as_ptr(), 1, out.as_mut_ptr(), 16),
            ERR_INVAL
        );
        assert_eq!(
            os32gui_cfg_set_int(nul, 1, b"k".as_ptr(), 1, 1),
            ERR_INVAL,
            "NULL scope は PERM ではなく INVAL (空スライスに化けない)"
        );
        assert_eq!(
            os32gui_cfg_set_int(b"app:filer".as_ptr(), 9, nul, 1, 1),
            ERR_INVAL,
            "NULL key"
        );
        assert!(fake::log().is_empty());
    }

    #[test]
    fn w28_set_bad_key_never_opens() {
        fake::reset();
        assert_eq!(set_int(b"app:filer", b"", 1), ERR_INVAL, "空 key");
        assert_eq!(set_int(b"app:filer", &[b'x'; 64], 1), ERR_INVAL, "64B の key");
        assert_eq!(set_int(b"app:filer", b"a\0b", 1), ERR_INVAL, "埋め込み NUL");
        assert!(fake::log().is_empty());
    }
}

/* ================================================================ */
/*  Host Services (票 N4 §1) の wrapper の分岐                        */
/* ================================================================ */
#[cfg(test)]
mod host_svc_tests {
    use super::host_fake::{self, Call};
    use super::hostsvc::*;

    /* --- HOST_E* (libos32host.h の写し、透過を確かめる) --- */
    const HOST_ELINK: i32 = -100;
    const HOST_ESERVICE: i32 = -103;
    const HOST_EINVAL: i32 = -104;
    const HOST_EIO: i32 = -105;
    const HOST_EABORT: i32 = -106;
    /* OS32_ERR_* (open 失敗の仕込み) */
    const ERR_NOTFOUND: i32 = -2;
    const ERR_ISDIR: i32 = -8;
    const ERR_INVAL: i32 = -9;

    fn get(url: &[u8], out: &mut [u8], http: Option<&mut u32>) -> i32 {
        let hp = match http {
            Some(r) => r as *mut u32,
            None => core::ptr::null_mut(),
        };
        os32gui_host_get(url.as_ptr(), url.len() as u32, out.as_mut_ptr(), out.len() as u32, hp)
    }
    fn clip_get(out: &mut [u8], total: Option<&mut u32>, svc: Option<&mut u32>) -> i32 {
        let tp = match total {
            Some(r) => r as *mut u32,
            None => core::ptr::null_mut(),
        };
        let sp = match svc {
            Some(r) => r as *mut u32,
            None => core::ptr::null_mut(),
        };
        os32gui_clip_get(out.as_mut_ptr(), out.len() as u32, tp, sp)
    }

    /* ---------------- 純粋部 ---------------- */

    #[test]
    fn h00_fold_open_err_table() {
        assert_eq!(fold_open_err(ERR_INVAL), HOST_EINVAL, "引数不正");
        assert_eq!(fold_open_err(ERR_ISDIR), HOST_EINVAL, "ディレクトリ");
        assert_eq!(fold_open_err(ERR_NOTFOUND), HOST_EIO, "その他 → EIO");
        assert_eq!(fold_open_err(-1), HOST_EIO);
    }

    #[test]
    fn h01_copy_name_drops_control_bytes() {
        let mut b = [0u8; 16];
        assert!(copy_name(b"a\tb\nc", &mut b), "制御バイト混じり");
        assert_eq!(&b[..4], b"abc\0", "TAB/LF は落ちる");
        let mut small = [0u8; 3];
        assert!(!copy_name(b"abcd", &mut small), "NUL の余地なし");
    }

    #[test]
    fn h01b_copy_name_empty_or_control_only_is_rejected() {
        /* 空 basename・制御文字のみは印刷ジョブ名にできない → false (EINVAL)。
         * 空白は制御文字ではないので残り、名前として通る (N4a nb3)。 */
        let mut b = [0u8; 16];
        assert!(!copy_name(b"", &mut b), "空名は false");
        let mut b2 = [0u8; 16];
        assert!(!copy_name(b"\t\n\x7f\x01", &mut b2), "制御文字のみは false");
        let mut b3 = [0u8; 16];
        assert!(copy_name(b"  ", &mut b3), "空白のみは通す (Agent が許す)");
        assert_eq!(&b3[..3], b"  \0", "空白はそのまま NUL 終端");
    }

    /* ---------------- host_get ---------------- */

    #[test]
    fn h02_host_get_pointer_validation() {
        host_fake::reset();
        host_fake::set_get(0, b"body", 200);
        let mut out = [0u8; 8];
        assert_eq!(get(b"", &mut out, None), HOST_EINVAL, "空 url");
        assert_eq!(
            os32gui_host_get(core::ptr::null(), 1, out.as_mut_ptr(), 8, core::ptr::null_mut()),
            HOST_EINVAL,
            "NULL url + len!=0"
        );
        assert_eq!(
            os32gui_host_get(b"http://x".as_ptr(), 8, core::ptr::null_mut(), 8, core::ptr::null_mut()),
            HOST_EINVAL,
            "NULL out"
        );
        assert_eq!(
            os32gui_host_get(b"http://x".as_ptr(), 8, out.as_mut_ptr(), 0, core::ptr::null_mut()),
            HOST_EINVAL,
            "cap 0"
        );
        assert!(host_fake::log().is_empty(), "検査で落ちたら host を呼ばない");
    }

    #[test]
    fn h03_host_get_cap_exceeded_returns_real_length() {
        host_fake::reset();
        host_fake::set_get(0, b"ABCDEF", 200); /* 6 バイト */
        let mut out = [0u8; 4];
        let mut http = 0u32;
        let ret = get(b"http://h/x", &mut out, Some(&mut http));
        assert_eq!(ret, 6, "戻り = 受信実長 (snprintf 流)");
        assert_eq!(&out, b"ABCD", "out には min(実長, cap)");
        assert_eq!(http, 200, "http_status 透過");
        assert_eq!(host_fake::log(), vec![Call::Get]);
        assert_eq!(host_fake::last_url(), b"http://h/x");
    }

    #[test]
    fn h04_host_get_error_passthrough_and_http_null() {
        host_fake::reset();
        host_fake::set_get(HOST_ELINK, b"", 0);
        let mut out = [0u8; 8];
        assert_eq!(get(b"http://h", &mut out, None), HOST_ELINK, "エラー透過、http NULL 可");
    }

    #[test]
    fn h05_host_get_404_is_business_status_not_error() {
        host_fake::reset();
        host_fake::set_get(0, b"nope", 404);
        let mut out = [0u8; 16];
        let mut http = 0u32;
        let ret = get(b"http://h/missing", &mut out, Some(&mut http));
        assert_eq!(ret, 4, "本文は届く (404 でもエラーにしない)");
        assert_eq!(http, 404);
    }

    /* ---------------- clip_get ---------------- */

    #[test]
    fn h06_clip_get_cap_exceeded_written_lt_total() {
        host_fake::reset();
        host_fake::set_clip_get(0, b"hello", 0); /* 5 バイト */
        let mut out = [0u8; 3];
        let mut total = 0u32;
        let w = clip_get(&mut out, Some(&mut total), None);
        assert_eq!(w, 3, "written = out に書いた長さ");
        assert_eq!(total, 5, "total = 受信実長");
        assert_eq!(&out, b"hel", "out は NUL 終端しない");
    }

    #[test]
    fn h07_clip_get_stops_at_nul() {
        host_fake::reset();
        host_fake::set_clip_get(0, b"ab\0cd", 0); /* 実長 5、NUL は index 2 */
        let mut out = [0u8; 16];
        let mut total = 0u32;
        let w = clip_get(&mut out, Some(&mut total), None);
        assert_eq!(w, 2, "NUL 以降も捨てる");
        assert_eq!(total, 5);
    }

    #[test]
    fn h08_clip_get_utf8_boundary() {
        host_fake::reset();
        /* "あい" = E3 81 82 E3 81 84 (6 バイト)。cap 4 → 途中で切らず "あ" だけ。 */
        host_fake::set_clip_get(0, "あい".as_bytes(), 0);
        let mut out = [0u8; 4];
        let mut total = 0u32;
        let w = clip_get(&mut out, Some(&mut total), None);
        assert_eq!(w, 3, "UTF-8 境界まで戻す (written 3)");
        assert_eq!(total, 6, "total 6");
        assert_eq!(&out[..3], "あ".as_bytes());
    }

    #[test]
    fn h09_clip_get_cap0_or_null_is_einval() {
        host_fake::reset();
        host_fake::set_clip_get(0, b"x", 0);
        let mut out = [0u8; 8];
        assert_eq!(
            os32gui_clip_get(out.as_mut_ptr(), 0, core::ptr::null_mut(), core::ptr::null_mut()),
            HOST_EINVAL,
            "cap 0"
        );
        assert_eq!(
            os32gui_clip_get(core::ptr::null_mut(), 8, core::ptr::null_mut(), core::ptr::null_mut()),
            HOST_EINVAL,
            "out NULL"
        );
        assert!(host_fake::log().is_empty());
    }

    #[test]
    fn h10_clip_get_svc_passthrough_and_error() {
        host_fake::reset();
        host_fake::set_clip_get(HOST_ESERVICE, b"", 503);
        let mut out = [0u8; 8];
        let mut svc = 0u32;
        let r = clip_get(&mut out, None, Some(&mut svc));
        assert_eq!(r, HOST_ESERVICE, "業務失敗は HOST_ESERVICE");
        assert_eq!(svc, 503, "svc_status 透過");
    }

    /* ---------------- print_text ---------------- */

    #[test]
    fn h11_print_text_passes_body_pages_svc_and_strips_name() {
        host_fake::reset();
        host_fake::set_print(0, 3, 0);
        let mut pages = 0u32;
        let mut svc = 0u32;
        let name = b"re\tport"; /* TAB は落ちる */
        let body = b"hello world";
        let r = os32gui_print_text(
            name.as_ptr(),
            name.len() as u32,
            body.as_ptr(),
            body.len() as u32,
            &mut pages as *mut u32,
            &mut svc as *mut u32,
        );
        assert_eq!(r, 0);
        assert_eq!(pages, 3, "pages 透過");
        assert_eq!(host_fake::last_name(), b"report", "制御文字を落とす");
        assert_eq!(host_fake::last_print_body(), body);
        assert_eq!(host_fake::log(), vec![Call::PrintText]);
    }

    #[test]
    fn h12_print_text_service_error_passthrough() {
        host_fake::reset();
        host_fake::set_print(HOST_ESERVICE, 0, 500);
        let mut svc = 0u32;
        let name = b"j";
        let body = b"x";
        let r = os32gui_print_text(
            name.as_ptr(),
            1,
            body.as_ptr(),
            1,
            core::ptr::null_mut(),
            &mut svc as *mut u32,
        );
        assert_eq!(r, HOST_ESERVICE);
        assert_eq!(svc, 500, "svc_status に業務値");
    }

    /* ---------------- print_file ---------------- */

    #[test]
    fn h13_print_file_open_invalid_and_dir_is_einval() {
        for rc in [ERR_INVAL, ERR_ISDIR] {
            host_fake::reset();
            host_fake::set_file(rc, b"", None);
            let name = b"f";
            let path = b"/etc/x";
            let r = os32gui_print_file(
                name.as_ptr(),
                1,
                path.as_ptr(),
                path.len() as u32,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            );
            assert_eq!(r, HOST_EINVAL, "open rc {} → EINVAL", rc);
            assert_eq!(host_fake::log(), vec![Call::FileOpen], "open だけ (stream/close なし)");
        }
    }

    #[test]
    fn h14_print_file_open_other_is_eio() {
        host_fake::reset();
        host_fake::set_file(ERR_NOTFOUND, b"", None);
        let name = b"f";
        let path = b"/etc/x";
        let r = os32gui_print_file(
            name.as_ptr(),
            1,
            path.as_ptr(),
            path.len() as u32,
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        );
        assert_eq!(r, HOST_EIO, "その他の open 失敗 → EIO");
    }

    #[test]
    fn h15_print_file_streams_and_closes() {
        host_fake::reset();
        host_fake::set_file(3, b"file body 123", None); /* fd 3 */
        host_fake::set_print(0, 2, 0);
        let mut pages = 0u32;
        let name = b"doc.txt";
        let path = b"/home/doc.txt";
        let r = os32gui_print_file(
            name.as_ptr(),
            name.len() as u32,
            path.as_ptr(),
            path.len() as u32,
            &mut pages as *mut u32,
            core::ptr::null_mut(),
        );
        assert_eq!(r, 0);
        assert_eq!(pages, 2);
        assert_eq!(host_fake::last_name(), b"doc.txt");
        assert_eq!(host_fake::last_path(), b"/home/doc.txt");
        assert_eq!(host_fake::last_print_body(), b"file body 123", "src で全量を引く");
        let log = host_fake::log();
        assert_eq!(log[0], Call::FileOpen);
        assert_eq!(*log.last().unwrap(), Call::FileClose, "全経路で close");
        assert!(log.contains(&Call::PrintStream));
    }

    #[test]
    fn h16_print_file_read_error_aborts_but_still_closes() {
        host_fake::reset();
        host_fake::set_file(3, b"", Some(-1)); /* read が失敗 */
        host_fake::set_print(0, 9, 0);
        let name = b"f";
        let path = b"/x";
        let r = os32gui_print_file(
            name.as_ptr(),
            1,
            path.as_ptr(),
            2,
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        );
        assert_eq!(r, HOST_EABORT, "src < 0 → EABORT");
        assert_eq!(*host_fake::log().last().unwrap(), Call::FileClose, "失敗経路でも close");
    }

    /* ---------------- clip_put ---------------- */

    #[test]
    fn h17_clip_put_length_limits() {
        host_fake::reset();
        host_fake::set_clip_put(0, 0);
        assert_eq!(
            os32gui_clip_put(b"x".as_ptr(), 0, core::ptr::null_mut()),
            HOST_EINVAL,
            "空 (len 0)"
        );
        let big = vec![b'x'; 4097];
        assert_eq!(
            os32gui_clip_put(big.as_ptr(), 4097, core::ptr::null_mut()),
            HOST_EINVAL,
            "4096 超"
        );
        assert!(host_fake::log().is_empty(), "検査で落ちたら host を呼ばない");
    }

    #[test]
    fn h18_clip_put_passthrough_and_svc() {
        host_fake::reset();
        host_fake::set_clip_put(0, 200);
        let mut svc = 0u32;
        let r = os32gui_clip_put(b"clip me".as_ptr(), 7, &mut svc as *mut u32);
        assert_eq!(r, 0);
        assert_eq!(host_fake::last_clip_put(), b"clip me");
        assert_eq!(svc, 200, "svc_status 透過");

        host_fake::reset();
        let ok = vec![b'y'; 4096];
        host_fake::set_clip_put(0, 0);
        assert_eq!(os32gui_clip_put(ok.as_ptr(), 4096, core::ptr::null_mut()), 0, "4096 ちょうどは通る");
    }

    /* ---------------- host_time ---------------- */

    #[test]
    fn h19_host_time_writes_and_null_is_einval() {
        host_fake::reset();
        host_fake::set_time(0, b"2026-09-14 12:34:56");
        let mut out = [0xAAu8; 20];
        assert_eq!(os32gui_host_time(out.as_mut_ptr()), 0);
        assert_eq!(&out[..19], b"2026-09-14 12:34:56");
        assert_eq!(out[19], 0, "NUL 終端");
        assert_eq!(os32gui_host_time(core::ptr::null_mut()), HOST_EINVAL, "NULL out");
    }

    /* ---------------- out_sink の cap 超過後も数える (硬化) ---------------- */

    /* 実サービスは本文を複数回に分けて sink する。out_sink は cap 到達後の
     * チャンクを捨てつつ 0 を返し続け (受理)、実長は host_* の nbytes が数える。
     * この可変分割を仕込むと、`out_sink` 入口に `if written >= cap { return 1 }`
     * を混ぜたとき (cap 超過で中断) chunk2 で EABORT になり戻りが 6 でなく
     * -106 に化けて FAIL する — 変異を殺す検査になる。 */
    #[test]
    fn h20_host_get_counts_past_cap_across_chunks() {
        host_fake::reset();
        /* 6 バイトを cap=4 をまたいで 4+2 で届ける。 */
        host_fake::set_get_chunked(0, b"ABCDEF", 200, &[4, 2]);
        let mut out = [0u8; 4];
        let mut http = 0u32;
        let ret = get(b"http://h/x", &mut out, Some(&mut http));
        assert_eq!(ret, 6, "cap 超過後も数え続けて実長 6 (2 チャンク目も受理)");
        assert_eq!(&out, b"ABCD", "out には先頭 cap ぶんだけ");
        assert_eq!(http, 200);
    }

    #[test]
    fn h20b_clip_get_counts_past_cap_across_chunks() {
        host_fake::reset();
        /* clip_get も同じ out_sink を通る。cap=4 に "ABCDEF" を 4+2 で届け、
         * 2 チャンク目が cap 境界をまたぐようにする (変異を殺せる分割)。 */
        host_fake::set_clip_get_chunked(0, b"ABCDEF", 0, &[4, 2]);
        let mut out = [0u8; 4];
        let mut total = 0u32;
        let w = clip_get(&mut out, Some(&mut total), None);
        assert_eq!(total, 6, "total = 受信実長 6 (cap 超過後も数える)");
        assert_eq!(w, 4, "out に書いたのは cap ぶん (ASCII なので境界戻し無し)");
        assert_eq!(&out, b"ABCD");
    }

    /* ---------------- 空名は EINVAL (wrapper 経由) ---------------- */

    #[test]
    fn h21_print_empty_or_control_name_is_einval() {
        /* print_text: 制御文字のみの name は copy_name が空にして EINVAL。 */
        host_fake::reset();
        host_fake::set_print(0, 1, 0);
        let name = b"\t\n";
        let body = b"x";
        assert_eq!(
            os32gui_print_text(
                name.as_ptr(),
                name.len() as u32,
                body.as_ptr(),
                1,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            ),
            HOST_EINVAL,
            "制御文字のみの印刷名"
        );
        assert!(host_fake::log().is_empty(), "検査で落ちたら host を呼ばない");

        /* print_file: 空名 (len 0) も EINVAL。open すら呼ばない。 */
        host_fake::reset();
        host_fake::set_file(3, b"body", None);
        let path = b"/x";
        assert_eq!(
            os32gui_print_file(
                core::ptr::null(),
                0,
                path.as_ptr(),
                path.len() as u32,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            ),
            HOST_EINVAL,
            "空名の print_file"
        );
        assert!(host_fake::log().is_empty(), "名前検査で落ちたら open もしない");
    }

    /* ---------------- 番地・長さの反例 ---------------- */

    #[test]
    fn h22_oversized_and_null_args_are_einval() {
        host_fake::reset();
        host_fake::set_get(0, b"ok", 200);
        host_fake::set_print(0, 1, 0);
        host_fake::set_file(3, b"body", None);
        host_fake::set_clip_put(0, 0);

        /* url 1396B 超 (URL_CAP-1 = 1395 が上限)。 */
        let big_url = vec![b'u'; 1396];
        let mut out = [0u8; 8];
        assert_eq!(
            os32gui_host_get(big_url.as_ptr(), 1396, out.as_mut_ptr(), 8, core::ptr::null_mut()),
            HOST_EINVAL,
            "url 1396B は上限超"
        );

        /* name 256B 超 (NAME_MAX = 255 が上限)。 */
        let big_name = vec![b'n'; 256];
        let body = b"x";
        assert_eq!(
            os32gui_print_text(
                big_name.as_ptr(),
                256,
                body.as_ptr(),
                1,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            ),
            HOST_EINVAL,
            "印刷名 256B は上限超"
        );

        /* path 256B 超 (PATH_MAX = 255 が上限)。name は正当に。 */
        let big_path = vec![b'/'; 256];
        let ok_name = b"f";
        assert_eq!(
            os32gui_print_file(
                ok_name.as_ptr(),
                1,
                big_path.as_ptr(),
                256,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            ),
            HOST_EINVAL,
            "パス 256B は上限超"
        );

        /* print_text(buf=NULL, len=3) — 本文 NULL + len!=0。 */
        assert_eq!(
            os32gui_print_text(
                ok_name.as_ptr(),
                1,
                core::ptr::null(),
                3,
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            ),
            HOST_EINVAL,
            "本文 NULL + len!=0"
        );

        /* clip_put(NULL, 5) — buf NULL + len!=0。 */
        assert_eq!(
            os32gui_clip_put(core::ptr::null(), 5, core::ptr::null_mut()),
            HOST_EINVAL,
            "clip_put NULL + len!=0"
        );

        assert!(host_fake::log().is_empty(), "すべて検査で落ち、host を 1 本も呼ばない");
    }
}
