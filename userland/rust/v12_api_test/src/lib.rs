//! v12_api_test — GUI v1.2 デスクトップ client API の実機確認アプリ (票 C4 §7)
//!
//! gshell (WM) の下で動く**普通のアプリ**。v1.2 で足した 6 本の公開 API
//! (ジャンプ表 95〜100) を 1 枚の窓から全部叩き、結果を**ラベルとして窓の中に
//! 出す** — `/api/screenshot` だけで合否を判定できるようにするため。
//!
//! ```text
//!   キー / ボタン        試すもの                                    表示先
//!   1 / [1 MsgBox]      modal_open(OK) → on_modal → modal_result     msg:
//!   2 / [2 File]        file_open      → on_modal → modal_result     file:
//!   3 / [3 Input]       input_open     → on_modal → modal_result     input:
//!   4 / [4 Launch]      session_launch("/usr/bin/gui_demo.bin")      launch:
//!   5 / [5 FULL]        FULL / STALE の否定試験 (契約 M2)            msg:
//!   6 / [6 Ring]        リング満杯を跨いだ Modal の生存 (契約 V12-P) msg:
//!   7 / [7 CUI]         session_switch_cui() の戻り値 (契約 S6)      launch:
//!   ESC                 終了
//! ```
//!
//! 窓の下端には Icon16 を 2 つ描く。左は **mask 全 1 (不透明)**、右は
//! **菱形 mask (角が透明)**。どちらもデスクトップ色 (`GUI_COLOR_DESKTOP`) の
//! 下地の上に描くので、右のアイコンの角に下地色が透けていれば mask が効いている。
//!
//! `GUI_EV_QUIT` は libos32gui の既定 `on_quit` (= `ui.quit()`) で抜ける
//! (`session_launch` / `session_switch_cui` が受理されると WM が Quit を投げてくる)。
#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

use libos32gui::gapi::proto::{
    GUI_COLOR_DESKTOP, GUI_COLOR_TEXT, GUI_MAX_TIMERS, GUI_MODAL_OK, GUI_OP_POLL,
};
use libos32gui::gapi::types::{Rect, Style};
use libos32gui::icon::{draw_icon16, GuiIcon16};
use libos32gui::widget::{self, WidgetId, SCAN_ESC};
use libos32gui::{modal, session, App, GuiErr, GuiResult, SizeSpec, Ui, Window, WindowSpec};
use os32api::gui::types::SurfaceId;
use os32api::KernelAPI;

/// 起動要求で差し替える相手 (v1.2 の LAUNCH は絶対パスのみ)。
const LAUNCH_PATH: &[u8] = b"/usr/bin/gui_demo.bin";

/// キー 6 でループへ戻らない長さ (tick = 10ms なので 600 tick = 6 秒)。
const RING_PAUSE_TICKS: u32 = 600;
/// そのうち「生の `OP_POLL` でリングを埋める」時間 (2 秒)。残りは無通信で待つ。
const RING_FILL_TICKS: u32 = 200;

/// `modal_result` に渡す「絶対に存在しない」ダイアログ ID (ERR_STALE 期待)。
const BOGUS_DIALOG: u16 = 9999;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let mut app = match Test::build() {
        Ok(a) => a,
        Err(e) => return e.code(),
    };
    let _ = libos32gui::run(&mut app);
    0
}

/* ================================================================ */
/*  アプリの状態                                                     */
/* ================================================================ */

/// `on_modal` をどの試験として解釈するか。
const MODE_PLAIN: u8 = 0;
/// キー 5: FULL / STALE の否定試験 (契約 M2)。
const MODE_FULL: u8 = 5;
/// キー 6: リング満杯を跨いだ Modal の生存 (契約 V12-P)。
const MODE_RING: u8 = 6;

struct Test {
    win: Option<Window>,
    b_msg: WidgetId,
    b_file: WidgetId,
    b_input: WidgetId,
    b_launch: WidgetId,
    b_full: WidgetId,
    b_ring: WidgetId,
    b_cui: WidgetId,
    lbl_modal: WidgetId,
    lbl_file: WidgetId,
    lbl_input: WidgetId,
    lbl_sess: WidgetId,
    lbl_hint: WidgetId,
    /// 発行した DialogId → どの試験か (1=msg/5/6 共用 / 2=file / 3=input)。0 = 未発行。
    pending: [u16; 4],
    /// `on_modal` の解釈 (`MODE_*`)。
    mode: u8,
    /// キー 5 の途中経過 (`5: open2=...`)。`on_modal` で続きを足す。
    seq: [u8; 64],
    seq_len: usize,
    /* --- キー 6 (リング満杯) の記録 --- */
    /// `after_commit` で 6 秒止まる予約。
    ring_pause: bool,
    /// 一度でもキー 6 を走らせたか (`on_overflow` でラベルを描き直す)。
    ring_active: bool,
    /// `on_overflow` を受けた回数。
    ovf_count: u32,
    /// `dropped` の累計。
    dropped_total: u32,
    /// リングに溜まった最大件数 (128 = `GUI_RING_CAPACITY` なら満杯を作れた)。
    ring_max: u16,
    /// `modal_result` の結果 (未着は `i32::MIN`)。
    ring_result: i32,
}

impl Test {
    fn build() -> GuiResult<Test> {
        let info = libos32gui::gapi::screen_info();
        let sw = info.width as i32;
        let sh = info.height as i32;
        let w = clamp(sw * 3 / 5, 300, 380);
        let h = clamp(sh * 7 / 10, 250, 300);

        let win = Window::create(
            &WindowSpec::new(b"v12 api test", Rect::new(24, 20, w as i16, h as i16))
                .min_size(300, 250),
        )?;

        let root = widget::column(8, 5)?;

        /* --- ボタン列 1 (PM の /api/mouse 台本はこの並びを叩く) --- */
        let btns = widget::row(0, 6)?;
        let b_msg = widget::button(b"1 MsgBox")?;
        let b_file = widget::button(b"2 File")?;
        let b_input = widget::button(b"3 Input")?;
        let b_launch = widget::button(b"4 Launch")?;
        widget::add(btns, b_msg, SizeSpec::Flex(1))?;
        widget::add(btns, b_file, SizeSpec::Flex(1))?;
        widget::add(btns, b_input, SizeSpec::Flex(1))?;
        widget::add(btns, b_launch, SizeSpec::Flex(1))?;
        widget::add(root, btns, SizeSpec::Fixed(26))?;

        /* --- ボタン列 2: G2 の否定ケース --- */
        let btns2 = widget::row(0, 6)?;
        let b_full = widget::button(b"5 FULL")?;
        let b_ring = widget::button(b"6 Ring")?;
        let b_cui = widget::button(b"7 CUI")?;
        widget::add(btns2, b_full, SizeSpec::Flex(1))?;
        widget::add(btns2, b_ring, SizeSpec::Flex(1))?;
        widget::add(btns2, b_cui, SizeSpec::Flex(1))?;
        widget::add(root, btns2, SizeSpec::Fixed(26))?;

        /* --- 結果ラベル (screenshot で読む) --- */
        let lbl_modal = widget::label(b"msg: -")?;
        let lbl_file = widget::label(b"file: -")?;
        let lbl_input = widget::label(b"input: -")?;
        let lbl_sess = widget::label(b"launch: -")?;
        widget::add(root, lbl_modal, SizeSpec::Fixed(17))?;
        widget::add(root, lbl_file, SizeSpec::Fixed(17))?;
        widget::add(root, lbl_input, SizeSpec::Fixed(17))?;
        widget::add(root, lbl_sess, SizeSpec::Fixed(17))?;

        let lbl_hint = widget::label(b"ESC: quit  icons: opaque / masked")?;
        widget::add(root, lbl_hint, SizeSpec::Fixed(17))?;

        /* 残りは widget を置かない = 下地のまま。そこへ on_paint で icon16 を描く。 */
        win.set_root(root)?;
        win.set_focus()?;

        Ok(Test {
            win: Some(win),
            b_msg,
            b_file,
            b_input,
            b_launch,
            b_full,
            b_ring,
            b_cui,
            lbl_modal,
            lbl_file,
            lbl_input,
            lbl_sess,
            lbl_hint,
            pending: [0; 4],
            mode: MODE_PLAIN,
            seq: [0; 64],
            seq_len: 0,
            ring_pause: false,
            ring_active: false,
            ovf_count: 0,
            dropped_total: 0,
            ring_max: 0,
            ring_result: i32::MIN,
        })
    }

    fn win_id(&self) -> u32 {
        match self.win.as_ref() {
            Some(w) => w.id(),
            None => 0,
        }
    }

    /* ---- 1〜3: 3 つの open。DialogId を控えて on_modal で照合する ---- */

    fn do_msgbox(&mut self) {
        self.mode = MODE_PLAIN;
        let r = modal::modal_open(self.win_id(), GUI_MODAL_OK, b"v1.2 MODAL_RESULT test");
        self.note_open(r, 1, self.lbl_modal, b"msg");
    }

    fn do_file(&mut self) {
        self.mode = MODE_PLAIN;
        let r = modal::file_open(self.win_id(), b"Open a file");
        self.note_open(r, 2, self.lbl_file, b"file");
    }

    fn do_input(&mut self) {
        self.mode = MODE_PLAIN;
        let r = modal::input_open(self.win_id(), b"Type text (SHIFT+SPACE = FEP)");
        self.note_open(r, 3, self.lbl_input, b"input");
    }

    /// 4: `session_launch` は「受理」しか返さない。受理されれば WM が Quit を投げる。
    fn do_launch(&mut self) {
        let mut buf = [0u8; 64];
        let n = match session::session_launch(LAUNCH_PATH) {
            Ok(()) => fmt2(&mut buf, b"launch: accepted", LAUNCH_PATH),
            Err(e) => fmt2(&mut buf, b"launch", short_err(e)),
        };
        let _ = widget::set_text(self.lbl_sess, &buf[..n]);
        self.repaint();
    }

    /* ---- 5: FULL / STALE の否定試験 (契約 M2) ---- */
    ///
    /// 1 手目でモーダルを開き、**consume せずに**もう 1 回 `modal_open` する。
    /// 続きは `on_modal` の中 (`full_finish`)。
    fn do_full(&mut self) {
        self.seq_len = 0;
        self.seq_len = put(&mut self.seq, 0, b"5: ");
        let id = match modal::modal_open(self.win_id(), GUI_MODAL_OK, b"FULL/STALE test") {
            Ok(id) => id,
            Err(e) => {
                let n = self.seq_len;
                let n = put(&mut self.seq, n, b"open1=");
                let n = put(&mut self.seq, n, short_err(e));
                self.seq_len = n;
                self.mode = MODE_PLAIN;
                self.show_seq();
                return;
            }
        };
        self.pending[1] = id;
        self.mode = MODE_FULL;

        /* 表示中のモーダルがあるので 2 本目は開けない (契約 M2 / v1 は 1 枚)。 */
        let open2 = res_name(modal::modal_open(self.win_id(), GUI_MODAL_OK, b"second"));
        let n = self.seq_len;
        let n = put(&mut self.seq, n, b"open2=");
        let n = put(&mut self.seq, n, open2);
        self.seq_len = n;
        self.show_seq();
    }

    /// キー 5 の続き。`on_modal` が来た時点で走る (まだ consume していない)。
    fn full_finish(&mut self, dialog: u16) {
        let mut val = [0u8; 256];

        /* (a) 未 consume の結果があるので MODAL_OPEN は ERR_FULL (契約 M2)。 */
        let open3 = res_name(modal::modal_open(self.win_id(), GUI_MODAL_OK, b"third"));
        let n = put(&mut self.seq, self.seq_len, b" open3=");
        let n = put(&mut self.seq, n, open3);

        /* (b) 正規の 1 回目: r=1 (OK)、値は空。 */
        let n = put(&mut self.seq, n, b" r=");
        let n = match modal::modal_result(dialog, &mut val) {
            Ok(m) => {
                let k = put_num(&mut self.seq, n, m.result as i32);
                if m.copied != 0 {
                    /* MessageBox は値が空のはず。空でなければ長さを見せる。 */
                    let k = put(&mut self.seq, k, b"+v");
                    put_num(&mut self.seq, k, m.copied as i32)
                } else {
                    k
                }
            }
            Err(e) => put(&mut self.seq, n, short_err(e)),
        };

        /* (c) 二重 consume は ERR_STALE (契約 M2)。 */
        let n = put(&mut self.seq, n, b" again=");
        let n = put(&mut self.seq, n, res_name(modal::modal_result(dialog, &mut val)));

        /* (d) 存在しない ID も ERR_STALE。 */
        let n = put(&mut self.seq, n, b" bad=");
        let n = put(
            &mut self.seq,
            n,
            res_name(modal::modal_result(BOGUS_DIALOG, &mut val)),
        );

        self.seq_len = n;
        self.mode = MODE_PLAIN;
        self.show_seq();
    }

    /* ---- 6: リング満杯を跨いだ Modal の生存 (契約 V12-P / M3) ---- */
    ///
    /// **アプリ自身でリングを満杯にする**。マウスや打鍵に頼ると溢れない —
    /// WM は連続 Pointer を 1 件に畳み (`ring::append`)、モーダル表示中の打鍵は
    /// ダイアログ宛で app のリングには積まないため (2026-09-06 実測 ovf=0)。
    ///
    /// 手順:
    /// 1. 1 tick 反復のタイマを 8 本 (`GUI_MAX_TIMERS`) 張る。
    /// 2. MessageBox を開く。
    /// 3. `after_commit` で約 6 秒、**`ring_head` を進めずに生の `OP_POLL` だけ**
    ///    を回す。WM は `OP_POLL` の中で期限切れタイマを追記する
    ///    (`timer::fire_expired`) ので、リングは 128 件で満杯のまま張り付く。
    /// 4. その間に PM が OK をクリックする。WM は満杯で `GUI_EV_MODAL` を積めず、
    ///    **捨てずに pending として保持**して次の `OP_POLL` で再配送する (契約 M3)。
    /// 5. 再開後に通常の `poll` がリングを空にし、次の周で Modal が届く。
    ///    タイマは `on_modal` で落とす (それまでループを起こし続ける役も兼ねる)。
    fn do_ring(&mut self) {
        self.ring_active = true;
        self.ovf_count = 0;
        self.dropped_total = 0;
        self.ring_max = 0;
        self.ring_result = i32::MIN;
        self.arm_ring_timers();
        match modal::modal_open(self.win_id(), GUI_MODAL_OK, b"ring-full test: click OK") {
            Ok(id) => {
                self.pending[1] = id;
                self.mode = MODE_RING;
                self.ring_pause = true;
            }
            Err(e) => {
                self.kill_ring_timers();
                self.mode = MODE_PLAIN;
                let mut buf = [0u8; 64];
                let n = fmt2(&mut buf, b"6: open", short_err(e));
                let _ = widget::set_text(self.lbl_modal, &buf[..n]);
            }
        }
        self.show_ring();
    }

    /// 1 tick 反復のタイマを上限本数まで張る (リングを埋める種)。
    fn arm_ring_timers(&self) {
        let w = self.win_id();
        let mut id = 0u8;
        while (id as usize) < GUI_MAX_TIMERS {
            let _ = libos32gui::client::timer_set(w, id, 1, true);
            id += 1;
        }
    }

    fn kill_ring_timers(&self) {
        let w = self.win_id();
        let mut id = 0u8;
        while (id as usize) < GUI_MAX_TIMERS {
            let _ = libos32gui::client::timer_kill(w, id);
            id += 1;
        }
    }

    /// `6: r=<n> ring=<max> ovf=<n> dropped=<n>` を組み立てて `msg:` ラベルへ。
    ///
    /// `ring` は溜まった最大件数 (128 = `GUI_RING_CAPACITY` に張り付けば満杯を
    /// 作れている)。`ovf` / `dropped` はカーネルの打鍵待ち行列が溢れた分しか
    /// 増えない (WM は導出イベントを捨てず期限を進めないだけなので、外から
    /// `/api/key` を連打しない限り 0 のままで正しい)。
    fn show_ring(&mut self) {
        let mut buf = [0u8; 64];
        let n = put(&mut buf, 0, b"6: r=");
        let n = if self.ring_result == i32::MIN {
            put(&mut buf, n, b"?")
        } else {
            put_num(&mut buf, n, self.ring_result)
        };
        let n = put(&mut buf, n, b" ring=");
        let n = put_num(&mut buf, n, self.ring_max as i32);
        let n = put(&mut buf, n, b" ovf=");
        let n = put_num(&mut buf, n, self.ovf_count as i32);
        let n = put(&mut buf, n, b" dropped=");
        let n = put_num(&mut buf, n, self.dropped_total as i32);
        let _ = widget::set_text(self.lbl_modal, &buf[..n]);
        self.repaint();
    }

    /// 約 6 秒、リングを満杯にしたまま張り付かせる。2 段構え。
    ///
    /// **第 1 段 (最初の [`RING_FILL_TICKS`])**: `ring_head` を進めずに生の
    /// `OP_POLL` を回す。`client::poll()` は使わない — あれは `ring_head` を
    /// tail まで進める (= 消費してしまう)。ここで欲しいのは「WM は積むが
    /// アプリは消費しない」状態で、WM は `OP_POLL` の中でしか導出イベントを
    /// 積まない (`timer::fire_expired`) ので、こちらから叩く必要がある。
    ///
    /// **第 2 段 (残り)**: `gui_call` を一切やめて `sys_halt` だけで待つ。リングは
    /// 満杯のままなので、X4 のポンプは打鍵をカーネル待ち行列に残す
    /// (`input::capture` の `space_ok`)。ここで `/api/key` を連打されると
    /// カーネル待ち行列が溢れ、`kbd_dropped_count` の差分が `dropped` +
    /// `OVERFLOW` として上がる (契約 T3 の筋書き 3b)。連打が無ければ 0 のまま
    /// で正しい — WM は導出イベントを「捨てる」のではなく期限を進めないだけ
    /// なので、タイマだけでは `dropped` は増えない。
    ///
    /// `OP_WAIT` はハンドラ内から呼んではいけない (契約 U3) ので、待ちは
    /// `sys_halt` (KAPI) で作る。
    fn ring_fill_now(&mut self) {
        let mut max = self.ring_max;
        unsafe {
            let a = os32api::api();
            let t0 = (a.get_tick)();
            let mut guard: u32 = 0;
            loop {
                let elapsed = (a.get_tick)().wrapping_sub(t0);
                if elapsed >= RING_PAUSE_TICKS {
                    break;
                }
                if elapsed < RING_FILL_TICKS {
                    /* 第 1 段: WM に導出イベント (Timer) を積ませる。head は動かさない。 */
                    let _ = libos32gui::client::call(GUI_OP_POLL, 0);
                    let h = libos32gui::client::read_header();
                    let depth = h.ring_tail.wrapping_sub(h.ring_head);
                    if depth > max {
                        max = depth;
                    }
                }
                (a.sys_halt)();
                guard += 1;
                if guard > 200_000 {
                    break; /* tick が進まない環境でも抜ける保険 */
                }
            }
        }
        self.ring_max = max;
    }

    /* ---- 7: セッション API で CUI へ ---- */

    fn do_switch_cui(&mut self) {
        let mut buf = [0u8; 64];
        let n = match session::session_switch_cui() {
            Ok(()) => put(&mut buf, 0, b"7: switch_cui=accepted"),
            Err(e) => {
                let k = put(&mut buf, 0, b"7: switch_cui=");
                put(&mut buf, k, short_err(e))
            }
        };
        let _ = widget::set_text(self.lbl_sess, &buf[..n]);
        self.repaint();
    }

    /* ---- 小物 ---- */

    fn show_seq(&mut self) {
        let n = self.seq_len;
        let mut tmp = [0u8; 64];
        let mut i = 0;
        while i < n && i < tmp.len() {
            tmp[i] = self.seq[i];
            i += 1;
        }
        let _ = widget::set_text(self.lbl_modal, &tmp[..i]);
        self.repaint();
    }

    fn note_open(&mut self, r: GuiResult<u16>, kind: usize, lbl: WidgetId, tag: &[u8]) {
        let mut buf = [0u8; 64];
        let n = match r {
            Ok(id) => {
                self.pending[kind] = id;
                fmt_num(&mut buf, tag, b": dialog #", id as i32)
            }
            Err(e) => fmt2(&mut buf, tag, short_err(e)),
        };
        let _ = widget::set_text(lbl, &buf[..n]);
        self.repaint();
    }

    fn repaint(&self) {
        if let Some(w) = self.win.as_ref() {
            let _ = w.invalidate_all();
        }
    }
}

/* ================================================================ */
/*  ハンドラ                                                         */
/* ================================================================ */

impl App for Test {
    fn on_click(&mut self, _ui: &mut Ui, w: WidgetId) {
        if w == self.b_msg {
            self.do_msgbox();
        } else if w == self.b_file {
            self.do_file();
        } else if w == self.b_input {
            self.do_input();
        } else if w == self.b_launch {
            self.do_launch();
        } else if w == self.b_full {
            self.do_full();
        } else if w == self.b_ring {
            self.do_ring();
        } else if w == self.b_cui {
            self.do_switch_cui();
        }
    }

    /// 数字キーでも同じことをする (`/api/key` 台本用)。ESC で終了。
    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, ch: u8, _mods: u8, down: bool) {
        if !down {
            return;
        }
        if scan == SCAN_ESC {
            ui.quit();
            return;
        }
        match ch {
            b'1' => self.do_msgbox(),
            b'2' => self.do_file(),
            b'3' => self.do_input(),
            b'4' => self.do_launch(),
            b'5' => self.do_full(),
            b'6' => self.do_ring(),
            b'7' => self.do_switch_cui(),
            _ => {}
        }
    }

    /// **ここが票 C4 の要**: 値は `on_modal` の中で `modal_result` から取る。
    /// 非同期なので open が同期で path/text を返すことはない。
    fn on_modal(&mut self, _ui: &mut Ui, dialog: u16, result: i16) {
        let _ = result; /* 値も結果も modal_result 側で受ける (契約 M1) */

        /* キー 5 / 6 は専用の筋書き。 */
        if self.mode == MODE_FULL && dialog == self.pending[1] {
            self.full_finish(dialog);
            return;
        }
        if self.mode == MODE_RING && dialog == self.pending[1] {
            /* リングを埋め続けていたタイマはここで止める (ここまでループを
             * 起こし続ける役でもあった — 満杯で保留された Modal が届くまで
             * OP_WAIT で寝てしまわないように)。 */
            self.kill_ring_timers();
            let mut val = [0u8; 256];
            self.ring_result = match modal::modal_result(dialog, &mut val) {
                Ok(m) => m.result as i32,
                Err(e) => e.code(),
            };
            self.mode = MODE_PLAIN;
            self.show_ring();
            return;
        }

        let mut val = [0u8; 256];
        let r = modal::modal_result(dialog, &mut val);
        let (lbl, tag) = if dialog == self.pending[2] {
            (self.lbl_file, &b"file"[..])
        } else if dialog == self.pending[3] {
            (self.lbl_input, &b"input"[..])
        } else {
            (self.lbl_modal, &b"msg"[..])
        };

        let mut buf = [0u8; 64];
        let n = match r {
            Ok(m) => {
                if m.copied == 0 {
                    /* MessageBox は値が空 (契約 M1)。result だけ見せる。 */
                    fmt_num(&mut buf, tag, b": r=", m.result as i32)
                } else {
                    fmt2(&mut buf, tag, &val[..m.copied])
                }
            }
            Err(e) => fmt2(&mut buf, tag, short_err(e)),
        };
        let _ = widget::set_text(lbl, &buf[..n]);
        self.repaint();
    }

    /// 取りこぼし通知 (契約 T3)。キー 6 の合否はこの回数と `dropped` で見る。
    fn on_overflow(&mut self, _ui: &mut Ui, dropped: u16) {
        self.ovf_count = self.ovf_count.wrapping_add(1);
        self.dropped_total = self.dropped_total.wrapping_add(dropped as u32);
        if self.ring_active {
            self.show_ring();
        }
    }

    /// `commit` の直後。キー 6 の「6 秒リングを埋める」はここで走らせる —
    /// モーダルが画面に出てからでないと PM が OK をクリックできない。
    fn after_commit(&mut self, _ui: &mut Ui) {
        if self.ring_pause {
            self.ring_pause = false;
            self.ring_fill_now();
        }
    }

    fn on_close(&mut self, ui: &mut Ui, _window: u32) {
        self.win = None;
        ui.quit();
    }

    /// ウィジェットが使わなかった下端に Icon16 を 2 つ描く (契約 V12-I の目視確認)。
    ///
    /// 下地はデスクトップ色。左は mask 全 1、右は菱形 mask なので、右の角に
    /// 下地色が残っていれば「mask=0 は描かない」が効いている。
    fn on_paint(&mut self, _ui: &mut Ui, _window: u32, surface: SurfaceId, _rect: Rect) {
        /* ヒント行の下に置く (窓の高さやタイトルバー厚に依存しない)。 */
        let hint = widget::rect(self.lbl_hint);
        if hint.is_empty() {
            return;
        }
        let y = hint.bottom() + 6;
        let (_, ch) = match self.win.as_ref() {
            Some(w) => w.client_size(),
            None => return,
        };
        if y + 24 > ch as i32 {
            return; /* 入らないなら描かない (widget を潰さない) */
        }
        /* 下地: 24x24 のデスクトップ色パッチ 2 枚 */
        let bg = Style::new(GUI_COLOR_TEXT, GUI_COLOR_DESKTOP);
        libos32gui::gapi::fill_rect(surface, Rect::new(10, y as i16, 24, 24), bg);
        libos32gui::gapi::fill_rect(surface, Rect::new(44, y as i16, 24, 24), bg);
        /* 左: 不透明 / 右: 菱形 mask */
        draw_icon16(surface, 14, y + 4, &ICON_OPAQUE);
        draw_icon16(surface, 48, y + 4, &ICON_MASKED);
    }
}

/* ================================================================ */
/*  Icon16 のデータ (契約 V12-I の論理形式をそのまま組む)             */
/* ================================================================ */

/// 枠 = 色 0 (TEXT)、内側 = 色 8 (ALERT)。両アイコン共通の画素。
static ICON_PIXELS: [u8; 128] = boxed_pixels(GUI_COLOR_TEXT, 8);

/// mask 全 1 (不透明)。
static ICON_OPAQUE: GuiIcon16 = GuiIcon16 { pixels: ICON_PIXELS, mask: [0xFF; 32] };
/// 菱形 mask (角が透明)。
static ICON_MASKED: GuiIcon16 = GuiIcon16 { pixels: ICON_PIXELS, mask: diamond_mask() };

/// 16x16 の「枠つき塗り」を 4bpp row-major (even x = 上位ニブル) で組む。
const fn boxed_pixels(border: u8, fill: u8) -> [u8; 128] {
    let mut p = [0u8; 128];
    let mut y = 0usize;
    while y < 16 {
        let mut x = 0usize;
        while x < 16 {
            let c = if y == 0 || y == 15 || x == 0 || x == 15 { border } else { fill };
            let i = y * 8 + (x >> 1);
            if x & 1 == 0 {
                p[i] = (p[i] & 0x0F) | ((c & 0x0F) << 4);
            } else {
                p[i] = (p[i] & 0xF0) | (c & 0x0F);
            }
            x += 1;
        }
        y += 1;
    }
    p
}

/// 菱形の 1bpp mask (bit7 が左端、1 = 不透明)。四隅が 0 になる。
const fn diamond_mask() -> [u8; 32] {
    let mut m = [0u8; 32];
    let mut y = 0usize;
    while y < 16 {
        let mut x = 0usize;
        while x < 16 {
            let dx = if x >= 8 { x - 8 } else { 7 - x };
            let dy = if y >= 8 { y - 8 } else { 7 - y };
            if dx + dy <= 6 {
                m[y * 2 + (x >> 3)] |= 1u8 << (7 - (x & 7));
            }
            x += 1;
        }
        y += 1;
    }
    m
}

/* ================================================================ */
/*  小道具 (alloc も core::fmt も使わない)                            */
/*                                                                  */
/*  すべて `#[inline(never)]`。ラベル 1 本組むたびに展開されると      */
/*  .text が 21.6KB まで膨らんでいた (18.8KB に戻る)。試験アプリの    */
/*  中身は変わらないので、複製を減らして読みやすさを取る。            */
/* ================================================================ */

#[inline(never)]
fn clamp(v: i32, lo: i32, hi: i32) -> i32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

/// `GuiErr::name()` から `ERR_` を落とした短い名前 (`FULL` / `STALE` / `NOSYS` …)。
#[inline(never)]
fn short_err(e: GuiErr) -> &'static [u8] {
    let n = e.name();
    if n.len() > 4 && n[0] == b'E' && n[1] == b'R' && n[2] == b'R' && n[3] == b'_' {
        &n[4..]
    } else {
        n
    }
}

/// 成功なら `ok`、失敗なら短いエラー名 (否定試験の 1 語表示用)。
#[inline(never)]
fn res_name<T>(r: GuiResult<T>) -> &'static [u8] {
    match r {
        Ok(_) => b"ok",
        Err(e) => short_err(e),
    }
}

/// `a` + `": "` + `b` を詰める。返り値: 書いたバイト数。
#[inline(never)]
fn fmt2(dst: &mut [u8], a: &[u8], b: &[u8]) -> usize {
    let mut n = put(dst, 0, a);
    n = put(dst, n, b": ");
    put(dst, n, b)
}

/// `a` + `b` + 10 進数を詰める。
#[inline(never)]
fn fmt_num(dst: &mut [u8], a: &[u8], b: &[u8], v: i32) -> usize {
    let mut n = put(dst, 0, a);
    n = put(dst, n, b);
    put_num(dst, n, v)
}

/// `dst[at..]` へ 10 進数を詰める。返り値: 新しい末尾。
#[inline(never)]
fn put_num(dst: &mut [u8], at: usize, v: i32) -> usize {
    let mut n = at;
    let mut tmp = [0u8; 12];
    let mut k = 0usize;
    let neg = v < 0;
    let mut u = if neg { (-(v as i64)) as u32 } else { v as u32 };
    if u == 0 {
        tmp[k] = b'0';
        k += 1;
    }
    while u > 0 {
        tmp[k] = b'0' + (u % 10) as u8;
        u /= 10;
        k += 1;
    }
    if neg {
        n = put(dst, n, b"-");
    }
    while k > 0 {
        k -= 1;
        if n < dst.len() {
            dst[n] = tmp[k];
            n += 1;
        }
    }
    n
}

/// `dst[at..]` へ `src` を NUL 手前まで詰める。返り値: 新しい末尾。
#[inline(never)]
fn put(dst: &mut [u8], at: usize, src: &[u8]) -> usize {
    let mut n = at;
    let mut i = 0usize;
    while i < src.len() && src[i] != 0 && n < dst.len() {
        dst[n] = src[i];
        n += 1;
        i += 1;
    }
    n
}
