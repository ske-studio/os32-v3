//! Explicit host substitutes: pixel RAM, font patterns, input samples and heap.
//! These are not evidence of guest KAPI/heap/backend correctness.
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Mutex,
};
pub const W: usize = 640;
pub const H: usize = 480;
static PIXELS: Mutex<Vec<u8>> = Mutex::new(Vec::new());
pub static READS: AtomicUsize = AtomicUsize::new(0);
pub static ALLOCS: AtomicUsize = AtomicUsize::new(0);
pub static FREES: AtomicUsize = AtomicUsize::new(0);
pub static MOUSE: Mutex<(i16, i16, u8)> = Mutex::new((0, 0, 0));
pub fn clear(c: u8) {
    *PIXELS.lock().unwrap() = vec![c; W * H];
}
pub fn pixels() -> Vec<u8> {
    PIXELS.lock().unwrap().clone()
}
#[no_mangle]
pub extern "C" fn gfx_pixel(x: i32, y: i32, c: u8) {
    if x >= 0 && y >= 0 && (x as usize) < W && (y as usize) < H {
        let mut p = PIXELS.lock().unwrap();
        if !p.is_empty() {
            p[y as usize * W + x as usize] = c;
        }
    }
}
#[no_mangle]
pub extern "C" fn gfx_get_pixel(x: i32, y: i32) -> u8 {
    if x >= 0 && y >= 0 && (x as usize) < W && (y as usize) < H {
        PIXELS
            .lock()
            .unwrap()
            .get(y as usize * W + x as usize)
            .copied()
            .unwrap_or(0)
    } else {
        0
    }
}
#[no_mangle]
pub extern "C" fn gfx_hline(x: i32, y: i32, w: i32, c: u8) {
    for xx in x..x + w {
        gfx_pixel(xx, y, c);
    }
}
#[no_mangle]
pub extern "C" fn gfx_vline(x: i32, y: i32, h: i32, c: u8) {
    for yy in y..y + h {
        gfx_pixel(x, yy, c);
    }
}
#[no_mangle]
pub extern "C" fn gfx_fill_rect(x: i32, y: i32, w: i32, h: i32, c: u8) {
    for yy in y..y + h {
        gfx_hline(x, yy, w, c);
    }
}
#[no_mangle]
pub extern "C" fn gfx_rect(x: i32, y: i32, w: i32, h: i32, c: u8) {
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}
#[no_mangle]
pub extern "C" fn gfx_line(mut x: i32, mut y: i32, x1: i32, y1: i32, c: u8) {
    let dx = (x1 - x).abs();
    let sx = if x < x1 { 1 } else { -1 };
    let dy = -(y1 - y).abs();
    let sy = if y < y1 { 1 } else { -1 };
    let mut e = dx + dy;
    loop {
        gfx_pixel(x, y, c);
        if x == x1 && y == y1 {
            break;
        }
        let e2 = 2 * e;
        if e2 >= dy {
            e += dy;
            x += sx;
        }
        if e2 <= dx {
            e += dx;
            y += sy;
        }
    }
}
/* gshell の文字は text.rs (gfx_draw_font) だけを通る (T7b-b4 S6)。
 * `kcg_draw_utf8` / `kcg_set_scale` の贋物は置かない — 呼べばリンクで落ちる。 */
/* lib/utf8.c の unicode_to_ank と同じ対応。全角は 0 (= FEP のセル幅判定が
 * 2 桁扱いにする)。以前は libos32term_render::ank を借りていたが、gshell は
 * もう T4/T5R に依存しないのでここへ写した。 */
#[no_mangle]
pub extern "C" fn unicode_to_ank(cp: u32) -> u8 {
    match cp {
        n @ 0x20..=0x7e => n as u8,
        0xa5 => 0x5c,
        n @ 0xff61..=0xff9f => (n - 0xff61 + 0xa1) as u8,
        n @ 0xff01..=0xff5e => (n - 0xff01 + 0x21) as u8,
        _ => 0,
    }
}
/* T7b-b3: text.rs の描画口。呼出しを記録し、画素も置く (透過、1bpp MSB 左)。 */
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FontDraw {
    pub x: i32,
    pub y: i32,
    pub w_bytes: i32,
    pub h_lines: i32,
    pub fg: u8,
    pub pat: Vec<u8>,
}
pub static FONT_DRAWS: Mutex<Vec<FontDraw>> = Mutex::new(Vec::new());
pub fn font_draws() -> Vec<FontDraw> {
    lk(&FONT_DRAWS).clone()
}
#[no_mangle]
pub unsafe extern "C" fn gfx_draw_font(x: i32, y: i32, pat: *const u8, w_bytes: i32, h_lines: i32, fg: u8) {
    let n = (w_bytes.max(0) * h_lines.max(0)) as usize;
    let bytes = std::slice::from_raw_parts(pat, n).to_vec();
    for row in 0..h_lines {
        for b in 0..w_bytes {
            let v = bytes[(row * w_bytes + b) as usize];
            for col in 0..8 {
                if v & (0x80 >> col) != 0 {
                    gfx_pixel(x + b * 8 + col, y + row, fg);
                }
            }
        }
    }
    lk(&FONT_DRAWS).push(FontDraw { x, y, w_bytes, h_lines, fg, pat: bytes });
}
/* KCG の ROM の代わり (0xa5 埋め)。どの字を引いたかは KCG_READS に残す。 */
pub static KCG_READS: Mutex<Vec<(char, u16)>> = Mutex::new(Vec::new());
pub fn kcg_reads() -> Vec<(char, u16)> {
    lk(&KCG_READS).clone()
}
unsafe extern "C" fn unicode_to_jis(cp: u32) -> u16 {
    match cp {
        0x4E9C => 0x3021, /* 亜 */
        0x3042 => 0x2422, /* あ */
        _ => 0,
    }
}
unsafe extern "C" fn ank(c: u8, p: *mut u8) {
    READS.fetch_add(1, Ordering::SeqCst);
    lk(&KCG_READS).push(('a', c as u16));
    std::ptr::write_bytes(p, 0xa5, 16);
}
unsafe extern "C" fn kanji(j: u16, p: *mut u8) {
    READS.fetch_add(1, Ordering::SeqCst);
    lk(&KCG_READS).push(('k', j));
    std::ptr::write_bytes(p, 0xa5, 32);
}
unsafe extern "C" fn alloc(n: u32) -> *mut u8 {
    ALLOCS.fetch_add(1, Ordering::SeqCst);
    std::alloc::alloc(std::alloc::Layout::from_size_align(n as usize, 4).unwrap())
}
/* 数えるだけ。gshell 側に mem_alloc を呼ぶ経路がもう無く、確保長を控えて
 * いないので、ここで dealloc すると layout 不一致になる。試験プロセスは
 * 短命なので意図的に leak させる。 */
unsafe extern "C" fn free(_p: *mut u8) {
    FREES.fetch_add(1, Ordering::SeqCst);
}
unsafe extern "C" fn zero() -> u32 {
    0
}
unsafe extern "C" fn zero_i32() -> i32 {
    0
}
unsafe extern "C" fn no_key() -> i32 {
    -1
}
unsafe extern "C" fn nothing() {}
pub static GFX_SHUTDOWNS: AtomicUsize = AtomicUsize::new(0);
unsafe extern "C" fn gfx_shutdown() { GFX_SHUTDOWNS.fetch_add(1, Ordering::SeqCst); }
unsafe extern "C" fn dirty(_: i32, _: i32, _: i32, _: i32) {
    DIRTY_RECTS.fetch_add(1, Ordering::SeqCst);
}
unsafe extern "C" fn present_dirty() {
    PRESENTS.fetch_add(1, Ordering::SeqCst);
}
unsafe extern "C" fn mouse(p: *mut u8) {
    let (x, y, b) = *lk(&MOUSE);
    std::ptr::write_bytes(p, 0, 10);
    p.cast::<i16>().write(x);
    p.add(2).cast::<i16>().write(y);
    p.add(8).write(b);
}
pub struct Shm {
    ptr: *mut u8,
    len: usize,
}
impl Shm {
    pub fn new() -> Self {
        unsafe extern "C" {
            fn mmap(p: *mut u8, n: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
        }
        let len = 0x30000 + 16 * 16384;
        let ptr = unsafe { mmap(core::ptr::null_mut(), len, 3, 0x22 | 0x40, -1, 0) };
        assert!(
            (ptr as usize) < u32::MAX as usize && !ptr.is_null(),
            "MAP_32BIT required for exact u32 SHM code"
        );
        Self { ptr, len }
    }
    pub fn base(&self) -> u32 {
        self.ptr as u32
    }
}
impl Drop for Shm {
    fn drop(&mut self) {
        unsafe extern "C" {
            fn munmap(p: *mut u8, n: usize) -> i32;
        }
        unsafe {
            munmap(self.ptr, self.len);
        }
    }
}
/// いま入っている 16 色 (`gfx_set_palette` / `gfx_get_palette` の実体)。
pub static PALETTE: Mutex<[u8; 48]> = Mutex::new([0; 48]);
/// `gfx_set_palette(i, r, g, b)` の呼び出し列 (退避→復元の順序の観測点)。
pub static PALETTE_SETS: Mutex<Vec<(i32, u8, u8, u8)>> = Mutex::new(Vec::new());
unsafe extern "C" fn palette(i: i32, r: u8, g: u8, b: u8) {
    lk(&PALETTE_SETS).push((i, r, g, b));
    if (0..16).contains(&i) {
        let mut p = lk(&PALETTE);
        p[i as usize * 3] = r;
        p[i as usize * 3 + 1] = g;
        p[i as usize * 3 + 2] = b;
    }
}
unsafe extern "C" fn get_palette(i: i32, r: *mut u8, g: *mut u8, b: *mut u8) {
    let p = *lk(&PALETTE);
    let k = if (0..16).contains(&i) { i as usize } else { 0 };
    r.write(p[k * 3]);
    g.write(p[k * 3 + 1]);
    b.write(p[k * 3 + 2]);
}
/// `gfx_set_palette` の呼び出し列 (複製)。
pub fn palette_sets() -> Vec<(i32, u8, u8, u8)> {
    lk(&PALETTE_SETS).clone()
}
unsafe extern "C" fn render(_: *mut u8) {}
unsafe extern "C" fn ime_idle(key: i32) -> i32 {
    if key < 0 || key == (0x60 << 8) { -1 } else { key }
}
/// テストが積む生キー (`kbd_trygetrawkey` が 1 件ずつ返す)。空なら -1。
pub static RAWKEYS: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// テストが積むカーネル FEP (`ime_feed_key`) の返り値。空なら 0x100 (Pass)。
pub static IME_SCRIPT: Mutex<Vec<i32>> = Mutex::new(Vec::new());
unsafe extern "C" fn raw_key() -> i32 {
    let mut q = lk(&RAWKEYS);
    if q.is_empty() {
        -1
    } else {
        q.remove(0)
    }
}
unsafe extern "C" fn ime_feed(keydata: i32) -> i32 {
    if keydata == (0x60 << 8) { return -1; } // lifetime cancel has no scripted output
    let mut q = lk(&IME_SCRIPT);
    if q.is_empty() {
        0x100
    } else {
        q.remove(0)
    }
}
unsafe extern "C" fn ime_active() -> i32 {
    1
}
/// FEP を on にして、`ime_feed_key` に `script` を仕込む。
pub fn fep_script(script: &[i32]) {
    unsafe {
        (*os32api::api_ptr()).ime_is_active = ime_active;
        (*os32api::api_ptr()).ime_feed_key = ime_feed;
    }
    *lk(&IME_SCRIPT) = script.to_vec();
}
pub fn push_rawkeys(keys: &[i32]) {
    lk(&RAWKEYS).extend_from_slice(keys);
}
/// `kbd_get_modifiers()` の答え (**現在値**、SHIFT_*)。gshell は起動時にしか
/// 読まない (票 KBD_NAV §1-5) — 試験は raw の列と食い違う値を置いて、
/// 現在値を読む実装を落とす。
pub static KBD_MODS: AtomicUsize = AtomicUsize::new(0);
pub fn set_kbd_mods(m: u32) {
    KBD_MODS.store(m as usize, Ordering::SeqCst);
}
unsafe extern "C" fn kbd_mods() -> u32 {
    KBD_MODS.load(Ordering::SeqCst) as u32
}

/* ================================================================ */
/*  K5b-W: アプリ 4 本の同時実行 (KAPI v44) の差し替え                */
/*                                                                  */
/*  `exec_park` はゲストでは longjmp して**戻らない**。ホストでは戻る */
/*  しかないので、呼ばれた回数だけを数えて `PARK_RET` を返す。        */
/*  制御の流れ (park → top-level → resume) はカーネルの領分で、       */
/*  `tools/tests/multiapp_impl_host.c` が実物の AppSlot で検査済み。  */
/*  ここで押さえるのは **WM の判断** (誰を起こすか / いつ譲るか)。    */
/* ================================================================ */

/// `exec_park` が呼ばれた回数。
pub static PARKS: AtomicUsize = AtomicUsize::new(0);
/// `exec_park` の戻り値 (既定 `OS32_ERR_INVAL` = 譲れなかった)。
pub static PARK_RET: Mutex<i32> = Mutex::new(-1);
/// `exec_resume(app_id, wait_ret)` の呼び出し列。
pub static RESUMES: Mutex<Vec<(i32, i32)>> = Mutex::new(Vec::new());
/// `exec_resume` の戻り値の台本 (先頭から 1 件ずつ。空なら app_id = また park)。
pub static RESUME_SCRIPT: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// `exec_start(cmdline)` の呼び出し列 (パス文字列)。
pub static STARTS: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());
/// `exec_start` の戻り値の台本 (空なら 2 = app_id 2 が park した)。
pub static START_SCRIPT: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// `exec_kill(app_id)` の呼び出し列。
pub static KILLS: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// `snd_focus(app_id)` の呼び出し列。
pub static SND_FOCUS: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// `exec_abort_clear()` が呼ばれた回数 (KAPI v45、決裁 A1)。
pub static ABORT_CLEARS: AtomicUsize = AtomicUsize::new(0);
/// `kbd_inject_pending()` が返す未読バイト数 (KAPI v47、票 K7 指摘 C)。
pub static INJECTED: Mutex<Vec<u8>> = Mutex::new(Vec::new());
pub static INJECT_CALLS: AtomicUsize = AtomicUsize::new(0);
pub static INJECT_LIMIT: AtomicUsize = AtomicUsize::new(usize::MAX);
pub static KBD_PENDING: AtomicUsize = AtomicUsize::new(0);
/// `exec_app_state(app_id)` が返す状態。添字 = app_id、既定は 2 (`PARKED`)。
pub static APP_STATE: Mutex<Vec<i32>> = Mutex::new(Vec::new());

/// MemStat の全体部 / ID 別 (flags, trim_epoch)。既定 mask 0。
pub static MEMSTAT_GLOBAL: Mutex<(u32, u32)> = Mutex::new((0, 0));
pub static MEMSTAT_APPS: Mutex<Vec<(u32, u32)>> = Mutex::new(Vec::new());
pub static MEMSTAT_CALLS: Mutex<Vec<(i32, u32)>> = Mutex::new(Vec::new());
unsafe extern "C" fn mem_stat(id: i32, out: *mut u8, size: u32) -> i32 {
    lk(&MEMSTAT_CALLS).push((id, size));
    assert!(size == 132 || size == 200, "MemStat prefix or full snapshot");
    std::ptr::write_bytes(out, 0, size as usize);
    let (pressure, mask) = *lk(&MEMSTAT_GLOBAL);
    std::ptr::copy_nonoverlapping(pressure.to_le_bytes().as_ptr(), out.add(120), 4);
    std::ptr::copy_nonoverlapping(mask.to_le_bytes().as_ptr(), out.add(124), 4);
    if id > 0 {
        let (flags, epoch) = lk(&MEMSTAT_APPS).get(id as usize).copied().unwrap_or((0, 0));
        std::ptr::copy_nonoverlapping(flags.to_le_bytes().as_ptr(), out.add(12), 4);
        std::ptr::copy_nonoverlapping(epoch.to_le_bytes().as_ptr(), out.add(128), 4);
    }
    /* 実物 (kapi_sys.c) は写したバイト数を返す。 */
    size as i32
}
pub fn set_memstat_app(id: i32, flags: u32, epoch: u32) {
    let mut v = lk(&MEMSTAT_APPS);
    if v.len() <= id as usize {
        v.resize(id as usize + 1, (0, 0));
    }
    v[id as usize] = (flags, epoch);
}

/* ---- 起動要求表 (KAPI v49、票 T9 D3) ---------------------------------- */
/// `launch_pending()` が返す本数。
pub static LAUNCH_PENDING: AtomicUsize = AtomicUsize::new(0);
/// `launch_take` が順に返す要求 (token, kind, arg, cmdline)。空なら 0 (無し)。
pub static TAKE_SCRIPT: Mutex<Vec<(i32, i32, i32, Vec<u8>)>> = Mutex::new(Vec::new());
/// `launch_take` が呼ばれた回数 (「STALE を再試行しない」の観測点)。
pub static TAKES: AtomicUsize = AtomicUsize::new(0);
/// `launch_report(token, rc)` の呼び出し列。
pub static REPORTS: Mutex<Vec<(i32, i32)>> = Mutex::new(Vec::new());
/// `launch_report` の戻り値 (既定 0。`OS32_ERR_STALE` = -11 を返させる試験がある)。
pub static REPORT_RET: Mutex<i32> = Mutex::new(0);
/// `launch_child(id)` の答え。添字 = id、既定 0 (連鎖の末尾)。
pub static LAUNCH_CHILD: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// `exec_kill(id)` で `FREE` (0) に落とす ID の列 (子孫ごと畳む K の振る舞い)。
pub static KILL_FREES: Mutex<Vec<i32>> = Mutex::new(Vec::new());
/// `get_tick()` が返す tick (D5 の「同じ tick に 2 回起こさない」の観測点)。
pub static TICK: AtomicUsize = AtomicUsize::new(0);
/// `get_tick()` が呼び出しごとに返す列 (空なら `TICK` をそのまま返す)。
pub static TICK_SCRIPT: Mutex<Vec<u32>> = Mutex::new(Vec::new());

/// 積まれている起動要求の本数を置く。
pub fn set_launch_pending(n: usize) {
    LAUNCH_PENDING.store(n, Ordering::SeqCst);
}
/// `launch_take` が返す要求を 1 本積む (取り出すと `launch_pending` が 1 減る)。
pub fn push_take(token: i32, kind: i32, arg: i32, cmdline: &[u8]) {
    lk(&TAKE_SCRIPT).push((token, kind, arg, cmdline.to_vec()));
    LAUNCH_PENDING.fetch_add(1, Ordering::SeqCst);
}
/// `launch_take` が呼ばれた回数。
pub fn take_calls() -> usize {
    TAKES.load(Ordering::SeqCst)
}
/// `launch_report(token, rc)` の呼び出し列 (複製)。
pub fn report_calls() -> Vec<(i32, i32)> {
    lk(&REPORTS).clone()
}
/// `launch_report` の戻り値を置く (-11 = `OS32_ERR_STALE`)。
pub fn set_report_ret(rc: i32) {
    *lk(&REPORT_RET) = rc;
}
/// `launch_child(id)` の答えを置く (0 = 連鎖の末尾)。
pub fn set_launch_child(id: i32, child: i32) {
    let mut v = lk(&LAUNCH_CHILD);
    if v.len() <= id as usize {
        v.resize(id as usize + 1, 0);
    }
    v[id as usize] = child;
}
/// `exec_kill` が `FREE` に落とす ID を置く (既定は空 = 状態を動かさない)。
pub fn set_kill_frees(ids: &[i32]) {
    *lk(&KILL_FREES) = ids.to_vec();
}
/// `get_tick()` が返す値を置く。
pub fn set_tick(t: u32) {
    TICK.store(t as usize, Ordering::SeqCst);
}
/// `get_tick()` を**呼び出しごとに**この列で返す (尽きたら最後の値のまま)。
/// 「選んでから再開するまでの間に PIT が進む」= tick 境界をまたぐ周を作る。
pub fn set_tick_script(ticks: &[u32]) {
    *lk(&TICK_SCRIPT) = ticks.to_vec();
}

unsafe extern "C" fn get_tick() -> u32 {
    let mut q = lk(&TICK_SCRIPT);
    if !q.is_empty() {
        let t = q.remove(0);
        /* 最後の 1 つはそのまま居座る (以後の呼び出しはこの値)。 */
        TICK.store(t as usize, Ordering::SeqCst);
        return t;
    }
    TICK.load(Ordering::SeqCst) as u32
}
unsafe extern "C" fn launch_pending() -> i32 {
    LAUNCH_PENDING.load(Ordering::SeqCst) as i32
}
unsafe extern "C" fn launch_take(
    buf: *mut u8,
    cap: u32,
    requester: *mut i32,
    kind: *mut i32,
    arg: *mut i32,
) -> i32 {
    TAKES.fetch_add(1, Ordering::SeqCst);
    /* 実物と同じ門: cap < LAUNCH_CMDLINE_MAX は断る (§1a)。 */
    if buf.is_null() || cap < 256 {
        return -2; /* OS32_ERR_INVAL */
    }
    let mut q = lk(&TAKE_SCRIPT);
    if q.is_empty() {
        return 0;
    }
    let (token, k, a, cmd) = q.remove(0);
    LAUNCH_PENDING.fetch_sub(1, Ordering::SeqCst);
    let n = core::cmp::min(cmd.len(), cap as usize - 1);
    std::ptr::copy_nonoverlapping(cmd.as_ptr(), buf, n);
    *buf.add(n) = 0;
    if !requester.is_null() {
        /* KILL は孤児回収 (-1)、LAUNCH は要求者 2 を既定にする。 */
        *requester = if k == 2 { -1 } else { 2 };
    }
    if !kind.is_null() {
        *kind = k;
    }
    if !arg.is_null() {
        *arg = a;
    }
    token
}
unsafe extern "C" fn launch_report(token: i32, rc: i32) -> i32 {
    lk(&REPORTS).push((token, rc));
    *lk(&REPORT_RET)
}
unsafe extern "C" fn launch_child(id: i32) -> i32 {
    lk(&LAUNCH_CHILD).get(id as usize).copied().unwrap_or(0)
}

/* ================================================================ */
/*  S4-W: 設定レジストリ (libos32cfg) の贋物 (票 S4 §5)              */
/*                                                                  */
/*  実 DB も SQLite も出てこない (それは S2-C の `test_cfg.py` の     */
/*  領分)。ここで押さえるのは **gshell の判断** —                     */
/*  「いつ開くか / 何を何回どの順で呼ぶか / いつ呼ばないか」。        */
/* ================================================================ */

/// `cfg_*` の呼び出し 1 件。**純粋な照会 (`cfg_status` / `cfg_last_sqlite` /
/// `cfg_schema_version` / `cfg_last_close_error`) は数えない** — 見たいのは
/// DB を動かす操作の順序だから。
#[derive(Clone, PartialEq, Eq, Debug)]
pub enum CfgCall {
    /// `cfg_open(&db, writable)`。
    Open(i32),
    /// `cfg_get_int(db, scope, key, def)`。
    GetInt(Vec<u8>, Vec<u8>),
    Begin,
    /// `cfg_set_int(db, scope, key, v)`。
    SetInt(Vec<u8>, Vec<u8>, i32),
    Commit,
    Rollback,
    Close,
}

/// 贋物の台本と記録。
pub struct CfgFake {
    /// `cfg_open` の戻り値 (負なら開かない)。
    pub open_ret: i32,
    /// 0 を返しながら `*out` に NULL を置く (ライブラリの契約違反の再現)。
    pub open_null: bool,
    /// `cfg_status` の答え (既定 `CFG_OK`)。
    pub status: i32,
    /// **n 本目の `cfg_get_int` を返した後**に [`CfgFake::status`] をこの値へ
    /// 変える (`Some((n, status))`)。「読んでいる途中の I/O 失敗」(票 §5 の
    /// (18)) を作るための仕掛け。
    ///
    /// 遷移の引き金を `cfg_status` の**呼び出し回数**ではなく **get の進み具合**
    /// に結び付けてあるのが要点 — status を get より前に採る実装 (取り逃がす
    /// ほう) は「まだ `CFG_OK`」を読むので、S18 がそこで落ちる。
    pub status_after_get: Option<(usize, i32)>,
    pub sqlite: i32,
    pub schema: i32,
    /// `desktop/color` の値 (None = 行が無い → `cfg_get_int` は def を返す)。
    pub color: Option<i32>,
    /// `taskbar/clock_24h` の値 (同上)。
    pub clock: Option<i32>,
    /// `font_set` / `font/gen` の値 (同上、T7b-b3)。
    pub font_set: Option<i32>,
    pub font_gen: Option<i32>,
    pub begin_ret: i32,
    pub set_ret: i32,
    /// `cfg_set_int` を**呼び出しごとに**この列で返す (尽きたら [`CfgFake::set_ret`])。
    /// 「1 本目は通り、2 本目の set だけ落ちる」(票 §5 の (20)(c)) を作る。
    pub set_ret_script: Vec<i32>,
    pub commit_ret: i32,
    pub close_ret: i32,
    /// `cfg_last_close_error()` の答え。
    pub last_close_error: i32,
    /// 呼び出しの列。
    pub calls: Vec<CfgCall>,
}

impl CfgFake {
    const NEW: CfgFake = CfgFake {
        open_ret: 0,
        open_null: false,
        status: 0, /* CFG_OK */
        status_after_get: None,
        sqlite: 0,
        schema: 1,
        color: None,
        clock: None,
        font_set: None,
        font_gen: None,
        begin_ret: 0,
        set_ret: 0,
        set_ret_script: Vec::new(),
        commit_ret: 0,
        close_ret: 0,
        last_close_error: 0,
        calls: Vec::new(),
    };
}

pub static CFG: Mutex<CfgFake> = Mutex::new(CfgFake::NEW);
/// `cfg_open` が返す不透明ハンドルの置き場 (中身は誰も読まない)。
static CFG_DB: AtomicUsize = AtomicUsize::new(0);

/// 台本を書き換える (`mocks::cfg(|c| c.status = CFG_MISSING)`)。
pub fn cfg<F: FnOnce(&mut CfgFake)>(f: F) {
    f(&mut lk(&CFG));
}
/// `cfg_*` の呼び出し列 (複製)。
pub fn cfg_calls() -> Vec<CfgCall> {
    lk(&CFG).calls.clone()
}
/// `cfg_open` が呼ばれた回数 (「呼ばれない」ことの観測点)。
pub fn cfg_open_calls() -> usize {
    lk(&CFG)
        .calls
        .iter()
        .filter(|c| matches!(c, CfgCall::Open(_)))
        .count()
}

#[no_mangle]
pub unsafe extern "C" fn cfg_open(out: *mut *mut os32api::cfg::CfgDb, writable: i32) -> i32 {
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::Open(writable));
    if c.open_ret < 0 {
        return c.open_ret;
    }
    if !out.is_null() {
        *out = if c.open_null {
            core::ptr::null_mut()
        } else {
            /* 触られない番地でよいが、NULL でなければ何でもよいわけではない
             * (実装が `is_null()` を見る)。静的な 1 語を指す。 */
            CFG_DB.store(1, Ordering::SeqCst);
            (&CFG_DB as *const AtomicUsize) as *mut os32api::cfg::CfgDb
        };
    }
    c.open_ret
}
#[no_mangle]
pub unsafe extern "C" fn cfg_close(_db: *mut os32api::cfg::CfgDb) -> i32 {
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::Close);
    c.close_ret
}
#[no_mangle]
pub unsafe extern "C" fn cfg_last_close_error() -> i32 {
    lk(&CFG).last_close_error
}
#[no_mangle]
pub unsafe extern "C" fn cfg_status(_db: *const os32api::cfg::CfgDb) -> i32 {
    lk(&CFG).status
}
#[no_mangle]
pub unsafe extern "C" fn cfg_last_sqlite(_db: *const os32api::cfg::CfgDb) -> i32 {
    lk(&CFG).sqlite
}
#[no_mangle]
pub unsafe extern "C" fn cfg_schema_version(_db: *const os32api::cfg::CfgDb) -> i32 {
    lk(&CFG).schema
}

unsafe fn cstr(p: *const u8) -> Vec<u8> {
    if p.is_null() {
        return Vec::new();
    }
    let mut n = 0;
    while *p.add(n) != 0 && n < 256 {
        n += 1;
    }
    std::slice::from_raw_parts(p, n).to_vec()
}

#[no_mangle]
pub unsafe extern "C" fn cfg_get_int(
    _db: *mut os32api::cfg::CfgDb,
    scope: *const u8,
    key: *const u8,
    def: i32,
) -> i32 {
    let (s, k) = (cstr(scope), cstr(key));
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::GetInt(s, k.clone()));
    /* 実物と同じ約束: 失敗も未設定も `def` (負値も正当な設定値なので、
     * 戻り値では区別しない)。 */
    let v = if k == b"desktop/color" {
        c.color
    } else if k == b"taskbar/clock_24h" {
        c.clock
    } else if k == b"font_set" {
        c.font_set
    } else if k == b"font/gen" {
        c.font_gen
    } else {
        None
    };
    /* 「n 本目の get を返した後に状態が変わる」— 読んでいる途中で prepare /
     * step が I/O で落ちた、を贋物で作る。数えるのは `cfg_get_int` の
     * 呼び出し回数 (この 1 件を押した後の値)。 */
    if let Some((n, s)) = c.status_after_get {
        let gets = c
            .calls
            .iter()
            .filter(|x| matches!(x, CfgCall::GetInt(_, _)))
            .count();
        if gets >= n {
            c.status = s;
        }
    }
    v.unwrap_or(def)
}
#[no_mangle]
pub unsafe extern "C" fn cfg_begin(_db: *mut os32api::cfg::CfgDb) -> i32 {
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::Begin);
    c.begin_ret
}
#[no_mangle]
pub unsafe extern "C" fn cfg_set_int(
    _db: *mut os32api::cfg::CfgDb,
    scope: *const u8,
    key: *const u8,
    v: i32,
) -> i32 {
    let (s, k) = (cstr(scope), cstr(key));
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::SetInt(s, k, v));
    if c.set_ret_script.is_empty() {
        c.set_ret
    } else {
        c.set_ret_script.remove(0)
    }
}
#[no_mangle]
pub unsafe extern "C" fn cfg_commit(_db: *mut os32api::cfg::CfgDb) -> i32 {
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::Commit);
    c.commit_ret
}
#[no_mangle]
pub unsafe extern "C" fn cfg_rollback(_db: *mut os32api::cfg::CfgDb) -> i32 {
    let mut c = lk(&CFG);
    c.calls.push(CfgCall::Rollback);
    0
}

/// `sys_time()` が返す epoch 秒 (時計の整形の観測点、票 S4 §5 の (3)(13))。
pub static SYS_TIME: AtomicUsize = AtomicUsize::new(0);
/// `sys_time()` の答えを置く。
pub fn set_sys_time(t: u32) {
    SYS_TIME.store(t as usize, Ordering::SeqCst);
}
unsafe extern "C" fn sys_time() -> u32 {
    SYS_TIME.load(Ordering::SeqCst) as u32
}

/// `kprintf` に渡った 1 行 (`"%s"` の実引数)。
pub static KPRINTS: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());
/// `kprintf` の行の列 (複製)。
pub fn kprint_lines() -> Vec<Vec<u8>> {
    lk(&KPRINTS).clone()
}
/// 可変長の `kprintf` は Rust では定義できない (`c_variadic` は unstable) ので、
/// **固定 3 引数**で書いて `transmute` で表へ入れる。gshell が渡すのは
/// `("%s", NUL 終端の 1 本)` だけなので、これで過不足なく読める。
unsafe extern "C" fn kprintf3(_attr: u8, _fmt: *const u8, arg: *const u8) {
    lk(&KPRINTS).push(cstr(arg));
}

/// 中毒 (panic 中に掴んでいた) した Mutex でも読めるようにする。検査が
/// `assert!(*X.lock().unwrap() == ..)` で落ちると次の試験まで巻き添えになる。
fn lk<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

/// `snd_focus` へ渡した owner の列 (複製。ロックを持ったまま assert しない)。
pub fn snd_focus_calls() -> Vec<i32> {
    lk(&SND_FOCUS).clone()
}
/// `exec_resume(app_id, wait_ret)` の呼び出し列 (複製)。
pub fn resume_calls() -> Vec<(i32, i32)> {
    lk(&RESUMES).clone()
}
/// `exec_kill(app_id)` の呼び出し列 (複製)。
pub fn kill_calls() -> Vec<i32> {
    lk(&KILLS).clone()
}
/// `exec_start(cmdline)` の呼び出し列 (複製)。
pub fn start_calls() -> Vec<Vec<u8>> {
    lk(&STARTS).clone()
}
/// `exec_abort_clear()` が呼ばれた回数。
pub fn abort_clear_calls() -> usize {
    ABORT_CLEARS.load(Ordering::SeqCst)
}

/// `gfx_init` が呼ばれた回数。
pub static GFX_INITS: AtomicUsize = AtomicUsize::new(0);
/// `gfx_add_dirty_rect` に積まれた矩形の数 (票 T8 D4a の「描かない」の観測点)。
pub static DIRTY_RECTS: AtomicUsize = AtomicUsize::new(0);
/// `gfx_present_dirty` が呼ばれた回数。
pub static PRESENTS: AtomicUsize = AtomicUsize::new(0);
/// `gfx_screen_owner()` が返す画面の所有者 (KAPI v48。1 = WM、2〜5 = アプリ)。
pub static SCREEN_OWNER: AtomicUsize = AtomicUsize::new(1);
/// `sys_open` が開ける「ファイル」の中身 (空 = 開けない)。OS32X ヘッダ用。
pub static FILE_BYTES: Mutex<Vec<u8>> = Mutex::new(Vec::new());
/// `sys_open` に渡ったパスの列 (入口がヘッダを読んだことの観測点)。
pub static OPENS: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());
/// 開いている `fd` の読み出し位置 (単一 fd を前提にした最小の模型)。
static FILE_POS: AtomicUsize = AtomicUsize::new(0);

/// 画面の所有者を置く (1 = WM、2〜5 = アプリ)。
pub fn set_screen_owner(id: i32) {
    SCREEN_OWNER.store(id as usize, Ordering::SeqCst);
}
/// `gfx_add_dirty_rect` / `gfx_present_dirty` の回数 (積んだ矩形数, present 数)。
pub fn present_counts() -> (usize, usize) {
    (
        DIRTY_RECTS.load(Ordering::SeqCst),
        PRESENTS.load(Ordering::SeqCst),
    )
}
/// 次に `sys_open` で開けるファイルの中身を置く (空 = 開けない)。
pub fn set_file(bytes: &[u8]) {
    *lk(&FILE_BYTES) = bytes.to_vec();
}
/// OS32X ヘッダ 40B を組み立てる (`sdk/include/os32/os32_kapi_shared.h`)。
pub fn os32x_header(flags: u32) -> Vec<u8> {
    let mut h = vec![0u8; 40];
    h[0..4].copy_from_slice(&0x4F53_3332u32.to_le_bytes());
    h[4..8].copy_from_slice(&40u32.to_le_bytes());
    h[8..12].copy_from_slice(&2u32.to_le_bytes());
    h[12..16].copy_from_slice(&flags.to_le_bytes());
    h
}
/// `sys_open` に渡ったパスの列 (複製)。
pub fn open_calls() -> Vec<Vec<u8>> {
    lk(&OPENS).clone()
}

unsafe extern "C" fn screen_owner() -> i32 {
    SCREEN_OWNER.load(Ordering::SeqCst) as i32
}
unsafe extern "C" fn sys_open(path: *const u8, _mode: i32) -> i32 {
    let mut n = 0;
    while *path.add(n) != 0 && n < 256 {
        n += 1;
    }
    lk(&OPENS).push(std::slice::from_raw_parts(path, n).to_vec());
    if lk(&FILE_BYTES).is_empty() {
        return -1;
    }
    FILE_POS.store(0, Ordering::SeqCst);
    7
}
unsafe extern "C" fn sys_read(_fd: i32, buf: *mut u8, size: u32) -> i32 {
    let f = lk(&FILE_BYTES);
    let pos = FILE_POS.load(Ordering::SeqCst);
    let n = core::cmp::min(size as usize, f.len().saturating_sub(pos));
    std::ptr::copy_nonoverlapping(f.as_ptr().add(pos), buf, n);
    FILE_POS.store(pos + n, Ordering::SeqCst);
    n as i32
}
unsafe extern "C" fn sys_close(_fd: i32) {}

/// ゲストの `gfx_init` (`gfx/gfx_core.c`) は **VRAM の両ページをゼロクリア
/// する**。「消えたのに描き直させない」不具合 (W-1) をホストで再現するには
/// そこまで模す必要がある — 何もしない代用だと、画が消えたことを試験が
/// 観測できず検査が空振りする。
unsafe extern "C" fn gfx_init() {
    GFX_INITS.fetch_add(1, Ordering::SeqCst);
    clear(0);
}

unsafe extern "C" fn exec_park() -> i32 {
    PARKS.fetch_add(1, Ordering::SeqCst);
    *lk(&PARK_RET)
}
unsafe extern "C" fn exec_resume(app_id: i32, wait_ret: i32) -> i32 {
    lk(&RESUMES).push((app_id, wait_ret));
    let mut q = lk(&RESUME_SCRIPT);
    if q.is_empty() {
        app_id
    } else {
        q.remove(0)
    }
}
unsafe extern "C" fn exec_start(cmdline: *const u8) -> i32 {
    let mut n = 0;
    while *cmdline.add(n) != 0 && n < 256 {
        n += 1;
    }
    lk(&STARTS).push(std::slice::from_raw_parts(cmdline, n).to_vec());
    let mut q = lk(&START_SCRIPT);
    if q.is_empty() {
        2
    } else {
        q.remove(0)
    }
}
unsafe extern "C" fn exec_kill(app_id: i32) -> i32 {
    lk(&KILLS).push(app_id);
    /* 実物の `exec_kill` は連鎖の子孫ごと畳む (票 T9 D8)。畳んだ結果
     * `exec_app_state` が `FREE` になる ID を試験が置く (既定は空)。 */
    let freed = lk(&KILL_FREES).clone();
    for id in freed {
        set_app_state(id, 0);
    }
    0
}
unsafe extern "C" fn snd_focus(app_id: i32) -> i32 {
    lk(&SND_FOCUS).push(app_id);
    0
}
/* KAPI v45 (K5c、決裁 A1)。owner 1 (WM top-level) からしか呼べないので、
 * ゲストでは `op_wait` の中から呼ぶと `OS32_ERR_INVAL`。ここは回数を数える
 * だけ — 「どこから呼んだか」はカーネル側の領分
 * (`tools/tests/multiapp_impl_host.c` ケース 20 が実物の AppSlot で検査済み)。 */
unsafe extern "C" fn exec_abort_clear() -> i32 {
    ABORT_CLEARS.fetch_add(1, Ordering::SeqCst);
    0
}
/* KAPI v47 (K7)。注入リングはカーネルの持ち物なので、ここは試験が置いた
 * 「未読バイト数」と「その ID の状態」をそのまま返すだけ — 注入と取り出しの
 * 実体は `tools/tests/kbd_inject_host.c` が実物で検査済み。 */
unsafe extern "C" fn kbd_inject(bytes: *const u8, len: u32) -> i32 {
    INJECT_CALLS.fetch_add(1, Ordering::SeqCst);
    let len = len.min(INJECT_LIMIT.load(Ordering::SeqCst) as u32);
    lk(&INJECTED).extend_from_slice(std::slice::from_raw_parts(bytes, len as usize));
    // WM bytes are not visible through the WM's legacy pending query.
    len as i32
}
unsafe extern "C" fn kbd_inject_pending() -> u32 {
    KBD_PENDING.load(Ordering::SeqCst) as u32
}
unsafe extern "C" fn exec_app_state(app_id: i32) -> i32 {
    lk(&APP_STATE).get(app_id as usize).copied().unwrap_or(2)
}
/// 注入リングの未読バイト数を置く (0 = 空)。
pub fn set_kbd_pending(n: u32) {
    KBD_PENDING.store(n as usize, Ordering::SeqCst);
}
/// `exec_app_state(app_id)` の答えを置く (3 = `APP_STATE_WAIT_KEY`)。
pub fn set_app_state(app_id: i32, state: i32) {
    let mut v = lk(&APP_STATE);
    if v.len() <= app_id as usize {
        v.resize(app_id as usize + 1, 2);
    }
    v[app_id as usize] = state;
}

pub static SERIAL_IN: Mutex<Vec<u8>> = Mutex::new(Vec::new());
pub static SERIAL_OUT: Mutex<Vec<u8>> = Mutex::new(Vec::new());
pub static SERIAL_INITIALIZED: AtomicUsize = AtomicUsize::new(0);
pub static RSHELL_ACTIVE: Mutex<Vec<i32>> = Mutex::new(Vec::new());
pub static SWITCHES: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());
unsafe extern "C" fn serial_trygetchar() -> i32 {
    let mut q = lk(&SERIAL_IN);
    if q.is_empty() { -1 } else { q.remove(0) as i32 }
}
unsafe extern "C" fn serial_puts(s: *const u8) {
    let mut n = 0; while *s.add(n) != 0 { n += 1; }
    lk(&SERIAL_OUT).extend_from_slice(std::slice::from_raw_parts(s,n));
}
unsafe extern "C" fn serial_initialized() -> i32 { SERIAL_INITIALIZED.load(Ordering::SeqCst) as i32 }
unsafe extern "C" fn rshell_active(active: i32) { lk(&RSHELL_ACTIVE).push(active); }
unsafe extern "C" fn switch_shell(s: *const u8) -> i32 {
    let mut n = 0; while *s.add(n) != 0 { n += 1; }
    lk(&SWITCHES).push(std::slice::from_raw_parts(s,n).to_vec()); 0
}

pub fn init() {
    unsafe { gfx_ready = 1; }
    let mut a = os32api::mock_api();
    a.get_tick = get_tick;
    a.serial_trygetchar = serial_trygetchar;
    a.serial_puts = serial_puts;
    a.serial_is_initialized = serial_initialized;
    a.rshell_set_active = rshell_active;
    a.sys_switch_shell = switch_shell;
    lk(&SERIAL_IN).clear(); lk(&SERIAL_OUT).clear(); lk(&RSHELL_ACTIVE).clear(); lk(&SWITCHES).clear();
    SERIAL_INITIALIZED.store(0, Ordering::SeqCst);
    a.sys_time = sys_time;
    a.kbd_dropped_count = zero;
    a.kbd_trygetrawkey = raw_key;
    a.kbd_get_modifiers = kbd_mods;
    KBD_MODS.store(0, Ordering::SeqCst);
    a.mouse_poll = mouse;
    a.gfx_init = gfx_init;
    a.gfx_screen_info = screen_info;
    a.gfx_shutdown = gfx_shutdown;
    GFX_SHUTDOWNS.store(0, Ordering::SeqCst);
    a.tvram_clear = nothing;
    a.gfx_set_palette = palette;
    a.gfx_get_palette = get_palette;
    a.ime_set_render = render;
    a.ime_is_active = zero_i32;
    a.ime_feed_key = ime_idle;
    a.kcg_read_ank = ank;
    a.kcg_read_kanji = kanji;
    a.unicode_to_jis = unicode_to_jis;
    lk(&FONT_DRAWS).clear();
    lk(&KCG_READS).clear();
    a.mem_alloc = alloc;
    a.mem_free = free;
    a.gfx_add_dirty_rect = dirty;
    a.gfx_present_dirty = present_dirty;
    /* 票 T8: 画面の所有者 (KAPI v48) と OS32X ヘッダの読み取り。 */
    a.gfx_screen_owner = screen_owner;
    a.sys_open = sys_open;
    a.sys_read = sys_read;
    a.sys_close = sys_close;
    /* `op_wait` / 単独ループの待ち。ホストでは何もしない (時計は get_tick 側)。 */
    a.sys_halt = nothing;
    a.exec_park = exec_park;
    a.exec_resume = exec_resume;
    a.exec_start = exec_start;
    a.exec_kill = exec_kill;
    a.exec_abort_clear = exec_abort_clear;
    a.snd_focus = snd_focus;
    a.kbd_inject = kbd_inject;
    lk(&INJECTED).clear(); INJECT_CALLS.store(0, Ordering::SeqCst);
    a.kbd_inject_pending = kbd_inject_pending;
    a.exec_app_state = exec_app_state;
    a.mem_stat = mem_stat;
    *lk(&MEMSTAT_GLOBAL) = (0, 0);
    lk(&MEMSTAT_APPS).clear();
    lk(&MEMSTAT_CALLS).clear();
    crate::trim::reset();
    /* 票 T9 (KAPI v49): 起動要求表。表そのものの遷移は実物で検査済み
     * (`tools/tests/launch_host.c`)。ここは WM が「いつ・何を渡したか」だけ見る。 */
    a.launch_pending = launch_pending;
    a.launch_take = launch_take;
    a.launch_report = launch_report;
    a.launch_child = launch_child;
    lk(&RAWKEYS).clear();
    lk(&IME_SCRIPT).clear();
    GFX_INITS.store(0, Ordering::SeqCst);
    DIRTY_RECTS.store(0, Ordering::SeqCst);
    PRESENTS.store(0, Ordering::SeqCst);
    SCREEN_OWNER.store(1, Ordering::SeqCst);
    FILE_POS.store(0, Ordering::SeqCst);
    lk(&FILE_BYTES).clear();
    lk(&OPENS).clear();
    *lk(&PALETTE) = [0; 48];
    lk(&PALETTE_SETS).clear();
    crate::fullscreen::reset();
    PARKS.store(0, Ordering::SeqCst);
    *lk(&PARK_RET) = -1;
    lk(&RESUMES).clear();
    lk(&RESUME_SCRIPT).clear();
    lk(&STARTS).clear();
    lk(&START_SCRIPT).clear();
    lk(&KILLS).clear();
    lk(&SND_FOCUS).clear();
    ABORT_CLEARS.store(0, Ordering::SeqCst);
    KBD_PENDING.store(0, Ordering::SeqCst);
    INJECT_LIMIT.store(usize::MAX, Ordering::SeqCst);
    lk(&APP_STATE).clear();
    LAUNCH_PENDING.store(0, Ordering::SeqCst);
    TAKES.store(0, Ordering::SeqCst);
    TICK.store(0, Ordering::SeqCst);
    lk(&TICK_SCRIPT).clear();
    lk(&TAKE_SCRIPT).clear();
    lk(&REPORTS).clear();
    *lk(&REPORT_RET) = 0;
    lk(&LAUNCH_CHILD).clear();
    lk(&KILL_FREES).clear();
    /* 票 S4 (設定レジストリ)。贋物の台本と記録、gshell 側の私有状態を戻す。 */
    a.kprintf = unsafe { core::mem::transmute::<*const (), _>(kprintf3 as *const ()) };
    *lk(&CFG) = CfgFake::NEW;
    lk(&KPRINTS).clear();
    SYS_TIME.store(0, Ordering::SeqCst);
    crate::settings::reset();
    crate::modal::reset();
    crate::startmenu::reset();
    crate::taskbar::reset();
    crate::multiapp::reset();
    os32api::os32_init(Box::into_raw(Box::new(a)));
    /* T7b-b3: font engine (lease は表が無ければ閉じに行かない)。 */
    unsafe { __os32_aux = core::ptr::null() };
    crate::font::reset();
    clear(9);
}

// e7: C SDK is mocked here; real attach is exercised by gfx_reattach_host.c.
pub static FB_ATTACHES: AtomicUsize = AtomicUsize::new(0);
pub static SCREEN_READS: AtomicUsize = AtomicUsize::new(0);
#[no_mangle]
pub static mut gfx_ready: i32 = 1;
// T7b-b2: CRT0 の __os32_aux (sdk/crt/crt0_c.c) の代わり。既定は app と同じ 0。
#[no_mangle]
pub static mut __os32_aux: *const core::ffi::c_void = core::ptr::null();
#[no_mangle]
pub unsafe extern "C" fn libos32gfx_init(api: *mut os32api::KernelAPI) {
    ((*api).gfx_init)();
    FB_ATTACHES.fetch_add(1, Ordering::SeqCst);
}
#[no_mangle]
pub unsafe extern "C" fn libos32gfx_shutdown() { (os32api::api().gfx_shutdown)(); }
#[no_mangle]
pub unsafe extern "C" fn gfx_present() { (os32api::api().gfx_add_dirty_rect)(0, 0, W as i32, H as i32); }
unsafe extern "C" fn screen_info(out: *mut u8) {
    SCREEN_READS.fetch_add(1, Ordering::SeqCst);
    let mut info = os32api::gui::types::ScreenInfo::ZERO;
    info.width = 640; info.height = 400; info.bpp = 4;
    *(out as *mut os32api::gui::types::ScreenInfo) = info;
}

/* ================================================================ */
/*  T7b-b3 S3: shell service 表 v2 の贋物 (font の lease と guard)     */
/*                                                                  */
/*  lease は実ファイル (OS32_GSHELL_FONT_DIR の標準 set、integration */
/*  .py が fetch_fonts で揃える) を名前の basename で開く。guard は   */
/*  callback を 1 回呼ぶだけで、その中で read_at 以外が呼ばれたら     */
/*  `violations` に数える (kernel/font_guard.h の約束)。               */
/* ================================================================ */
pub mod fontsvc {
    use super::lk;
    use crate::ffi::{ShellService, SHELL_SERVICE_MAGIC, SHELL_SERVICE_VERSION};
    use std::collections::HashMap;
    use std::sync::{Arc, Mutex};

    pub const ERR_IO: i32 = -1;

    #[derive(Default)]
    pub struct Fake {
        /// id - 1 → 開いている lease の中身 (None = 閉じた)。
        pub leases: Vec<Option<Arc<Vec<u8>>>>,
        /// (呼んだ入口, guard の中だったか)。
        pub calls: Vec<(&'static str, bool)>,
        pub in_guard: bool,
        /// guard の中で read_at 以外を呼んだ回数。
        pub violations: usize,
        /// basename がこれで終わる path の open を rc で断る。
        pub open_fail: Option<(&'static str, i32)>,
        /// read_at を全部 ERR_IO にする。
        pub read_fail: bool,
        /// validate の答え (0 = OK)。
        pub validate_ret: i32,
        /// guard を callback を呼ばずにこの rc で返す。
        pub guard_ret: Option<i32>,
        /// close をこの rc で断る (lease は開いたまま — WM の中の段 B を模す)。
        pub close_fail: Option<i32>,
        pub reads: usize,
        /// TASK_FONTPERF F3: 束 (.g16) を置く (false = その open は NOTFOUND)。
        pub g16: bool,
    }
    pub static FAKE: Mutex<Option<Fake>> = Mutex::new(None);
    static FILES: Mutex<Option<HashMap<String, Arc<Vec<u8>>>>> = Mutex::new(None);

    pub fn with<R>(f: impl FnOnce(&mut Fake) -> R) -> R {
        let mut g = lk(&FAKE);
        f(g.get_or_insert_with(Fake::default))
    }
    /// 開いている lease の本数。
    pub fn open_count() -> usize {
        with(|f| f.leases.iter().filter(|l| l.is_some()).count())
    }
    pub fn calls() -> Vec<(&'static str, bool)> {
        with(|f| f.calls.clone())
    }
    pub fn count(op: &str) -> usize {
        with(|f| f.calls.iter().filter(|c| c.0 == op).count())
    }

    fn file(name: &str) -> Option<Arc<Vec<u8>>> {
        let mut g = lk(&FILES);
        let map = g.get_or_insert_with(HashMap::new);
        if let Some(v) = map.get(name) {
            return Some(v.clone());
        }
        let path = if name.ends_with(".g16") {
            /* 束は integration.py が同じ TTF から作ったもの。 */
            std::path::PathBuf::from(std::env::var("OS32_GSHELL_G16").ok()?)
        } else {
            std::path::Path::new(&std::env::var("OS32_GSHELL_FONT_DIR").ok()?).join(name)
        };
        let bytes = std::fs::read(path).ok()?;
        let v = Arc::new(bytes);
        map.insert(name.to_string(), v.clone());
        Some(v)
    }

    fn note(f: &mut Fake, op: &'static str) {
        let g = f.in_guard;
        f.calls.push((op, g));
        if g && op != "read_at" {
            f.violations += 1;
        }
    }

    unsafe extern "C" fn open(path: *const u8, out_id: *mut u32, out_size: *mut u32) -> i32 {
        let mut n = 0;
        while *path.add(n) != 0 {
            n += 1;
        }
        let p = std::str::from_utf8(std::slice::from_raw_parts(path, n)).unwrap().to_string();
        let base = p.rsplit('/').next().unwrap().to_string();
        let fail = with(|f| {
            note(f, "open");
            f.open_fail.filter(|(s, _)| base.ends_with(s)).map(|(_, rc)| rc)
        });
        if let Some(rc) = fail {
            return rc;
        }
        if base.ends_with(".g16") && !with(|f| f.g16) {
            return -2;
        }
        let data = match file(&base) {
            Some(d) => d,
            None => return -2,
        };
        with(|f| {
            f.leases.push(Some(data.clone()));
            *out_id = f.leases.len() as u32;
        });
        *out_size = data.len() as u32;
        0
    }
    unsafe extern "C" fn read_at(id: u32, off: u32, dst: *mut u8, len: u32) -> i32 {
        let data = with(|f| {
            note(f, "read_at");
            f.reads += 1;
            if f.read_fail {
                return Err(ERR_IO);
            }
            f.leases.get(id as usize - 1).cloned().flatten().ok_or(-11)
        });
        let data = match data {
            Ok(d) => d,
            Err(rc) => return rc,
        };
        let off = off as usize;
        if off > data.len() {
            return -9;
        }
        let n = (len as usize).min(data.len() - off);
        std::ptr::copy_nonoverlapping(data.as_ptr().add(off), dst, n);
        n as i32
    }
    unsafe extern "C" fn validate(id: u32) -> i32 {
        with(|f| {
            note(f, "validate");
            if f.leases.get(id as usize - 1).map_or(true, |l| l.is_none()) {
                return -11;
            }
            f.validate_ret
        })
    }
    unsafe extern "C" fn close(id: u32) -> i32 {
        with(|f| {
            note(f, "close");
            if let Some(rc) = f.close_fail {
                return rc;
            }
            match f.leases.get_mut(id as usize - 1) {
                Some(l) if l.is_some() => {
                    *l = None;
                    0
                }
                _ => -11,
            }
        })
    }
    unsafe extern "C" fn guard(
        call: Option<unsafe extern "C" fn(*mut u8) -> i32>,
        ctx: *mut u8,
    ) -> i32 {
        let early = with(|f| {
            note(f, "guard");
            f.guard_ret
        });
        if let Some(rc) = early {
            return rc;
        }
        with(|f| f.in_guard = true);
        let rc = match call {
            Some(c) => c(ctx),
            None => -9,
        };
        with(|f| f.in_guard = false);
        rc
    }

    pub static mut TABLE: ShellService = ShellService {
        magic: SHELL_SERVICE_MAGIC,
        version: SHELL_SERVICE_VERSION,
        generation: 1,
        font_source_open: Some(open),
        font_source_read_at: Some(read_at),
        font_source_validate: Some(validate),
        font_source_close: Some(close),
        font_guard_run: Some(guard),
    };

    /// 贋物を空にして `__os32_aux` に表を置く (`version` で古い表も作れる)。
    pub fn install(version: u32) {
        *lk(&FAKE) = Some(Fake::default());
        unsafe {
            TABLE.version = version;
            super::__os32_aux = core::ptr::addr_of!(TABLE) as *const core::ffi::c_void;
        }
    }
}
