//! os32x.rs — OS32X ヘッダの宣言ビットを読む (票 T8 D4 の入口)。
//!
//! 起動する前に「そのプログラムは何者か」を 40B だけ読んで決める:
//!
//! | 宣言 | 入口の扱い |
//! |---|---|
//! | `FORCE_CPL0` (`--cpl0`) | **起動しない** (`cui only: <名>`)。VRAM を直接触る v86 / VDM 系 |
//! | `CUI_ONLY` (`--cui-only`) | **起動しない** (同上)。CPL=3 のまま V86 へ入る `v86.bin` はこちら |
//! | `FLAG_GFX` (`--gfx`) | `exec_start` の前に全画面モードの印を立てる (所有者の問い合わせと二重) |
//! | どれも無い | いつもどおり |
//!
//! カーネルの拒否 (`appslot_cpl0_admit` / `gfx_kapi_init`、票 T8 D1) が最後の砦で、
//! ここはユーザーに理由を見せるための前段。判定そのものは [`classify`] という
//! 純関数 1 本に閉じてホストで試験する (I/O は [`classify_path`] だけが持つ)。
//!
//! 同じ判定は端末 (`userland/rust/t5a_display/src/prompt.rs`) にも小さく持つ。
//! 共有ライブラリを 1 本増やすより写しの方が安い (票 T8 §4 の指示)。

/// `OS32X_MAGIC` (`sdk/include/os32/os32_kapi_shared.h`)。'OS32' の LE。
pub const MAGIC: u32 = 0x4F53_3332;
/// v1 ヘッダのサイズ (`OS32X_HDR_V1_SIZE`)。読むのはこの 40B だけ。
pub const HDR_SIZE: usize = 40;
/// `OS32X_FLAG_GFX` — 全画面 GFX を使う宣言。
pub const FLAG_GFX: u32 = 0x0001;
/// `OS32X_FLAG_FORCE_CPL0` — CPL=0 強制 (v86 / VDM)。GUI からは起動しない。
pub const FLAG_FORCE_CPL0: u32 = 0x0004;
/// `OS32X_FLAG_CUI_ONLY` — CUI 専用の宣言。GUI からは起動しない。
///
/// `v86.bin` は `--cpl0` ではなく **flags 0x0 の CPL=3 プログラム**で、V86 へは
/// KAPI 越しに入る (受入 F5 不合格の原因)。`FORCE_CPL0` だけを見る判定では
/// 素通りするので、この宣言を別に持つ。値はリテラル — 票 T8-2 の K/B が
/// `OS32X_FLAG_CUI_ONLY 0x0010` を `sdk/include/os32/os32_kapi_shared.h` へ
/// 足したら、そちらが正典になる。
pub const FLAG_CUI_ONLY: u32 = 0x0010;

/// ヘッダ内の位置 (`OS32Header`)。
const OFF_MAGIC: usize = 0x00;
const OFF_HEADER_SIZE: usize = 0x04;
const OFF_FLAGS: usize = 0x0C;

/// 入口の判定 (票 T8 D4)。
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    /// ふつうの CPL=3 プログラム。GUI 中も起動してよい。
    Plain,
    /// 全画面 GFX の宣言つき (`FLAG_GFX`)。
    FullScreen,
    /// CPL=0 強制 (`FORCE_CPL0`)。GUI からは起動しない。
    CuiOnly,
}

#[inline]
fn le32(b: &[u8], off: usize) -> u32 {
    (b[off] as u32)
        | ((b[off + 1] as u32) << 8)
        | ((b[off + 2] as u32) << 16)
        | ((b[off + 3] as u32) << 24)
}

/// **純関数**: OS32X ヘッダの先頭 40B → 入口の判定。
///
/// 読めなかった / OS32X でない / 短い ものは [`Kind::Plain`] = 「ふつうに扱う」に
/// 倒す。ここで弾いても意味が無い (カーネルが `EXEC_ERR_INVALID` を返す) 上に、
/// 立てない側へ倒せば既存の起動経路は 1 つも変わらない。
///
/// `FORCE_CPL0` / `CUI_ONLY` は `FLAG_GFX` より強い — v86 の一部は両方立てている。
pub fn classify(hdr: &[u8]) -> Kind {
    if hdr.len() < HDR_SIZE {
        return Kind::Plain;
    }
    if le32(hdr, OFF_MAGIC) != MAGIC || (le32(hdr, OFF_HEADER_SIZE) as usize) < HDR_SIZE {
        return Kind::Plain;
    }
    let flags = le32(hdr, OFF_FLAGS);
    if flags & (FLAG_FORCE_CPL0 | FLAG_CUI_ONLY) != 0 {
        Kind::CuiOnly
    } else if flags & FLAG_GFX != 0 {
        Kind::FullScreen
    } else {
        Kind::Plain
    }
}

/// パスを開いて先頭 40B を読み、[`classify`] にかける。
///
/// `path` は**呼ぶ側の私有バッファ**で NUL 終端されていること (契約 §7.1 の 3)。
/// 開けない / 読めないときは [`Kind::Plain`] — 起動を試みて `exec_start` の
/// エラーを出す従来どおりの流れになる。
pub fn classify_path(path: &[u8]) -> Kind {
    let mut hdr = [0u8; HDR_SIZE];
    let a = unsafe { os32api::api() };
    /* KAPI_O_RDONLY = 0 (`os32_kapi_shared.h`)。`vfs_open` はディレクトリを
     * 拒むので、`/usr` のようなパスは「開けない」= Plain になる。 */
    let fd = unsafe { (a.sys_open)(path.as_ptr(), 0) };
    if fd < 0 {
        return Kind::Plain;
    }
    let n = unsafe { (a.sys_read)(fd, hdr.as_mut_ptr(), HDR_SIZE as u32) };
    unsafe { (a.sys_close)(fd) };
    if n < HDR_SIZE as i32 {
        return Kind::Plain;
    }
    classify(&hdr)
}

/// `/usr/bin/v86.bin` → `v86.bin` (`cui only: <名>` の表示用)。NUL / 末尾まで。
pub fn basename(path: &[u8]) -> &[u8] {
    let mut end = 0;
    while end < path.len() && path[end] != 0 {
        end += 1;
    }
    let mut start = 0;
    let mut i = 0;
    while i < end {
        if path[i] == b'/' {
            start = i + 1;
        }
        i += 1;
    }
    &path[start..end]
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hdr(flags: u32) -> [u8; HDR_SIZE] {
        let mut h = [0u8; HDR_SIZE];
        h[..4].copy_from_slice(&MAGIC.to_le_bytes());
        h[4..8].copy_from_slice(&(HDR_SIZE as u32).to_le_bytes());
        h[8..12].copy_from_slice(&2u32.to_le_bytes());
        h[12..16].copy_from_slice(&flags.to_le_bytes());
        h
    }

    /// 票 T8 D4 の入口判定 (純関数)。cpl0 / gfx / どちらも無し。
    #[test]
    fn header_flags_decide_the_entry() {
        assert_eq!(
            classify(&hdr(0x0002)),
            Kind::Plain,
            "RING3 だけなら普通のアプリ"
        );
        assert_eq!(classify(&hdr(FLAG_GFX | 0x0002)), Kind::FullScreen);
        assert_eq!(classify(&hdr(FLAG_FORCE_CPL0)), Kind::CuiOnly);
        /* v86 は --gfx と --cpl0 が両方立ちうる。CPL=0 が勝つ。 */
        assert_eq!(classify(&hdr(FLAG_GFX | FLAG_FORCE_CPL0)), Kind::CuiOnly);
        /* 票 T8-2: v86.bin は CPL=3 (flags に FORCE_CPL0 が無い) なので
         * CUI_ONLY 単独でも起動しない。 */
        assert_eq!(
            classify(&hdr(FLAG_CUI_ONLY)),
            Kind::CuiOnly,
            "CUI_ONLY だけでも cui only"
        );
        assert_eq!(classify(&hdr(FLAG_CUI_ONLY | 0x0002)), Kind::CuiOnly);
        /* CUI_ONLY は FLAG_GFX より強い (全画面モードへ入れない)。 */
        assert_eq!(
            classify(&hdr(FLAG_GFX | FLAG_CUI_ONLY)),
            Kind::CuiOnly,
            "CUI_ONLY が GFX に勝つ"
        );
        /* 読めなかった / OS32X でない / 短い → 立てない側へ倒す。 */
        assert_eq!(classify(&[]), Kind::Plain);
        assert_eq!(classify(&hdr(FLAG_FORCE_CPL0)[..39]), Kind::Plain);
        let mut bad = hdr(FLAG_FORCE_CPL0);
        bad[0] ^= 0xFF;
        assert_eq!(classify(&bad), Kind::Plain, "magic 違いは触らない");
        let mut short_hdr = hdr(FLAG_FORCE_CPL0);
        short_hdr[4] = 8;
        assert_eq!(classify(&short_hdr), Kind::Plain, "header_size が小さい");
    }

    #[test]
    fn basename_takes_the_last_segment() {
        assert_eq!(basename(b"/usr/bin/v86.bin\0"), b"v86.bin");
        assert_eq!(basename(b"v86.bin"), b"v86.bin");
        assert_eq!(basename(b"/\0"), b"");
    }
}
