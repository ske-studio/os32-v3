//! fixture.rs — T7b-b2 b2③ のゲスト fixture (cargo feature `r1_fixture`、製品 build には入らない)。
//!
//! 設計 ~/os32-tmp/run/t7bb12/DESIGN.final.md §3-3・§5 b2③:
//!   - [`open`]: main の通常文脈 (gui_register の後) で、kernel から entry frame の
//!     4 語目で受けた内部入口の表 (`ffi::shell_service`) の `font_source_open` を呼ぶ。
//!   - [`pump`]: syscall 境界のポンプ (WM 区間、`ring3_wm_depth > 0`) の中で
//!     `font_source_read_at` を 1 回だけ呼び、先頭 4 バイトが SFNT の印かを見る。
//!   - 結果は static (`gshell_fixture_*`、`emu_read_mem` で読む) と、1 行の
//!     `fixture=OK id=0x.. len=..` (失敗は `fixture=FAIL ...`) を con_sink (kprintf)
//!     とシリアルに出す。
//!
//! kernel の段 A は戻り番地で呼び手を確かめるので、表を呼ぶのは戻り値を局所に
//! 受けて使ってから返す薄い `#[inline(never)]` 関数に閉じる (尾呼出しにしない、
//! 設計 §3-2 の b3 向け注記と同じ形)。lease は gshell の寿命の間ずっと開いたまま
//! (b1⑦ の hsync BUSY をこの lease で見る。retire が閉じる)。

use crate::ffi;

const FIXTURE_PATH: &[u8] = b"/data/JF-Dot-Shinonome16.ttf\0";

/// 0 = 未実行、1 = open 済み (read 待ち)、2 = OK、3 = FAIL。
#[no_mangle]
pub static mut gshell_fixture_state: u32 = 0;
/// shell lease の id (font_lease_test probe に渡す)。
#[no_mangle]
pub static mut gshell_fixture_id: u32 = 0;
/// 最後の呼出しの戻り値 (open は 0、read_at は実読込長、負は誤り)。
#[no_mangle]
pub static mut gshell_fixture_rc: i32 = 0;
/// open の時点の表の generation。
#[no_mangle]
pub static mut gshell_fixture_gen: u32 = 0;

#[inline(never)]
fn call_open(t: &ffi::ShellService, id: &mut u32, size: &mut u32) -> i32 {
    let f = match t.font_source_open { Some(f) => f, None => return i32::MIN };
    let r = unsafe { f(FIXTURE_PATH.as_ptr(), id as *mut u32, size as *mut u32) };
    core::hint::black_box(r)
}

#[inline(never)]
fn call_read_at(t: &ffi::ShellService, id: u32, dst: &mut [u8; 4]) -> i32 {
    let f = match t.font_source_read_at { Some(f) => f, None => return i32::MIN };
    let r = unsafe { f(id, 0, dst.as_mut_ptr(), dst.len() as u32) };
    core::hint::black_box(r)
}

fn sfnt_magic(b: &[u8; 4]) -> bool {
    *b == [0, 1, 0, 0] || b == b"true" || b == b"OTTO"
}

/// 書式の要らない 1 行を組む (`fixture=<word> id=0x<hex> rc=<dec>`)。
struct Line { b: [u8; 96], n: usize }
impl Line {
    fn s(&mut self, s: &[u8]) {
        for &c in s {
            if self.n < self.b.len() - 3 { self.b[self.n] = c; self.n += 1; }
        }
    }
    fn hex(&mut self, v: u32) {
        let mut i = 8;
        while i > 0 {
            i -= 1;
            let d = ((v >> (i * 4)) & 15) as u8;
            self.s(&[if d < 10 { b'0' + d } else { b'a' + d - 10 }]);
        }
    }
    fn dec(&mut self, v: i32) {
        let mut u = if v < 0 { self.s(b"-"); (v as i64).unsigned_abs() } else { v as u64 };
        let mut tmp = [0u8; 20];
        let mut k = 0;
        loop {
            tmp[k] = b'0' + (u % 10) as u8;
            k += 1;
            u /= 10;
            if u == 0 { break; }
        }
        while k > 0 { k -= 1; self.s(&[tmp[k]]); }
    }
}

fn report(word: &[u8]) {
    let mut l = Line { b: [0; 96], n: 0 };
    l.s(b"fixture=");
    l.s(word);
    l.s(b" id=0x");
    l.hex(unsafe { gshell_fixture_id });
    l.s(b" gen=");
    l.dec(unsafe { gshell_fixture_gen } as i32);
    l.s(b" rc=");
    l.dec(unsafe { gshell_fixture_rc });
    let n = l.n;
    unsafe {
        let a = os32api::api();
        l.b[n] = 0;
        (a.kprintf)(0x0A, b"%s\n\0".as_ptr(), l.b.as_ptr());
        if (a.serial_is_initialized)() != 0 {
            l.b[n] = b'\r';
            l.b[n + 1] = b'\n';
            l.b[n + 2] = 0;
            (a.serial_puts)(l.b.as_ptr());
        }
    }
}

/// main の通常文脈 (gui_register の後) で 1 回。
pub fn open() {
    let t = match ffi::shell_service() {
        Some(t) => t,
        None => {
            unsafe { gshell_fixture_state = 3; gshell_fixture_rc = i32::MIN; }
            report(b"FAIL(no-table)");
            return;
        }
    };
    let mut id = 0u32;
    let mut size = 0u32;
    let r = call_open(t, &mut id, &mut size);
    unsafe {
        gshell_fixture_gen = t.generation;
        gshell_fixture_rc = r;
        gshell_fixture_id = id;
        gshell_fixture_state = if r == 0 { 1 } else { 3 };
    }
    if r != 0 {
        report(b"FAIL(open)");
    }
}

/// ポンプ (WM 区間) の中で、open 済みなら read_at を 1 回だけ。
pub fn pump() {
    if unsafe { gshell_fixture_state } != 1 {
        return;
    }
    let t = match ffi::shell_service() {
        Some(t) => t,
        None => { unsafe { gshell_fixture_state = 3; } return; }
    };
    let mut head = [0u8; 4];
    let r = call_read_at(t, unsafe { gshell_fixture_id }, &mut head);
    let ok = r == 4 && sfnt_magic(&head);
    unsafe {
        gshell_fixture_rc = r;
        gshell_fixture_state = if ok { 2 } else { 3 };
    }
    report(if ok { b"OK" } else { b"FAIL(read)" });
}
