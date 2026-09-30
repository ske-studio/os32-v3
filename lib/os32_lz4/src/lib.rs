/*
 * os32_lz4 — LZ4 ブロック形式 Safe デコーダ (Rust実装)
 *
 * LZ4ブロック仕様 (lz4_Block_format.md) に準拠。
 * 公式 LZ4 / Python lz4.block で圧縮されたデータをそのまま展開可能。
 *
 * C版 (lib/lz4.c) の完全な置き換え。
 * Rustの境界チェック・安全なスライスアクセスにより、
 * GCC最適化に起因するNULLガードページアクセスの問題を回避する。
 *
 * #[no_mangle] extern "C" fn lz4_decode(...) で C ABI 互換。
 */
#![no_std]

use core::panic::PanicInfo;

/* LZ4定数 */
const LZ4_MINMATCH: usize = 4;

/// LZ4ブロック展開 (Safe版, Rust実装)
///
/// src:             LZ4ブロック形式の圧縮データへのポインタ
/// compressed_size: 圧縮データのサイズ (バイト)
/// dst:             展開先バッファへのポインタ (事前確保済み)
/// dst_capacity:    展開先バッファの最大サイズ (バイト)
///
/// 戻り値: 展開後のバイト数 (>= 0)
///         エラー時: 負数
///           -1: 入力データ不正 (破損)
///           -2: 出力バッファ不足
///           -3: NULLポインタ
///           -4: パニック (Rustの境界チェック違反)
#[no_mangle]
pub unsafe extern "C" fn lz4_decode(
    src: *const u8,
    compressed_size: i32,
    dst: *mut u8,
    dst_capacity: i32,
) -> i32 {
    /* NULLチェック・サイズチェック */
    if src.is_null() || dst.is_null() || compressed_size < 0 || dst_capacity < 0 {
        return -3;
    }

    let csz = compressed_size as usize;
    let cap = dst_capacity as usize;

    /* 入出力バッファをスライスとして扱う */
    let input = core::slice::from_raw_parts(src, csz);
    let output = core::slice::from_raw_parts_mut(dst, cap);

    match lz4_decode_safe(input, output) {
        Ok(n) => n as i32,
        Err(e) => e,
    }
}

/// 安全な LZ4 ブロック展開 (全てスライスアクセス、境界チェック付き)
fn lz4_decode_safe(input: &[u8], output: &mut [u8]) -> Result<usize, i32> {
    let mut ip: usize = 0;  /* 入力位置 */
    let mut op: usize = 0;  /* 出力位置 */

    loop {
        /* トークン読み込み */
        if ip >= input.len() {
            break;
        }
        let token = input[ip];
        ip += 1;

        /* --- リテラル --- */
        let mut lit_len: usize = ((token >> 4) & 0x0F) as usize;
        if lit_len == 15 {
            loop {
                if ip >= input.len() {
                    return Err(-1);
                }
                let s = input[ip] as usize;
                ip += 1;
                lit_len += s;
                if s != 255 {
                    break;
                }
            }
        }

        /* リテラルバイト列をコピー */
        if ip + lit_len > input.len() {
            return Err(-1);
        }
        if op + lit_len > output.len() {
            return Err(-2);
        }
        output[op..op + lit_len].copy_from_slice(&input[ip..ip + lit_len]);
        ip += lit_len;
        op += lit_len;

        /* 最終シーケンス判定: 入力を全て消費したらここで終了 */
        if ip >= input.len() {
            break;
        }

        /* --- Offset (2バイト LE) --- */
        if ip + 2 > input.len() {
            return Err(-1);
        }
        let offset = (input[ip] as usize) | ((input[ip + 1] as usize) << 8);
        ip += 2;

        if offset == 0 {
            return Err(-1);
        }
        if op < offset {
            return Err(-1);  /* 出力バッファ先頭を超える参照 */
        }

        /* --- マッチ --- */
        let mut match_len: usize = ((token & 0x0F) as usize) + LZ4_MINMATCH;
        if (token & 0x0F) == 15 {
            loop {
                if ip >= input.len() {
                    return Err(-1);
                }
                let s = input[ip] as usize;
                ip += 1;
                match_len += s;
                if s != 255 {
                    break;
                }
            }
        }

        /* マッチコピー (オーバーラップ対応: 1バイトずつコピー) */
        if op + match_len > output.len() {
            return Err(-2);
        }
        let match_start = op - offset;
        for i in 0..match_len {
            output[op + i] = output[match_start + i];
        }
        op += match_len;
    }

    Ok(op)
}

/* パニックハンドラ (カーネル内で使用するため最小限) */
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
