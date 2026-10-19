/*
 * font_test — T7b-a5 の 1bpp ラスタ試験アプリ (Rust)
 *
 * 製品と同じ os32-font (逐次読み reader + 1 字の輪郭 + 16ppem の 1bpp ラスタ、
 * 32px は 16px の最近傍 2 倍) を CPL=3 のアプリから使う。ファイル全体は読まない
 * (FD から read_at で必要な所だけ、LRU の read cache 経由)。
 *
 * フォント (配布物には入っていない — 使う人が hsync などで /data に置く):
 *   /data/JF-Dot-Shinonome16.ttf  標準の和文 (必須。無ければ SKIP で終了 2)
 *   /data/Inconsolata-Regular.ttf 欧文 (任意)
 *   /data/ipaexg.ttf              IPAex ゴシック (任意、比較用)
 *
 *   font_test -c   画面を使わない照合: 各フォントの決まった符号位置の集合を全部
 *                  ラスタして FONTCHK 行 (字数・黒画素数・FNV-1a) を出す。値は
 *                  tools/tests/test_font_raster.py の EXPECT 行と比べる。
 *   font_test      見本の描画 (16px と 32px)。キーで戻る。
 *
 * x87: アプリの FPU 文脈で動く (CPL=3 の前景)。gshell/製品の呼出しは
 * T7b-a2 の font_guard_run を通す (b3)。
 */
#![no_std]
#![no_main]

extern crate os32api;

use os32_font::{
    limits::MAX_BITMAP_BYTES,
    raster::{PixelSize, Placement, RasterScratch},
    reader::{Error, Face, Result, Source},
};
use os32api::gfx;
use os32api::KernelAPI;

const SHINONOME: &[u8] = b"/data/JF-Dot-Shinonome16.ttf\0";
const INCONSOLATA: &[u8] = b"/data/Inconsolata-Regular.ttf\0";
const IPAEXG: &[u8] = b"/data/ipaexg.ttf\0";

/* 照合する符号位置の範囲 (cmap に字があるものだけ数える)。
 * test_font_raster.py の GUEST_RANGES と同じ。 */
const CHECK_RANGES: [(u32, u32); 4] = [(0x20, 0x7e), (0xa0, 0xff), (0x3000, 0x30ff), (0x4e00, 0x4fff)];

const SCREEN_W: i32 = 640;
const SCREEN_H: i32 = 400;

/// 開いた FD の位置指定読み (lseek + read)。短読はそのまま返す。
struct FdSource {
    fd: i32,
    size: u64,
}
impl FdSource {
    fn open(path: &[u8]) -> Option<Self> {
        unsafe {
            let a = os32api::api();
            let fd = (a.sys_open)(path.as_ptr(), os32api::fs::O_RDONLY);
            if fd < 0 {
                return None;
            }
            let size = (a.sys_lseek)(fd, 0, os32api::fs::SEEK_END);
            if size <= 0 {
                (a.sys_close)(fd);
                return None;
            }
            Some(Self { fd, size: size as u64 })
        }
    }
}
impl Drop for FdSource {
    fn drop(&mut self) {
        unsafe { (os32api::api().sys_close)(self.fd) };
    }
}
impl Source for FdSource {
    fn file_size(&self) -> u64 {
        self.size
    }
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> Result<usize> {
        let offset = i32::try_from(offset).map_err(|_| Error::Bounds)?;
        unsafe {
            let a = os32api::api();
            if (a.sys_lseek)(self.fd, offset, os32api::fs::SEEK_SET) != offset {
                return Err(Error::Io);
            }
            let mut got = 0usize;
            while got < dst.len() {
                let n = (a.sys_read)(self.fd, dst.as_mut_ptr().add(got), (dst.len() - got) as u32);
                if n < 0 {
                    return Err(Error::Io);
                }
                if n == 0 {
                    break;
                }
                got += n as usize;
            }
            Ok(got)
        }
    }
}

/* 作業域と 1 字の bitmap は BSS に置く (スタックを使わない)。 */
static mut WORK: RasterScratch = RasterScratch::new();
static mut BITS: [u8; MAX_BITMAP_BYTES] = [0; MAX_BITMAP_BYTES];

fn work() -> &'static mut RasterScratch {
    unsafe { &mut *core::ptr::addr_of_mut!(WORK) }
}
fn bits() -> &'static mut [u8; MAX_BITMAP_BYTES] {
    unsafe { &mut *core::ptr::addr_of_mut!(BITS) }
}

fn render(face: &mut Face<FdSource>, cp: u32, size: PixelSize) -> Option<Placement> {
    let gid = face.glyph_index(cp).ok()??;
    if gid == 0 {
        return None;
    }
    work().render(face, gid, size, bits()).ok()
}

/* ---------------- 照合 (-c) ---------------- */

struct Check {
    glyphs: u32,
    black: u32,
    fallback: u32,
    fnv: u32,
}
impl Check {
    fn bytes(&mut self, data: &[u8]) {
        for &b in data {
            self.fnv = (self.fnv ^ u32::from(b)).wrapping_mul(16_777_619);
        }
    }
}

fn check_face(path: &[u8], name: &[u8]) {
    let Some(source) = FdSource::open(path) else {
        os32api::kprint!(b"FONTCHK %s SKIP not deployed\r\n\0", name.as_ptr());
        return;
    };
    let Ok(mut face) = Face::open(source) else {
        os32api::kprint!(b"FONTCHK %s FAIL open\r\n\0", name.as_ptr());
        return;
    };
    let mut c = Check { glyphs: 0, black: 0, fallback: 0, fnv: 0x811c_9dc5 };
    for (first, last) in CHECK_RANGES {
        for cp in first..=last {
            let gid = match face.glyph_index(cp) {
                Ok(Some(g)) if g != 0 => g,
                _ => continue,
            };
            match work().render(&mut face, gid, PixelSize::Px16, bits()) {
                Ok(p) => {
                    c.glyphs += 1;
                    c.bytes(&cp.to_le_bytes());
                    c.bytes(&p.width.to_le_bytes());
                    c.bytes(&p.height.to_le_bytes());
                    c.bytes(&p.left.to_le_bytes());
                    c.bytes(&p.top.to_le_bytes());
                    c.bytes(&p.advance.to_le_bytes());
                    let image = bits().get(..p.bytes()).unwrap_or(&[]);
                    for &b in image {
                        c.black += b.count_ones();
                    }
                    c.bytes(image);
                }
                Err(_) => c.fallback += 1,
            }
        }
    }
    os32api::kprint!(
        b"FONTCHK %s glyphs=%d black=%d fallback=%d fnv=%x\r\n\0",
        name.as_ptr(),
        c.glyphs as i32,
        c.black as i32,
        c.fallback as i32,
        c.fnv
    );
}

/* ---------------- 見本の描画 ---------------- */

fn draw(x: i32, baseline: i32, p: &Placement, color: u8) {
    for row in 0..i32::from(p.height) {
        for col in 0..i32::from(p.width) {
            let at = row as usize * usize::from(p.pitch) + col as usize / 8;
            if bits().get(at).copied().unwrap_or(0) & (0x80 >> (col % 8)) == 0 {
                continue;
            }
            let px = x + i32::from(p.left) + col;
            let py = baseline - i32::from(p.top) + row;
            if (0..SCREEN_W).contains(&px) && (0..SCREEN_H).contains(&py) {
                unsafe { gfx::gfx_pixel(px, py, color) };
            }
        }
    }
}

/// 欧文は Inconsolata (あれば)、それ以外と欠字は東雲。戻りは送り後の x。
fn draw_text(ja: &mut Face<FdSource>, latin: &mut Option<Face<FdSource>>, x: i32, baseline: i32,
             text: &str, size: PixelSize, color: u8) -> i32 {
    let mut pen = x << 6;
    for ch in text.chars() {
        let cp = ch as u32;
        let placed = match latin {
            Some(face) if cp < 0x2000 => render(face, cp, size),
            _ => None,
        }
        .or_else(|| render(ja, cp, size));
        if let Some(p) = placed {
            draw((pen + 32) >> 6, baseline, &p, color);
            pen += p.advance;
        } else {
            pen += (8 * size.factor() as i32) << 6;
        }
    }
    (pen + 32) >> 6
}

fn show(ja: &mut Face<FdSource>, latin: &mut Option<Face<FdSource>>) {
    unsafe {
        gfx::gfx_clear(0);
        gfx::gfx_fill_rect(0, 0, SCREEN_W, 20, 1);
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(8, 4, b"T7b-a5 1bpp raster: JF Dot Shinonome16 / Inconsolata\0".as_ptr(), 7, 1);
    }
    let lines16 = [
        "16px: \u{8B70} \u{9B31} \u{8594}\u{8587} \u{6A5F}\u{68B0} \u{8A2D}\u{5B9A}",
        "16px: \u{3042}\u{3044}\u{3046} \u{30A2}\u{30A4}\u{30A6} \u{6F22}\u{5B57}",
        "16px: Hello, OS32! \u{00C5} \u{00E7} W M m g j y {}[]",
    ];
    let mut y = 44;
    for line in lines16 {
        draw_text(ja, latin, 16, y, line, PixelSize::Px16, 15);
        y += 20;
    }
    y += 24;
    for line in ["32px: \u{8B70}\u{9B31}\u{8594}\u{8587}\u{6A5F}\u{68B0}", "32px: \u{8A2D}\u{5B9A} OS32 \u{00C5}\u{00E7}"] {
        draw_text(ja, latin, 16, y, line, PixelSize::Px32, 15);
        y += 40;
    }
    gfx::present();
}

/* ================================================================ */
/*  メイン関数                                                       */
/* ================================================================ */
#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    os32api::os32_init(api);
    let check = argc > 1 && unsafe {
        let arg = *argv.add(1);
        !arg.is_null() && *arg == b'-' && *arg.add(1) == b'c' && *arg.add(2) == 0
    };

    os32api::print(b"font_test: os32-font 16ppem 1bpp raster (32px = 2x nearest)\r\n\0");
    let Some(source) = FdSource::open(SHINONOME) else {
        os32api::print(b"font_test: SKIP font not deployed (/data/JF-Dot-Shinonome16.ttf)\r\n\0");
        return 2;
    };
    if check {
        drop(source);
        check_face(SHINONOME, b"shinonome\0");
        check_face(INCONSOLATA, b"inconsolata\0");
        check_face(IPAEXG, b"ipaexg\0");
        return 0;
    }
    let Ok(mut ja) = Face::open(source) else {
        os32api::print(b"font_test: FAIL open /data/JF-Dot-Shinonome16.ttf\r\n\0");
        return 1;
    };
    let mut latin = FdSource::open(INCONSOLATA).and_then(|s| Face::open(s).ok());

    os32api::print(b"Press any key to start...\r\n\0");
    os32api::wait_key();
    unsafe {
        gfx::libos32gfx_init(api);
    }
    show(&mut ja, &mut latin);
    os32api::wait_key();
    gfx::shutdown();
    0
}
