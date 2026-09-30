//! gui_bench — 契約 P2 の測定器。票 C2 作業 10。
//!
//! 測るもの: **入力 → 表示**。WM が入力を取り込んだ tick (`serial` ごとに
//! スロットの予備領域へ記録。契約 P2) から、その入力を処理した周の `commit`
//! 完了までの差を tick で数える。WM とリングの待ち時間を含む。
//! 併せて `gfx_stats` (契約 G7) の差分 = その周の present バイト数を出す。
//!
//! 代表操作 (契約 P2 の表) ごとに分けて数える:
//!
//! | 行 | 操作 | 予算 (NP21/W, present バイト) | 遅延の目標 (実機) |
//! |---|---|---|---|
//! | TYPE   | テキストボックスに 1 文字 | ≤ 2KB | P100 ≤ 2 tick |
//! | SCROLL | リストボックスのスクロール 1 行 | ≤ リスト面積 | P100 ≤ 2 tick |
//! | CLICK  | ボタン押下 | — | — |
//! | MOVE   | ポインタ移動 (ドラッグ追従) | 枠 1 本 ≤ 1KB | 1 tick 以内 |
//!
//! 使い方: gshell 上で起動 → `/api/key` で打鍵を流す → tvram (`GET /api/tvram`)
//! を読む。ESC で終了。
//!
//! **スロット予備領域の読み出しについて (PM への申し送り)**: 取り込み tick 表の
//! 位置 (スロット +11280、64 × 4B = u16 serial + u16 tick 下位) は W1 の
//! `gshell::slot` と C2 の `libos32gui::client` で二重定義になっている。
//! 共有ヘッダ `os32_gui_shared.h` に未記載なので、末尾追記が要る。
#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

use libos32gui::client;
use libos32gui::widget::{
    self, WidgetId, SCAN_DOWN, SCAN_ESC, SCAN_ROLLDOWN, SCAN_ROLLUP, SCAN_UP,
};
use libos32gui::{App, SizeSpec, Ui, Window, WindowSpec};
use os32api::gui::types::{Rect, Stats};
use os32api::KernelAPI;

const B_TYPE: usize = 0;
const B_SCROLL: usize = 1;
const B_CLICK: usize = 2;
const B_MOVE: usize = 3;
const NBUCKET: usize = 4;

static NAMES: [&[u8]; NBUCKET] = [b"TYPE  ", b"SCROLL", b"CLICK ", b"MOVE  "];

/// 1 周で覚えておける計測点 (serial → バケツ)。
const MAX_PENDING: usize = 16;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let mut app = match Bench::build() {
        Ok(a) => a,
        Err(e) => return e.code(),
    };
    let _ = libos32gui::run(&mut app);
    unsafe { (os32api::api().tvram_clear)() };
    0
}

/* ================================================================ */
/*  バケツ                                                           */
/* ================================================================ */

#[derive(Clone, Copy)]
struct Bucket {
    n: u32,
    last: u16,
    min: u16,
    max: u16,
    sum: u32,
    /// present バイト数 (その操作を含む周の合計)。
    bytes: u32,
    bytes_max: u32,
}

impl Bucket {
    const EMPTY: Bucket = Bucket {
        n: 0,
        last: 0,
        min: 0xFFFF,
        max: 0,
        sum: 0,
        bytes: 0,
        bytes_max: 0,
    };

    fn add(&mut self, ticks: u16) {
        self.n += 1;
        self.last = ticks;
        if ticks < self.min {
            self.min = ticks;
        }
        if ticks > self.max {
            self.max = ticks;
        }
        self.sum += ticks as u32;
    }
}

/* ================================================================ */
/*  アプリ                                                           */
/* ================================================================ */

struct Bench {
    win: Option<Window>,
    buckets: [Bucket; NBUCKET],
    pending: [(u16, usize); MAX_PENDING],
    npend: usize,
    prev: Stats,
    dirty_report: bool,
}

impl Bench {
    fn build() -> libos32gui::GuiResult<Bench> {
        let w = Window::create(&WindowSpec::new(b"gui_bench", Rect::new(24, 150, 420, 220)))?;
        let root = widget::column(6, 4)?;
        let hint = widget::label(b"type / arrows / click. ESC quits.")?;
        let tb = widget::textbox(b"")?;
        let lb = widget::listbox()?;
        let mut i = 0;
        while i < 30 {
            let mut name = [0u8; 8];
            name[0] = b'i';
            name[1] = b't';
            name[2] = b'e';
            name[3] = b'm';
            name[4] = b' ';
            name[5] = b'0' + ((i / 10) as u8);
            name[6] = b'0' + ((i % 10) as u8);
            widget::list_add(lb, &name[..7])?;
            i += 1;
        }
        widget::add(root, hint, SizeSpec::Fixed(18))?;
        widget::add(root, tb, SizeSpec::Fixed(22))?;
        widget::add(root, lb, SizeSpec::Flex(1))?;
        w.set_root(root)?;
        let _ = w.set_focus();
        let _ = widget::set_focus(tb);

        let prev = client::stats().unwrap_or(Stats::ZERO);
        Ok(Bench {
            win: Some(w),
            buckets: [Bucket::EMPTY; NBUCKET],
            pending: [(0, 0); MAX_PENDING],
            npend: 0,
            prev,
            dirty_report: true,
        })
    }

    fn mark(&mut self, serial: u16, bucket: usize) {
        if serial == 0 || self.npend >= MAX_PENDING {
            return;
        }
        self.pending[self.npend] = (serial, bucket);
        self.npend += 1;
    }
}

impl App for Bench {
    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, _ch: u8, _mods: u8, down: bool) {
        if down && scan == SCAN_ESC {
            ui.quit();
        }
    }

    fn on_close(&mut self, ui: &mut Ui, _window: u32) {
        self.win = None;
        ui.quit();
    }

    fn on_select(&mut self, _ui: &mut Ui, _w: WidgetId, _index: i32) {}

    /// **計測点の記録**: 生イベントの `serial` をバケツに割り当てる。
    /// `App` の既定ハンドラでは `serial` が見えないので、`on_raw` で受ける。
    fn on_raw(&mut self, _ui: &mut Ui, ev: &os32api::gui::proto::GuiEvent) {
        use os32api::gui::proto::{GUI_EV_BUTTON, GUI_EV_KEY, GUI_EV_POINTER, GUI_EV_TEXT};
        if ev.serial == 0 {
            return;
        }
        match ev.kind {
            GUI_EV_TEXT => self.mark(ev.serial, B_TYPE),
            GUI_EV_KEY => {
                if ev.sub != 0 {
                    let k = ev.key();
                    let scroll = k.scan == SCAN_UP
                        || k.scan == SCAN_DOWN
                        || k.scan == SCAN_ROLLUP
                        || k.scan == SCAN_ROLLDOWN;
                    /* 印字可能キーは Text 側で数えるので、ここでは編集/移動だけ。 */
                    if scroll {
                        self.mark(ev.serial, B_SCROLL);
                    }
                }
            }
            GUI_EV_BUTTON => self.mark(ev.serial, B_CLICK),
            GUI_EV_POINTER => self.mark(ev.serial, B_MOVE),
            _ => {}
        }
    }

    /// `commit` 直後。取り込み tick との差と present バイト数を締める (契約 P2)。
    fn after_commit(&mut self, _ui: &mut Ui) {
        let now = unsafe { (os32api::api().get_tick)() } as u16;
        let cur = client::stats().unwrap_or(Stats::ZERO);
        let bytes = cur.present_bytes.wrapping_sub(self.prev.present_bytes);
        self.prev = cur;

        let n = self.npend;
        self.npend = 0;
        if n == 0 {
            return;
        }
        let mut i = 0;
        while i < n {
            let (serial, b) = self.pending[i];
            if let Some(t0) = client::trace_tick(serial) {
                let d = now.wrapping_sub(t0);
                /* 10 分以上ずれた値は記録の使い回し。捨てる。 */
                if d < 6000 {
                    self.buckets[b].add(d);
                    self.buckets[b].bytes = bytes;
                    if bytes > self.buckets[b].bytes_max {
                        self.buckets[b].bytes_max = bytes;
                    }
                }
            }
            i += 1;
        }
        self.dirty_report = true;
        self.report();
    }
}

impl Bench {
    /// tvram へ表を出す (契約 P2: NP21/W では `GET /api/tvram` で読む)。
    fn report(&mut self) {
        if !self.dirty_report {
            return;
        }
        self.dirty_report = false;
        let mut line = [0u8; 78];

        let n = fill(&mut line, b"gui_bench  op      n   last  min  max  avg  bytes  maxB");
        put_line(0, &line[..n]);

        let mut b = 0usize;
        while b < NBUCKET {
            let k = &self.buckets[b];
            let mut p = 0usize;
            p += fill_at(&mut line, p, b"           ");
            p += fill_at(&mut line, p, NAMES[b]);
            p += num_at(&mut line, p, k.n, 5);
            p += num_at(&mut line, p, k.last as u32, 6);
            p += num_at(&mut line, p, if k.n == 0 { 0 } else { k.min as u32 }, 5);
            p += num_at(&mut line, p, k.max as u32, 5);
            p += num_at(&mut line, p, if k.n == 0 { 0 } else { k.sum / k.n }, 5);
            p += num_at(&mut line, p, k.bytes, 7);
            p += num_at(&mut line, p, k.bytes_max, 7);
            put_line(1 + b, &line[..p]);
            b += 1;
        }
    }
}

/* ================================================================ */
/*  tvram への行出力                                                 */
/* ================================================================ */

fn put_line(row: usize, text: &[u8]) {
    unsafe {
        let a = os32api::api();
        let mut x = 0usize;
        while x < 80 {
            let ch = if x < text.len() { text[x] } else { b' ' };
            (a.tvram_putchar_at)(x as i32, row as i32, ch, os32api::ATTR_WHITE);
            x += 1;
        }
    }
}

fn fill(dst: &mut [u8], src: &[u8]) -> usize {
    fill_at(dst, 0, src)
}

fn fill_at(dst: &mut [u8], at: usize, src: &[u8]) -> usize {
    let mut n = 0usize;
    while n < src.len() && at + n < dst.len() {
        dst[at + n] = src[n];
        n += 1;
    }
    n
}

/// 右詰めの 10 進数を `width` 桁で置く。返り値: 書いた桁数 (= width)。
fn num_at(dst: &mut [u8], at: usize, mut v: u32, width: usize) -> usize {
    let mut tmp = [b' '; 12];
    let mut k = 0usize;
    if v == 0 {
        tmp[0] = b'0';
        k = 1;
    }
    while v > 0 && k < tmp.len() {
        tmp[k] = b'0' + (v % 10) as u8;
        v /= 10;
        k += 1;
    }
    let mut i = 0usize;
    while i < width {
        let ch = if i + k >= width { tmp[width - 1 - i] } else { b' ' };
        if at + i < dst.len() {
            dst[at + i] = ch;
        }
        i += 1;
    }
    width
}
