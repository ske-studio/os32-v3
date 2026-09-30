//! edit_gui — テキストエディタの GUI 版 (票 `docs/archive/gui_v14/TASK_EDIT_GUI.md`)。
//!
//! **アプリを 1 本増やすのが目的ではない。** `libos32gui` / 設定レジストリ /
//! Host Services を**通しで使う受入試験**で、API の穴を露見させるのが仕事 (票 §0)。
//! だから機能は意図して小さい — 取り消しも検索も置換も無い (票 §6)。
//!
//! ```text
//! ┌ Edit ───────────────────────────────────────────────┐
//! │ [Open] [Save] [Copy] [Paste] [Print] [Quit]         │  ボタン列 (メニューの代わり)
//! │ ┌─────────────────────────────────────────────────┐ │
//! │ │ WK_TEXTAREA — **見えている行だけ**が入っている    │ │
//! │ └─────────────────────────────────────────────────┘ │
//! │ /usr/share/x.txt  12 lines  *                        │  状態行
//! └──────────────────────────────────────────────────────┘
//! ```
//!
//! # 作りの要点
//!
//! - **本文は [`doc::Doc`] が持つ** (アプリ側、`static`)。部品 (`WK_TEXTAREA`) は
//!   持たない。毎周、折り返して「見えている行」だけを写す (票 §2)。
//!   1 画面に入らないファイルでも部品の使用量は一定。
//! - **折り返しの桁は部品から聞く** (`textarea_columns`)。決め打ちしない ([C4])。
//!   桁の数え方と切れ目は `libos32gui::textcore` (共通の下請け) を通す —
//!   日本語は 3 バイト 2 桁で、自前で数えると必ずずれる (§4-27)。
//! - **入れ子ループ禁止**。確認・入力は非同期モーダル + [`PEND_NONE`] の状態機械。
//! - **フォーカスは持たずに聞く** (`widget::focused_in`、ジャンプ表 118)。
//!   自分で追っていたころは穴 H13 を踏んだ (票 §9)。
//! - 打鍵は `on_key` (編集キー) と `on_text_changed` (確定文字列) の 2 経路。
//!   後者は `WK_TEXTAREA` が溜めたものを `textarea_take_input` で引き取る。
//!
//! # 通しで使うもの (票 §4 — 3 つとも通す)
//!
//! | 使うもの | どこ |
//! |---|---|
//! | libos32gui | 窓・`WK_TEXTAREA`・ボタン・モーダル |
//! | 設定レジストリ (`cfgro`) | 起動時に `app:edit` / `last_path` を読み、開いた / 保存したら書く |
//! | Host Services (`hostsvc`) | Copy/Paste = クリップボード、Print = 印刷 |
//! | ファイル | `sys_open` / `read` / `write` / `close` |

#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

pub mod doc;

use doc::{Doc, VRow};
use libos32gui::gapi::proto::{
    GUI_MODAL_OK, GUI_MODAL_RESULT_OK, GUI_MODAL_YES_NO,
};
use libos32gui::gapi::types::Rect;
use libos32gui::widget::{
    self, WidgetId, SCAN_BS, SCAN_DEL, SCAN_DOWN, SCAN_ESC, SCAN_HOME, SCAN_LEFT, SCAN_RETURN,
    SCAN_RIGHT, SCAN_ROLLDOWN, SCAN_ROLLUP, SCAN_UP,
};
use libos32gui::{cfg, host, modal, App, SizeSpec, Ui, Window, WindowSpec};
use os32api::KernelAPI;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let mut app = match Editor::build() {
        Ok(a) => a,
        Err(e) => return e.code(),
    };
    let _ = libos32gui::run(&mut app);
    0
}

/* ================================================================ */
/*  寸法 ([C4]: 数値をコードへ散らさない)                             */
/* ================================================================ */

const WIN_X: i16 = 16;
const WIN_Y: i16 = 16;
const WIN_W: i16 = 576;
const WIN_H: i16 = 344;

const ROOT_PAD: i16 = 4;
const ROOT_GAP: i16 = 4;
/// ボタン列の高さ。
const BAR_H: i16 = 20;
/// ボタン 1 個の幅。
const BTN_W: i16 = 60;
/// 状態行の高さ。
const STATUS_H: i16 = 18;

/// パスを持てる長さ (NUL 込み)。
const PATH_CAP: usize = 128;
/// 読み込みの一時バッファ。`Doc` に入る最大量より少し大きく取る。
const LOAD_CAP: usize = doc::MAX_LINES * 96;
/// クリップボードの 1 回の上限 (`host::clip_put` の契約と同じ)。
const CLIP_CAP: usize = 1024;
/// 状態行の文字数。
const STATUS_CAP: usize = 96;

/// 設定レジストリの scope / key (書けるのは `app:` scope だけ)。
const CFG_SCOPE: &[u8] = b"app:edit";
const CFG_LAST_PATH: &[u8] = b"last_path";

/* --- 保留中の操作 (モーダルの結果をどう読むか) --- */
const PEND_NONE: u8 = 0;
const PEND_OPEN: u8 = 1;
const PEND_SAVE_AS: u8 = 2;
const PEND_CLOSE_CONFIRM: u8 = 3;
/// 見せるだけ (結果は捨てる)。
const PEND_MESSAGE: u8 = 4;

/* ================================================================ */
/*  本文は static (40KB 近い。アプリの構造体を stack に積まない)      */
/* ================================================================ */

static mut DOC: Doc = Doc::NEW;
static mut LOAD_BUF: [u8; LOAD_CAP] = [0; LOAD_CAP];

#[allow(static_mut_refs)]
fn doc() -> &'static mut Doc {
    unsafe { &mut DOC }
}

#[allow(static_mut_refs)]
fn load_buf() -> &'static mut [u8; LOAD_CAP] {
    unsafe { &mut LOAD_BUF }
}

/* ================================================================ */
/*  アプリ                                                          */
/* ================================================================ */

struct Editor {
    win: Option<Window>,
    area: WidgetId,
    status: WidgetId,
    b_open: WidgetId,
    b_save: WidgetId,
    b_copy: WidgetId,
    b_paste: WidgetId,
    b_print: WidgetId,
    b_quit: WidgetId,

    /// 見えている範囲の先頭 (**折り返した後**の通し行番号)。
    top: usize,
    /// 開いているファイル (NUL 終端)。空 = 名前なし。
    path: [u8; PATH_CAP],
    path_len: usize,

    pending: u8,
    dialog: u16,
    /// 閉じる確認で Yes だったら本当に終わる。
    closing: bool,

    status_buf: [u8; STATUS_CAP],
    status_len: usize,
}

impl Editor {
    fn build() -> libos32gui::GuiResult<Editor> {
        let spec = WindowSpec::new(b"Edit", Rect::new(WIN_X, WIN_Y, WIN_W, WIN_H));
        let win = Window::create(&spec)?;

        let root = widget::column(ROOT_PAD, ROOT_GAP)?;
        let bar = widget::row(0, ROOT_GAP)?;
        widget::add(root, bar, SizeSpec::Fixed(BAR_H))?;

        let b_open = widget::button(b"Open")?;
        let b_save = widget::button(b"Save")?;
        let b_copy = widget::button(b"Copy")?;
        let b_paste = widget::button(b"Paste")?;
        let b_print = widget::button(b"Print")?;
        let b_quit = widget::button(b"Quit")?;
        widget::add(bar, b_open, SizeSpec::Fixed(BTN_W))?;
        widget::add(bar, b_save, SizeSpec::Fixed(BTN_W))?;
        widget::add(bar, b_copy, SizeSpec::Fixed(BTN_W))?;
        widget::add(bar, b_paste, SizeSpec::Fixed(BTN_W))?;
        widget::add(bar, b_print, SizeSpec::Fixed(BTN_W))?;
        widget::add(bar, b_quit, SizeSpec::Fixed(BTN_W))?;

        let area = widget::textarea()?;
        widget::add(root, area, SizeSpec::Flex(1))?;

        let status = widget::label(b"")?;
        widget::add(root, status, SizeSpec::Fixed(STATUS_H))?;

        win.set_root(root)?;
        win.set_focus()?;

        let mut e = Editor {
            win: Some(win),
            area,
            status,
            b_open,
            b_save,
            b_copy,
            b_paste,
            b_print,
            b_quit,
            top: 0,
            path: [0; PATH_CAP],
            path_len: 0,
            pending: PEND_NONE,
            dialog: 0,
            closing: false,
            status_buf: [0; STATUS_CAP],
            status_len: 0,
        };
        /* `Doc::NEW` は全ビット 0 (.bss に置くため)。使う前に整える。 */
        doc().reset();
        /* 打鍵がいきなり本文へ入るように、編集面へフォーカスを置く。 */
        let _ = widget::set_focus(area);

        /* 設定レジストリ: 直前に開いた場所 (票 §4)。無ければ空のまま始める。 */
        let mut last = [0u8; PATH_CAP];
        if let Ok(n) = cfg::get_text(CFG_SCOPE, CFG_LAST_PATH, &mut last) {
            if n > 0 && n < PATH_CAP {
                e.open_path(&last[..n]);
            }
        }
        e.refresh();
        Ok(e)
    }

    fn win_id(&self) -> u32 {
        match self.win.as_ref() {
            Some(w) => w.id(),
            None => 0,
        }
    }

    fn repaint_all(&self) {
        if let Some(w) = self.win.as_ref() {
            let _ = w.invalidate_all();
        }
    }

    /* ------------------------------------------------------------ */
    /*  部品へ「見えている行」を渡す (票 §2 の中心)                   */
    /* ------------------------------------------------------------ */

    /// 折り返しの桁と入る行数は**部品から聞く** ([C4])。
    fn view_geom(&self) -> (usize, usize) {
        let cols = widget::textarea_columns(self.area);
        let rows = widget::textarea_visible_rows(self.area);
        let cols = if cols <= 0 { 1 } else { cols as usize };
        let rows = if rows <= 0 { 1 } else { rows as usize };
        let rows = if rows > doc::MAX_VIEW_ROWS { doc::MAX_VIEW_ROWS } else { rows };
        (cols, rows)
    }

    /// 本文を折り返して、見えている行だけを部品へ写す。
    fn refresh(&mut self) {
        let (cols, height) = self.view_geom();
        let d = doc();
        self.top = d.scroll_to_caret(cols, height, self.top);

        let mut rows = [VRow::EMPTY; doc::MAX_VIEW_ROWS];
        let n = d.fill_view(cols, self.top, &mut rows[..height]);

        let _ = widget::textarea_clear(self.area);
        let mut i = 0usize;
        while i < n {
            let r = rows[i];
            let line = d.line(r.line);
            /* 切れ目は textcore が決めた UTF-8 境界。ここで数え直さない。 */
            if widget::textarea_add_row(self.area, &line[r.start..r.end]).is_err() {
                break;
            }
            i += 1;
        }

        /* キャレット: 見えている範囲の何本目か + その行の先頭からのバイト数。 */
        let cv = d.caret_vrow(cols);
        if cv >= self.top && cv < self.top + n {
            let k = cv - self.top;
            let r = rows[k];
            let col = if d.cx >= r.start { d.cx - r.start } else { 0 };
            let _ = widget::textarea_set_caret(self.area, k as i32, col as i32);
        } else {
            let _ = widget::textarea_set_caret(self.area, -1, 0);
        }
        self.update_status();
        /* **窓全面を毎打鍵で塗り直さない。** `textarea_*` と `set_text` が
         * 自分の矩形だけを invalidate するので、それで足りる (契約 G4)。
         * 全面にすると 1 打鍵の描画が桁違いに重くなる (filer の v1.2 実測で
         * ペイン全面 = 1.3 秒)。 */
    }

    /* ------------------------------------------------------------ */
    /*  状態行                                                       */
    /* ------------------------------------------------------------ */

    fn set_status(&mut self, a: &[u8], b: &[u8]) {
        let mut n = 0usize;
        let mut i = 0usize;
        while i < a.len() && n + 1 < STATUS_CAP {
            self.status_buf[n] = a[i];
            n += 1;
            i += 1;
        }
        i = 0;
        while i < b.len() && b[i] != 0 && n + 1 < STATUS_CAP {
            self.status_buf[n] = b[i];
            n += 1;
            i += 1;
        }
        self.status_len = n;
        let buf = self.status_buf;
        let _ = widget::set_text(self.status, &buf[..n]);
    }

    fn update_status(&mut self) {
        let dirty = doc().dirty;
        let trunc = doc().truncated;
        let mut tail = [0u8; 24];
        let mut n = 0usize;
        if dirty {
            tail[n] = b' ';
            tail[n + 1] = b'*';
            n += 2;
        }
        if trunc != 0 {
            let m = b" (truncated)";
            let mut i = 0usize;
            while i < m.len() && n < tail.len() {
                tail[n] = m[i];
                n += 1;
                i += 1;
            }
        }
        let path = self.path;
        let plen = self.path_len;
        let name: &[u8] = if plen == 0 { b"(no name)" } else { &path[..plen] };
        let mut line = [0u8; STATUS_CAP];
        let mut k = 0usize;
        let mut i = 0usize;
        while i < name.len() && k + 1 < STATUS_CAP {
            line[k] = name[i];
            k += 1;
            i += 1;
        }
        i = 0;
        while i < n && k + 1 < STATUS_CAP {
            line[k] = tail[i];
            k += 1;
            i += 1;
        }
        self.set_status(&line[..k], b"");
    }

    /* ------------------------------------------------------------ */
    /*  モーダル (入れ子ループを作らない)                             */
    /* ------------------------------------------------------------ */

    fn message(&mut self, msg: &[u8]) {
        if let Ok(id) = modal::modal_open(self.win_id(), GUI_MODAL_OK, msg) {
            self.pending = PEND_MESSAGE;
            self.dialog = id;
        }
    }

    /* ------------------------------------------------------------ */
    /*  ファイル                                                     */
    /* ------------------------------------------------------------ */

    fn set_path(&mut self, p: &[u8]) {
        let mut n = 0usize;
        while n < p.len() && p[n] != 0 && n + 1 < PATH_CAP {
            self.path[n] = p[n];
            n += 1;
        }
        self.path[n] = 0;
        self.path_len = n;
    }

    fn open_path(&mut self, p: &[u8]) {
        self.set_path(p);
        let path = self.path;
        let r = doc::load_file(doc(), &path, load_buf());
        if r < 0 {
            self.path_len = 0;
            self.path[0] = 0;
            doc().reset();
            self.top = 0;
            self.set_status(b"open failed: ", p);
            return;
        }
        self.top = 0;
        if doc().truncated != 0 {
            /* [V4]: 捨てたことを黙らない。 */
            self.message(b"File too large. Only part of it is loaded; do not save over it.");
        }
        /* 次に開くときのために覚える (設定レジストリ、票 §4)。 */
        let path = self.path;
        let plen = self.path_len;
        let _ = cfg::set_text(CFG_SCOPE, CFG_LAST_PATH, &path[..plen]);
    }

    /// 保存する。**書けなかったら理由を出す** (受入 E7 / [V4])。
    fn save_now(&mut self) {
        if self.path_len == 0 {
            self.ask_save_as();
            return;
        }
        let path = self.path;
        let r = doc::save_file(doc(), &path);
        if r < 0 {
            let mut num = [0u8; 12];
            let n = fmt_i32(r, &mut num);
            self.set_status(b"save failed, error ", &num[..n]);
            self.message(b"Could not save. See the status line for the error number.");
            return;
        }
        doc().dirty = false;
        let mut num = [0u8; 12];
        let n = fmt_i32(r, &mut num);
        self.set_status(b"saved, bytes ", &num[..n]);
        let plen = self.path_len;
        let _ = cfg::set_text(CFG_SCOPE, CFG_LAST_PATH, &path[..plen]);
        if self.closing {
            self.closing = false;
            self.quit_now();
        }
    }

    fn ask_save_as(&mut self) {
        if let Ok(id) = modal::input_open(self.win_id(), b"Save as (full path)") {
            self.pending = PEND_SAVE_AS;
            self.dialog = id;
        }
    }

    fn quit_now(&mut self) {
        self.win = None; /* Drop で WM へ DESTROY。ループは窓 0 枚で戻る */
    }

    /// 閉じる要求。**保存していなければ確かめる** (受入 E6)。
    fn request_close(&mut self, ui: &mut Ui) {
        if !doc().dirty {
            ui.quit();
            return;
        }
        if let Ok(id) = modal::modal_open(
            self.win_id(),
            GUI_MODAL_YES_NO,
            b"The text has changed. Save it before closing?",
        ) {
            self.pending = PEND_CLOSE_CONFIRM;
            self.dialog = id;
        }
    }

    /* ------------------------------------------------------------ */
    /*  Host Services (票 §4: 印刷とクリップボード)                   */
    /* ------------------------------------------------------------ */

    /// キャレットのある**論理行**をホストのクリップボードへ (受入 E4)。
    fn clip_copy(&mut self) {
        let d = doc();
        let line = d.line(d.cy);
        if line.is_empty() {
            self.set_status(b"copy: empty line", b"");
            return;
        }
        let n = if line.len() > CLIP_CAP { CLIP_CAP } else { line.len() };
        let mut buf = [0u8; CLIP_CAP];
        buf[..n].copy_from_slice(&line[..n]);
        let r = host::clip_put(&buf[..n], None);
        if r < 0 {
            let mut num = [0u8; 12];
            let k = fmt_i32(r, &mut num);
            self.set_status(b"copy failed, error ", &num[..k]);
        } else {
            self.set_status(b"copied to host clipboard", b"");
        }
    }

    /// ホストのクリップボードをキャレットへ貼る (受入 E4)。
    fn clip_paste(&mut self) {
        let mut buf = [0u8; CLIP_CAP];
        match host::clip_get(&mut buf) {
            Ok((written, _total)) => {
                if written == 0 {
                    self.set_status(b"paste: clipboard is empty", b"");
                    return;
                }
                if !doc().insert(&buf[..written]) {
                    self.set_status(b"paste: the line is full", b"");
                    return;
                }
                self.refresh();
                self.set_status(b"pasted", b"");
            }
            Err(e) => {
                let mut num = [0u8; 12];
                let k = fmt_i32(e.code(), &mut num);
                self.set_status(b"paste failed, error ", &num[..k]);
            }
        }
    }

    /// いま開いているファイルを印刷する (受入 E5)。
    ///
    /// 保存前の本文はホストに無いので、**保存していない本文は印刷しない**。
    /// 「印刷したつもりで古いものが出る」ほうが害が大きい ([V4])。
    fn print_now(&mut self) {
        if self.path_len == 0 {
            self.set_status(b"print: save the file first", b"");
            return;
        }
        if doc().dirty {
            self.set_status(b"print: unsaved changes, save first", b"");
            return;
        }
        let path = self.path;
        let plen = self.path_len;
        let mut pages: u32 = 0;
        let mut svc: u32 = 0;
        let r = host::print_file(
            &path[..plen],
            &path[..plen],
            Some(&mut pages),
            Some(&mut svc),
        );
        if r < 0 {
            let mut num = [0u8; 12];
            let k = fmt_i32(r, &mut num);
            self.set_status(b"print failed, error ", &num[..k]);
            return;
        }
        if svc != 0 {
            let mut num = [0u8; 12];
            let k = fmt_i32(svc as i32, &mut num);
            self.set_status(b"print rejected by host, status ", &num[..k]);
            return;
        }
        let mut num = [0u8; 12];
        let k = fmt_i32(pages as i32, &mut num);
        self.set_status(b"printed, pages ", &num[..k]);
    }

    /* ------------------------------------------------------------ */
    /*  打鍵                                                         */
    /* ------------------------------------------------------------ */

    fn edit_key(&mut self, scan: u8) -> bool {
        let (_cols, height) = self.view_geom();
        let d = doc();
        match scan {
            SCAN_BS => {
                d.backspace();
            }
            SCAN_DEL => {
                d.delete();
            }
            SCAN_RETURN => {
                d.newline();
            }
            SCAN_LEFT => d.move_left(),
            SCAN_RIGHT => d.move_right(),
            SCAN_UP => d.move_up(),
            SCAN_DOWN => d.move_down(),
            SCAN_HOME => d.move_home(),
            SCAN_ROLLUP => {
                let mut k = 0usize;
                while k < height {
                    d.move_up();
                    k += 1;
                }
            }
            SCAN_ROLLDOWN => {
                let mut k = 0usize;
                while k < height {
                    d.move_down();
                    k += 1;
                }
            }
            _ => return false,
        }
        true
    }
}

/* ================================================================ */
/*  小さな整形 (libc は無い)                                          */
/* ================================================================ */

fn fmt_i32(v: i32, out: &mut [u8; 12]) -> usize {
    let mut tmp = [0u8; 12];
    let neg = v < 0;
    let mut n = if neg { (-(v as i64)) as u64 } else { v as u64 };
    let mut k = 0usize;
    if n == 0 {
        tmp[0] = b'0';
        k = 1;
    }
    while n > 0 && k < tmp.len() {
        tmp[k] = b'0' + (n % 10) as u8;
        n /= 10;
        k += 1;
    }
    let mut w = 0usize;
    if neg {
        out[0] = b'-';
        w = 1;
    }
    let mut i = k;
    while i > 0 {
        i -= 1;
        out[w] = tmp[i];
        w += 1;
    }
    w
}

/* ================================================================ */
/*  ハンドラ                                                         */
/* ================================================================ */

impl App for Editor {
    fn on_click(&mut self, ui: &mut Ui, w: WidgetId) {
        if w == self.b_open {
            if let Ok(id) = modal::file_open(self.win_id(), b"Open file") {
                self.pending = PEND_OPEN;
                self.dialog = id;
            }
        } else if w == self.b_save {
            self.save_now();
        } else if w == self.b_copy {
            self.clip_copy();
        } else if w == self.b_paste {
            self.clip_paste();
        } else if w == self.b_print {
            self.print_now();
        } else if w == self.b_quit {
            self.request_close(ui);
            return;
        }
        /* ボタンを押すとフォーカスがそこへ移り、以後の RETURN / SPACE が
         * そのボタンを再び押してしまう。編集面へ戻す。 */
        let _ = widget::set_focus(self.area);
        self.repaint_all();
    }

    /// 確定文字列 (印字可能 ASCII も FEP の日本語も) はここへ来る。
    ///
    /// `WK_TEXTAREA` は**本文を持たない**ので、溜まった分を引き取って
    /// 自分の本文へ入れる (票 §2)。
    fn on_text_changed(&mut self, _ui: &mut Ui, w: WidgetId) {
        if w != self.area {
            return;
        }
        let mut buf = [0u8; libos32gui::gapi::proto::GUI_TEXTAREA_INPUT_CAP];
        let n = widget::textarea_take_input(self.area, &mut buf);
        if n == 0 {
            return;
        }
        if !doc().insert(&buf[..n]) {
            self.set_status(b"the line is full", b"");
            return;
        }
        self.refresh();
    }

    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, _ch: u8, _mods: u8, down: bool) {
        if !down {
            return;
        }
        if self.pending != PEND_NONE {
            return; /* モーダルを開いている間は本文を触らない */
        }
        if scan == SCAN_ESC {
            self.request_close(ui);
            return;
        }
        /* **フォーカスは持たずに聞く** (ジャンプ表 118)。自分で追っていたころは、
         * ボタンを押した後に編集面へ戻したことを知る口が無く、**文字は入るのに
         * カーソルキーだけが死んだ** (穴 H13)。持たなければずれない。 */
        if widget::focused_in(self.win_id()) == self.area && self.edit_key(scan) {
            self.refresh();
        }
    }

    fn on_close(&mut self, ui: &mut Ui, _window: u32) {
        self.request_close(ui);
    }

    fn on_modal(&mut self, ui: &mut Ui, dialog: u16, _result: i16) {
        if dialog != self.dialog {
            return;
        }
        let pending = self.pending;
        self.pending = PEND_NONE;
        self.dialog = 0;

        let mut buf = [0u8; modal::MODAL_VALUE_MAX + 1];
        let r = match modal::modal_result(dialog, &mut buf) {
            Ok(r) => r,
            Err(_) => return,
        };
        match pending {
            PEND_OPEN => {
                if r.result == GUI_MODAL_RESULT_OK && r.copied > 0 {
                    self.open_path(&buf[..r.copied]);
                    self.refresh();
                }
            }
            PEND_SAVE_AS => {
                if r.result == GUI_MODAL_RESULT_OK && r.copied > 0 {
                    self.set_path(&buf[..r.copied]);
                    self.save_now();
                } else if self.closing {
                    self.closing = false;
                }
            }
            PEND_CLOSE_CONFIRM => {
                if r.result == GUI_MODAL_RESULT_OK {
                    /* Yes: 保存してから閉じる */
                    self.closing = true;
                    self.save_now();
                } else {
                    /* No: 捨てて閉じる。**黙って捨てたのではなく聞いた上で。** */
                    ui.quit();
                }
            }
            _ => {}
        }
        self.repaint_all();
    }

    fn on_configure(&mut self, _ui: &mut Ui, _window: u32) {
        /* 窓の大きさが変わると入る桁も行も変わる。折り返し直す。 */
        self.refresh();
    }

    fn on_quit(&mut self, ui: &mut Ui, _reason: u8) {
        ui.quit();
    }
}
