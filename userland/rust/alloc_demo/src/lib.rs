/*
 * alloc_demo — Rust alloc クレート活用デモ (OS32)
 *
 * Vec, String, Box 等のヒープアロケーション機能が
 * OS32の mem_alloc/mem_free 上で正常に動作することを検証する。
 *
 * 合否の出し方は票 docs/archive/test/TASK_TEST_RESULT.md §2 に従う。
 * 2026-09-17 まではここは**何も検査していなかった** — 常に 0 を返し、
 * 最後に無条件で "All tests passed!" を印字し、`sum=285` も
 * 「expected 285」とコメントに書くだけで比較していなかった。
 * 回帰台本がこれを「最後の 3 行を読め」としていたのはそのためで、
 * 偽の合格が 1 件混ざっていた。
 *
 * 集計行の書式と終了コードの対応は C 側の唯一の管理元
 * userland/lib/rt/testresult.h と同じ (`<名前>: PASS <n>/<m>` / 0・1・2)。
 * ずれていないことは tools/tests/test_result_conv.py が突き合わせる。
 */
#![no_std]
#![no_main]

extern crate alloc;
extern crate os32api;

use alloc::boxed::Box;
use alloc::format;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;
use os32api::KernelAPI;
use os32api::{kprint, kprint_attr};

/* 試験プログラムの名前は**固定文字列**。argv[0] から作らない
 * (リダイレクト先や呼び出し方で集計行が変わってしまう)。 */
const TEST_NAME: &str = "alloc_demo";

/* 終了コード (票 §2-1 / rt/testresult.h) */
const EXIT_PASS: i32 = 0;
const EXIT_FAIL: i32 = 1;

/* テスト結果カウンタ */
static mut PASS: i32 = 0;
static mut TOTAL: i32 = 0;

unsafe fn check(name: &[u8], ok: bool) {
    TOTAL += 1;
    if ok {
        PASS += 1;
        kprint_attr!(os32api::ATTR_GREEN, b"  [OK] %s\r\n\0", name.as_ptr());
    } else {
        kprint_attr!(os32api::ATTR_RED, b"  [NG] %s\r\n\0", name.as_ptr());
    }
}

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    os32api::os32_init(api);

    kprint!(b"=== Rust alloc demo ===\r\n\0");
    kprint!(b"\r\n\0");

    unsafe {
        PASS = 0;
        TOTAL = 0;

        /* --- Vec<i32> テスト --- */
        kprint_attr!(os32api::ATTR_GREEN, b"[Vec] \0");
        let mut nums: Vec<i32> = Vec::new();
        let mut i: usize = 0;
        while i < 10 {
            nums.push((i * i) as i32);
            i += 1;
        }
        kprint!(
            b"push 0..9 squared: len=%d, cap=%d\r\n\0",
            nums.len() as i32,
            nums.capacity() as i32
        );
        check(b"Vec::push 10 times -> len == 10\0", nums.len() == 10);
        check(b"Vec capacity >= len\0", nums.capacity() >= nums.len());

        /* 合計を計算。0^2 + 1^2 + ... + 9^2 = 285。 */
        let mut sum: i32 = 0;
        i = 0;
        while i < nums.len() {
            sum += nums[i];
            i += 1;
        }
        kprint!(b"  sum of squares = %d (expected 285)\r\n\0", sum);
        check(b"sum of squares 0..9 == 285\0", sum == 285);

        /* --- vec![] マクロテスト --- */
        kprint_attr!(os32api::ATTR_GREEN, b"[vec!] \0");
        let fib = vec![1i32, 1, 2, 3, 5, 8, 13, 21, 34, 55];
        kprint!(
            b"fibonacci: len=%d, last=%d\r\n\0",
            fib.len() as i32,
            fib[fib.len() - 1]
        );
        check(b"vec![..] keeps 10 elements\0", fib.len() == 10);
        check(b"vec![..] last element is 55\0", fib[fib.len() - 1] == 55);

        /* --- Box テスト --- */
        kprint_attr!(os32api::ATTR_GREEN, b"[Box] \0");
        let boxed: Box<i32> = Box::new(42);
        kprint!(b"Box<i32> = %d\r\n\0", *boxed);
        check(b"Box<i32> derefs to 42\0", *boxed == 42);

        /* --- String テスト --- */
        kprint_attr!(os32api::ATTR_GREEN, b"[String] \0");
        let mut s = String::from("Hello");
        s.push_str(", OS32!");
        check(b"String::push_str -> \"Hello, OS32!\"\0", s == "Hello, OS32!");
        check(b"String length is 12\0", s.len() == 12);
        s.push('\0');
        kprint!(b"%s (len=%d)\r\n\0", s.as_ptr(), (s.len() - 1) as i32);

        /* --- format! マクロテスト --- */
        kprint_attr!(os32api::ATTR_GREEN, b"[format!] \0");
        let tick = os32api::get_tick();
        let mut msg = format!("tick={}", tick);
        check(b"format! produced \"tick=<n>\"\0", msg.starts_with("tick="));
        check(
            b"format! rendered the number itself\0",
            msg.len() > "tick=".len(),
        );
        msg.push('\0');
        kprint!(b"%s\r\n\0", msg.as_ptr());

        /* --- Vec<String> テスト (入れ子ヒープ) --- */
        kprint_attr!(os32api::ATTR_GREEN, b"[Vec<String>] \0");
        let mut names: Vec<String> = Vec::new();
        names.push(String::from("PC-9801"));
        names.push(String::from("OS32"));
        names.push(String::from("Rust"));
        kprint!(b"count=%d\r\n\0", names.len() as i32);
        check(b"Vec<String> holds 3 entries\0", names.len() == 3);
        check(
            b"Vec<String> entries survive the nested allocations\0",
            names[0] == "PC-9801" && names[1] == "OS32" && names[2] == "Rust",
        );

        i = 0;
        while i < names.len() {
            let mut n = names[i].clone();
            check(b"String::clone keeps the contents\0", n == names[i]);
            n.push('\0');
            kprint!(b"  [%d] %s\r\n\0", i as i32, n.as_ptr());
            i += 1;
        }

        /* --- Drop (メモリ解放) テスト ---
         * 解放されているかは「同じ大きさをもう一度取れるか」で見る。
         * 解放されていなければヒープが減り続けるので、繰り返せば取れなくなる。 */
        kprint_attr!(os32api::ATTR_GREEN, b"[Drop] \0");
        let mut reuse_ok = true;
        i = 0;
        while i < 64 {
            /* 毎回ちがう印を書いて読み戻す。解放した領域が使い回されず
             * ヒープが減り続ければ途中で確保に失敗し、重なった領域を
             * 配れば印が合わなくなる。 */
            let mark = (i & 0xFF) as u8;
            let mut tmp = vec![0u8; 16 * 1024];
            tmp[0] = mark;
            tmp[16 * 1024 - 1] = mark;
            if tmp.len() != 16 * 1024 || tmp[0] != mark || tmp[16 * 1024 - 1] != mark {
                reuse_ok = false;
            }
            i += 1;
        }
        kprint!(b"16KB x 64 allocated, marked and freed.\r\n\0");
        check(
            b"a freed block can be allocated again (64 rounds of 16KB)\0",
            reuse_ok,
        );

        /* --- 集計行 (最終行に 1 行) --- */
        let pass = PASS;
        let total = TOTAL;
        let ok = total > 0 && pass == total;
        let line = format!(
            "{}: {} {}/{}\n\0",
            TEST_NAME,
            if ok { "PASS" } else { "FAIL" },
            pass,
            total
        );
        let attr = if ok {
            os32api::ATTR_GREEN
        } else {
            os32api::ATTR_RED
        };
        kprint!(b"\r\n\0");
        kprint_attr!(attr, b"%s\0", line.as_ptr());

        if ok {
            EXIT_PASS
        } else {
            EXIT_FAIL
        }
    }
}
