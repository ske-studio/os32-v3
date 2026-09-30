//! info.rs — About に出す行の組み立て (KAPI にも GUI にも触らない純粋な部分)。
//!
//! **About の中身は `ver` (userland/shell/cmd_base.c の `cmd_ver`) の出力と同じ**
//! (ユーザー指示 2026-09-29)。行の並び・文言・条件 (KAPI v65 未満や
//! `boot_image_info` の失敗では Commit / Image CRC を出さない) を `ver` に合わせ、
//! 行頭の字下げ ("  ") だけを落とす。
//!
//! 食い違いはホスト試験 (`tools/tests/test_about_info.py`) が見つける: 実物の
//! `cmd_base.c` を `tools/tests/ver_about_host.c` で組んで `cmd_ver` を回した出力と、
//! このファイルの [`ver_lines`] の出力を同じ入力で 1 行ずつ突き合わせる。
//! `ver` の文言を変えたらこのファイルも直す (試験が落ちる)。
//!
//! 値の取得は `lib.rs` (KAPI)、ここは**値 → 表示の文字列**だけ。試験がこのファイルを
//! そのまま `#[path]` で取り込んで回すので、`os32api` / `libos32gui` を use しない。
//!
//! 行はすべて ASCII (kanji の約束 — `utf8_set_jis_table_ready` — を持ち込まない)。
//! 1 行は [`LINE_MAX`] バイトで打ち切る (ASCII なので文字の途中では切れない)。

/// 1 行の最大バイト数。窓の外形は最大 440px、枠 2px × 2 と column の余白 10px × 2 を
/// 引いた 416px が 8px の半角で 52 桁。`ver` の最長の行
/// ("Image CRC: ffffffff (4294967295 bytes, HDD loader)" の 50 桁) が入る。
pub const LINE_MAX: usize = 52;

/// `BootImageInfo.source` (1 = FD ローダ、2 = HDD ローダ、0 = 不明)。
pub const BOOT_SRC_FD: u8 = 1;
pub const BOOT_SRC_HDD: u8 = 2;

/// `boot_image_info` が入った KAPI の版 (v65)。`ver` と同じく、これ未満のカーネルでは
/// 呼ばず、Commit / Image CRC の行を出さない。
pub const KAPI_BOOT_IMAGE_INFO: u32 = 65;

/// Commit / Image CRC の行を出すか (`ver` の `version >= 65 && boot_image_info(&bi) == 0`)。
/// `rc` は `boot_image_info` の戻り値 (`kapi` が足りなければ呼ばないので見ない)。
pub fn boot_shown(kapi: u32, rc: i32) -> bool {
    kapi >= KAPI_BOOT_IMAGE_INFO && rc == 0
}

/// 行の数の上限 (見出し 1 + 機器 7 + API / Build / Commit / Image CRC)。
pub const NLINES_MAX: usize = 1 + VER_HW.len() + 4;

/* ================================================================ */
/*  1 行ぶんの書き込み先                                             */
/* ================================================================ */

/// 固定長の 1 行。あふれた分は捨てる (ASCII だけを入れる)。
pub struct Line {
    buf: [u8; LINE_MAX],
    n: usize,
}

impl Line {
    pub const fn new() -> Line {
        Line { buf: [0; LINE_MAX], n: 0 }
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.n]
    }

    /// バイト列を足す。NUL が来たらそこで止める (KAPI の C 文字列をそのまま渡せる)。
    pub fn s(&mut self, t: &[u8]) -> &mut Line {
        let mut i = 0;
        while i < t.len() && t[i] != 0 && self.n < LINE_MAX {
            self.buf[self.n] = t[i];
            self.n += 1;
            i += 1;
        }
        self
    }

    /// 10 進。
    pub fn dec(&mut self, v: u32) -> &mut Line {
        let mut tmp = [0u8; 10];
        let mut k = 0;
        let mut x = v;
        loop {
            tmp[k] = b'0' + (x % 10) as u8;
            k += 1;
            x /= 10;
            if x == 0 {
                break;
            }
        }
        while k > 0 {
            k -= 1;
            self.s(&tmp[k..k + 1]);
        }
        self
    }

    /// 16 進 8 桁 (小文字、`ver` の `%08x` と同じ)。
    pub fn hex8(&mut self, v: u32) -> &mut Line {
        let digits = b"0123456789abcdef";
        let mut sh: i32 = 28;
        while sh >= 0 {
            let d = ((v >> sh) & 0xF) as usize;
            self.s(&digits[d..d + 1]);
            sh -= 4;
        }
        self
    }
}

/* ================================================================ */
/*  SYS_VERSION (include/config.h が唯一の定義)                      */
/* ================================================================ */

/// `#define SYS_VERSION "x.y"` の引用符の中身を返す。見つからなければ `b"?"`。
///
/// `ver` はカーネル側で同じマクロを表示する。版を 2 か所に書かないために、
/// About は config.h 自体を取り込んで**コンパイル時に**ここで切り出す
/// (`const fn`、実行時に config.h を抱えない)。
pub const fn sys_version(cfg: &[u8]) -> &[u8] {
    let key = b"#define SYS_VERSION";
    let mut i = 0;
    while i + key.len() <= cfg.len() {
        /* 行頭だけを見る (コメントの中の同名は拾わない)。 */
        if i == 0 || cfg[i - 1] == b'\n' {
            let mut k = 0;
            while k < key.len() && cfg[i + k] == key[k] {
                k += 1;
            }
            if k == key.len() {
                let mut p = i + k;
                /* 名前の直後は空白でなければ別のマクロ (SYS_VERSION_X など)。 */
                if p < cfg.len() && (cfg[p] == b' ' || cfg[p] == b'\t') {
                    while p < cfg.len() && (cfg[p] == b' ' || cfg[p] == b'\t') {
                        p += 1;
                    }
                    if p < cfg.len() && cfg[p] == b'"' {
                        let s = p + 1;
                        let mut e = s;
                        while e < cfg.len() && cfg[e] != b'"' && cfg[e] != b'\n' {
                            e += 1;
                        }
                        if e < cfg.len() && cfg[e] == b'"' && e > s {
                            let (_, tail) = cfg.split_at(s);
                            let (v, _) = tail.split_at(e - s);
                            return v;
                        }
                    }
                }
            }
        }
        i += 1;
    }
    b"?"
}

/* ================================================================ */
/*  ver の行 (userland/shell/cmd_base.c の cmd_ver と同じ文言)       */
/* ================================================================ */

/// 1 行目 "PC-9801 OS32 v<版> (Ring3 Native)" の前後。
pub const VER_HEAD: &[u8] = b"PC-9801 OS32 v";
pub const VER_HEAD_TAIL: &[u8] = b" (Ring3 Native)";

/// `ver` が決まった文言で出す機器の行 (字下げを落とした形)。
pub const VER_HW: [&[u8]; 7] = [
    b"CPU: Intel 386+ (Protected Mode + Paging)",
    b"PIC: 8259A x2 (remapped to INT 20h+)",
    b"PIT: 8254 @ 100Hz",
    b"KBD: uPD8251A (IRQ1)",
    b"SER: uPD8251A RS-232C (IRQ4)",
    b"SND: YM2203 (OPN) FM3+SSG3",
    b"GFX: 640x400x16 CPU direct",
];

/// `boot_image_info` が返した中身 (lib.rs が `BootImageInfo` から写す)。
pub struct BootImage<'a> {
    pub crc_valid: bool,
    pub crc: u32,
    pub size: u32,
    pub source: u8,
    /// NUL 終端の C 文字列 (NUL より後ろは見ない)。
    pub commit: &'a [u8],
}

/// "PC-9801 OS32 v2.0 (Ring3 Native)"。
pub fn title_line(out: &mut Line, version: &[u8]) {
    out.s(VER_HEAD).s(version).s(VER_HEAD_TAIL);
}

/// "API: vN" (`KernelAPI.version`)。
pub fn kapi_line(out: &mut Line, version: u32) {
    out.s(b"API: v").dec(version);
}

/// "Build: <日時>" (`sys_get_build_info`、カーネルを組んだ日時)。
pub fn build_line(out: &mut Line, build: &[u8]) {
    out.s(b"Build: ").s(build);
}

/// "Commit: <id>" (`BootImageInfo.commit`、空なら空のまま — `ver` の `%s` と同じ)。
pub fn commit_line(out: &mut Line, commit: &[u8]) {
    out.s(b"Commit: ").s(commit);
}

/// ローダの名前 (`ver` の `source == 2 ? "HDD" : source == 1 ? "FD" : "?"`)。
pub fn loader_name(source: u8) -> &'static [u8] {
    if source == BOOT_SRC_HDD {
        b"HDD"
    } else if source == BOOT_SRC_FD {
        b"FD"
    } else {
        b"?"
    }
}

/// "Image CRC: xxxxxxxx (N bytes, HDD loader)" / "Image CRC: none (loader did not record)"。
pub fn crc_line(out: &mut Line, b: &BootImage) {
    out.s(b"Image CRC: ");
    if b.crc_valid {
        out.hex8(b.crc)
            .s(b" (")
            .dec(b.size)
            .s(b" bytes, ")
            .s(loader_name(b.source))
            .s(b" loader)");
    } else {
        out.s(b"none (loader did not record)");
    }
}

/// `ver` と同じ行を `out` の先頭から埋め、埋めた行の数を返す。
/// `boot` は [`boot_shown`] が真のときだけ `Some` (`ver` はそれ以外では
/// Commit / Image CRC を出さない)。
pub fn ver_lines(
    out: &mut [Line; NLINES_MAX],
    version: &[u8],
    kapi: u32,
    build: &[u8],
    boot: Option<&BootImage>,
) -> usize {
    let mut n = 0;
    title_line(&mut out[n], version);
    n += 1;
    let mut i = 0;
    while i < VER_HW.len() {
        out[n].s(VER_HW[i]);
        n += 1;
        i += 1;
    }
    kapi_line(&mut out[n], kapi);
    n += 1;
    build_line(&mut out[n], build);
    n += 1;
    if let Some(b) = boot {
        commit_line(&mut out[n], b.commit);
        n += 1;
        crc_line(&mut out[n], b);
        n += 1;
    }
    n
}
