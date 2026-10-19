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
 *   font_test -p   T7b-a6 計測: 3 書体の open、東雲で 1000 字 (seq、-s は scatter) の
 *                  cold/warm と内訳 (lookup だけ / 輪郭まで / raster まで) の tick・
 *                  read_at 回数・byte、1 字の最大 tick、作業域の大きさを出す。
 *   font_test -f   T7b-a6 計測: 空き FD を開けるだけ開いて数える (FD 残数)。
 *                  3 書体を開いたままの数も出す。開いた FD は全部閉じる。
 *
 * x87: アプリの FPU 文脈で動く (CPL=3 の前景)。gshell/製品の呼出しは
 * T7b-a2 の font_guard_run を通す (b3)。
 */
#![no_std]
#![no_main]

extern crate os32api;

use os32_font::{
    limits::{MAX_BITMAP_BYTES, READ_CACHE_SLOTS, READ_CACHE_SLOT_BYTES},
    outline::GlyphScratch,
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

/* T7b-a6 計測: Source 層の read_at (= read cache の miss) を数える。 */
static mut IO_CALLS: u32 = 0;
static mut IO_BYTES: u32 = 0;
static mut IO_TICKS: u32 = 0;
fn io_stats() -> (u32, u32, u32) {
    unsafe { (IO_CALLS, IO_BYTES, IO_TICKS) }
}

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
        let t0 = os32api::get_tick();
        let r = self.read_at_raw(offset, dst);
        unsafe {
            IO_CALLS += 1;
            IO_BYTES += dst.len() as u32;
            IO_TICKS += os32api::get_tick().wrapping_sub(t0);
        }
        r
    }
}
impl FdSource {
    fn read_at_raw(&mut self, offset: i32, dst: &mut [u8]) -> Result<usize> {
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

/* T7b-a7: Face は値で返さない (read cache + 常駐 cmap で約 51KB)。BSS に置いた
 * 器に open でその場で開く。和文・欧文・比較用の 3 つで、同じ器を同時に二重に借りない。 */
static mut FACE_JA: Face<FdSource> = Face::new();
static mut FACE_LATIN: Face<FdSource> = Face::new();
static mut FACE_EXTRA: Face<FdSource> = Face::new();
fn face_ja() -> &'static mut Face<FdSource> {
    unsafe { &mut *core::ptr::addr_of_mut!(FACE_JA) }
}
fn face_latin() -> &'static mut Face<FdSource> {
    unsafe { &mut *core::ptr::addr_of_mut!(FACE_LATIN) }
}
fn face_extra() -> &'static mut Face<FdSource> {
    unsafe { &mut *core::ptr::addr_of_mut!(FACE_EXTRA) }
}
/// 開けたら true (失敗した器は閉じたまま)。
fn open_at(face: &mut Face<FdSource>, path: &[u8]) -> bool {
    FdSource::open(path).is_some_and(|s| face.open(s).is_ok())
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
    let face = face_ja();
    if face.open(source).is_err() {
        os32api::kprint!(b"FONTCHK %s FAIL open\r\n\0", name.as_ptr());
        return;
    }
    let mut c = Check { glyphs: 0, black: 0, fallback: 0, fnv: 0x811c_9dc5 };
    for (first, last) in CHECK_RANGES {
        for cp in first..=last {
            let gid = match face.glyph_index(cp) {
                Ok(Some(g)) if g != 0 => g,
                _ => continue,
            };
            match work().render(face, gid, PixelSize::Px16, bits()) {
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
    face.close();
}

/* ---------------- T7b-a6 計測 (-p / -f) ---------------- */

static mut OUTLINE: GlyphScratch = GlyphScratch::new();
static mut CPS: [u32; PERF_CHARS] = [0; PERF_CHARS];
const PERF_CHARS: usize = 1000;

#[derive(Clone, Copy, PartialEq)]
enum Stage {
    Lookup,
    Outline,
    Raster,
}

/// 東雲の cmap にある字を workload の順に PERF_CHARS 字集める。
/// seq: かな 3040-30FF → 漢字 4E00- の符号順。scatter: 4E00-9FFF を
/// 7919 刻みで巡回 (20992 と互いに素 = 置換) — 文章の漢字のばらけ方の近似。
fn collect(face: &mut Face<FdSource>, scatter: bool) -> usize {
    let cps = unsafe { &mut *core::ptr::addr_of_mut!(CPS) };
    let mut n = 0;
    let mut push = |face: &mut Face<FdSource>, cp: u32, n: &mut usize| {
        if *n < PERF_CHARS && matches!(face.glyph_index(cp), Ok(Some(g)) if g != 0) {
            cps[*n] = cp;
            *n += 1;
        }
    };
    if scatter {
        for k in 0..0x5200u32 {
            push(face, 0x4e00 + (k * 7919) % 0x5200, &mut n);
        }
    } else {
        for cp in (0x3040..=0x30ffu32).chain(0x4e00..=0x9fffu32) {
            push(face, cp, &mut n);
        }
    }
    n
}

/// 1 pass: 時間 (tick)、read_at 回数・byte・その中の tick、1 字の最大 tick。
fn pass(face: &mut Face<FdSource>, n: usize, stage: Stage, label: &[u8]) {
    let cps = unsafe { &*core::ptr::addr_of!(CPS) };
    let (c0, b0, i0) = io_stats();
    let t0 = os32api::get_tick();
    let mut worst = 0u32;
    let mut black = 0u32;
    let mut fail = 0u32;
    for &cp in cps.iter().take(n) {
        let s = os32api::get_tick();
        let ok = match face.glyph_index(cp) {
            Ok(Some(gid)) if gid != 0 => match stage {
                Stage::Lookup => true,
                Stage::Outline => unsafe { (*core::ptr::addr_of_mut!(OUTLINE)).outline(face, gid).is_ok() },
                Stage::Raster => match work().render(face, gid, PixelSize::Px16, bits()) {
                    Ok(p) => {
                        for &b in bits().get(..p.bytes()).unwrap_or(&[]) {
                            black += b.count_ones();
                        }
                        true
                    }
                    Err(_) => false,
                },
            },
            _ => false,
        };
        if !ok {
            fail += 1;
        }
        worst = worst.max(os32api::get_tick().wrapping_sub(s));
    }
    let t = os32api::get_tick().wrapping_sub(t0);
    let (c1, b1, i1) = io_stats();
    os32api::kprint!(
        b"PERF %s n=%d t=%d rd=%d kb=%d io=%d max=%d blk=%d f=%d\r\n\0",
        label.as_ptr(), n as i32, t as i32, (c1 - c0) as i32, ((b1 - b0) / 1024) as i32,
        (i1 - i0) as i32, worst as i32, black as i32, fail as i32
    );
}

fn open_face(face: &mut Face<FdSource>, path: &[u8], name: &[u8]) -> bool {
    let (c0, b0, _) = io_stats();
    let t0 = os32api::get_tick();
    let ok = open_at(face, path);
    let t = os32api::get_tick().wrapping_sub(t0);
    let (c1, b1, _) = io_stats();
    os32api::kprint!(
        b"PERF open %s ok=%d t=%d rd=%d kb=%d res=%d\r\n\0",
        name.as_ptr(), ok as i32, t as i32, (c1 - c0) as i32, ((b1 - b0) / 1024) as i32,
        face.cmap_resident() as i32
    );
    ok
}

/// scatter: false = seq だけ (-p)、true = scatter だけ (-s)。1 画面に収める。
fn perf(scatter_only: bool) {
    os32api::kprint!(
        b"PERF size face=%d rs=%d gs=%d bmp=%d slots=%dx%d\r\n\0",
        core::mem::size_of::<Face<FdSource>>() as i32,
        core::mem::size_of::<RasterScratch>() as i32,
        core::mem::size_of::<GlyphScratch>() as i32,
        MAX_BITMAP_BYTES as i32, READ_CACHE_SLOTS as i32, READ_CACHE_SLOT_BYTES as i32
    );
    if !scatter_only {
        open_face(face_latin(), INCONSOLATA, b"inconsolata\0");
        face_latin().close();
        open_face(face_extra(), IPAEXG, b"ipaexg\0");
        face_extra().close();
    }
    for (scatter, name) in [(false, &b"seq\0"[..]), (true, &b"scatter\0"[..])] {
        if scatter != scatter_only {
            continue;
        }
        let face = face_ja();
        if !open_face(face, SHINONOME, b"shinonome\0") {
            return;
        }
        let n = collect(face, scatter);
        face.close();
        os32api::kprint!(b"PERF workload %s n=%d\r\n\0", name.as_ptr(), n as i32);
        /* 段ごとに開き直し、read cache を空にして cold で測る。 */
        for (stage, label) in [(Stage::Lookup, &b"lookup\0"[..]), (Stage::Outline, &b"outline\0"[..])] {
            if !open_face(face, SHINONOME, b"shinonome\0") {
                return;
            }
            pass(face, n, stage, label);
        }
        if !open_face(face, SHINONOME, b"shinonome\0") {
            return;
        }
        pass(face, n, Stage::Raster, b"cold\0");
        pass(face, n, Stage::Raster, b"warm\0");
        face.close();
    }
}

/// 空き FD を開けるだけ開いて数え、全部閉じる。
fn fd_probe(label: &[u8]) -> i32 {
    let mut fds = [-1i32; 16];
    let mut n = 0usize;
    unsafe {
        let a = os32api::api();
        while n < fds.len() {
            let fd = (a.sys_open)(IPAEXG.as_ptr(), os32api::fs::O_RDONLY);
            if fd < 0 {
                break;
            }
            fds[n] = fd;
            n += 1;
        }
        for &fd in fds.iter().take(n) {
            (a.sys_close)(fd);
        }
    }
    os32api::kprint!(b"FDPROBE %s free=%d\r\n\0", label.as_ptr(), n as i32);
    n as i32
}

fn fd_mode() {
    fd_probe(b"base\0");
    let a = open_at(face_ja(), SHINONOME);
    let b = open_at(face_latin(), INCONSOLATA);
    let c = open_at(face_extra(), IPAEXG);
    os32api::kprint!(b"FDPROBE faces=%d\r\n\0", a as i32 + b as i32 + c as i32);
    fd_probe(b"with3faces\0");
    face_ja().close();
    face_latin().close();
    face_extra().close();
    fd_probe(b"after\0");
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
fn draw_text(ja: &mut Face<FdSource>, latin: &mut Option<&mut Face<FdSource>>, x: i32, baseline: i32,
             text: &str, size: PixelSize, color: u8) -> i32 {
    let mut pen = x << 6;
    for ch in text.chars() {
        let cp = ch as u32;
        let placed = match latin.as_deref_mut() {
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

fn show(ja: &mut Face<FdSource>, latin: &mut Option<&mut Face<FdSource>>) {
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
    let opt = |c: u8| {
        argc > 1 && unsafe {
            let arg = *argv.add(1);
            !arg.is_null() && *arg == b'-' && *arg.add(1) == c && *arg.add(2) == 0
        }
    };
    let check = opt(b'c');
    if opt(b'f') {
        fd_mode();
        return 0;
    }

    os32api::print(b"font_test: os32-font 16ppem 1bpp raster (32px = 2x nearest)\r\n\0");
    let Some(source) = FdSource::open(SHINONOME) else {
        os32api::print(b"font_test: SKIP font not deployed (/data/JF-Dot-Shinonome16.ttf)\r\n\0");
        return 2;
    };
    if opt(b'p') || opt(b's') {
        drop(source);
        perf(opt(b's'));
        return 0;
    }
    if check {
        drop(source);
        check_face(SHINONOME, b"shinonome\0");
        check_face(INCONSOLATA, b"inconsolata\0");
        check_face(IPAEXG, b"ipaexg\0");
        return 0;
    }
    let ja = face_ja();
    if ja.open(source).is_err() {
        os32api::print(b"font_test: FAIL open /data/JF-Dot-Shinonome16.ttf\r\n\0");
        return 1;
    }
    let mut latin = if open_at(face_latin(), INCONSOLATA) { Some(face_latin()) } else { None };

    os32api::print(b"Press any key to start...\r\n\0");
    os32api::wait_key();
    unsafe {
        gfx::libos32gfx_init(api);
    }
    show(ja, &mut latin);
    os32api::wait_key();
    gfx::shutdown();
    ja.close();
    if let Some(face) = latin {
        face.close();
    }
    0
}
