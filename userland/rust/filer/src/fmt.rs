//! fmt.rs — `core::fmt` も alloc も使わない小さな文字列組み立て。
//!
//! すべて「`dst[at..]` へ詰めて新しい末尾を返す」形。溢れたら黙って止まる
//! (ラベル用なので切り詰めてよい。**パスの切り詰めは禁止**で、そちらは
//! `model::check_abs` / `path_join` が拒否する)。

/// `dst[at..]` へ `src` を NUL 手前まで詰める。返り値: 新しい末尾。
#[inline(never)]
pub fn put(dst: &mut [u8], at: usize, src: &[u8]) -> usize {
    let mut n = at;
    let mut i = 0usize;
    while i < src.len() && src[i] != 0 && n < dst.len() {
        dst[n] = src[i];
        n += 1;
        i += 1;
    }
    n
}

/// `dst[at..]` へ 10 進数を詰める。
#[inline(never)]
pub fn put_num(dst: &mut [u8], at: usize, v: i32) -> usize {
    let mut n = at;
    if v < 0 {
        n = put(dst, n, b"-");
        return put_u32(dst, n, (-(v as i64)) as u32);
    }
    put_u32(dst, n, v as u32)
}

/// `dst[at..]` へ符号なし 10 進数を詰める。
#[inline(never)]
pub fn put_u32(dst: &mut [u8], at: usize, v: u32) -> usize {
    let mut tmp = [0u8; 12];
    let mut k = 0usize;
    let mut u = v;
    if u == 0 {
        tmp[k] = b'0';
        k += 1;
    }
    while u > 0 {
        tmp[k] = b'0' + (u % 10) as u8;
        u /= 10;
        k += 1;
    }
    let mut n = at;
    while k > 0 {
        k -= 1;
        if n < dst.len() {
            dst[n] = tmp[k];
            n += 1;
        }
    }
    n
}
