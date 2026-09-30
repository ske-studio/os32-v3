//! filer — GUI シェル v1.2 のファイルマネージャ (票 C5、契約 V12-F)
//!
//! gshell (WM) の下で動く**普通のアプリ**。Win3.1 風の 2 ペインで、GUI だけで
//! 基本ファイル操作とアプリ起動を完結させる。KAPI は v42 のまま
//! (`sys_ls/stat/mkdir/rename/unlink/rmdir/open/read/write/close` だけ)。
//!
//! ```text
//! ┌ File Manager ───────────────────────────────────────┐
//! │ [-] /            │ Name        Size    Type         │  左 = ツリー (listbox)
//! │   [+] usr        │ ..          -       DIR          │  右 = 自前描画 (アイコン)
//! │   [+] etc        │ gui_demo.bin 18944  BIN          │
//! ├──────────────────┴──────────────────────────────────┤
//! │ /usr/bin                                            │  下 = 現在地 + 状態
//! └─────────────────────────────────────────────────────┘
//! ```
//!
//! # 作りの要点
//!
//! - **左ペインは既存 listbox**。専用 TreeView ABI は作らない (契約 V12-F)。
//!   `[+]` / `[-]` + 2 桁インデントの文字列で木に見せ、開いた枝だけ `sys_ls` する。
//! - **右ペインとパス行は自前描画** (`on_paint`)。listbox の 1 項目は 40B しか
//!   持てず、アイコン + 3 桁組み (`name` / `size` / `type`) を並べられないため。
//!   ウィジェット木には**空ラベル**を置いてその矩形だけ借りる (ラベルは当たり
//!   判定の対象外なので、クリックは自前で拾える)。
//! - **入れ子ループ禁止**。確認・入力はすべて非同期モーダル。
//!   `open → on_modal → modal_result` の 1 往復で、続きは [`PEND_*`] の状態機械。
//! - **起動は `session_launch`**。`exec_run` は呼ばない (契約 F1)。受理されると
//!   WM が `Quit{REPLACE_APP}` を投げるので、[`App::on_quit`] で fd を畳んで戻る。
//! - **大きいコピーはタイマ駆動**で 1 周 16KB (4 チャンク) まで。カーソル・時計・
//!   Quit が生き続ける (契約 F3 / V12-P)。
//!
//! # キー割り当て
//!
//! ```text
//!   TAB              ペイン切替 (左ツリー ⇄ 右一覧)
//!   ↑ / ↓            選択移動           ROLL UP/DOWN  ページ送り   HOME  先頭
//!   RETURN           左: その枝へ移動 / 右: ディレクトリへ移動・.bin を Run
//!   ←                左: 枝を畳む     / 右: 親ディレクトリへ
//!   →                左: 枝を開く
//!   ESC              メニューを閉じる (開いていなければ終了)
//!   F5               再読み込み
//!   N / R / C / M / D  New Folder / Rename / Copy / Move / Delete
//! ```

#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

mod fmt;
mod icons;
mod model;

use libos32gui::gapi::proto::{
    GuiEvent, GUI_COLOR_DISABLED, GUI_COLOR_EDIT_BG, GUI_COLOR_FACE, GUI_COLOR_LIGHT,
    GUI_COLOR_SEL_BG, GUI_COLOR_SEL_TEXT, GUI_COLOR_SHADOW, GUI_COLOR_TEXT, GUI_EV_BUTTON,
    GUI_MODAL_INPUT, GUI_MODAL_OK, GUI_MODAL_RESULT_OK, GUI_MODAL_YES_NO,
};
use libos32gui::gapi::types::{Rect, Style, SurfaceId};
use libos32gui::gapi::{draw_rect, fill_rect, hline, text};
use libos32gui::icon::draw_icon16;
use libos32gui::widget::{
    self, WidgetId, LIST_ROW_H, SCAN_DOWN, SCAN_ESC, SCAN_HOME, SCAN_LEFT, SCAN_RETURN,
    SCAN_RIGHT, SCAN_ROLLDOWN, SCAN_ROLLUP, SCAN_TAB, SCAN_UP,
};
use libos32gui::{modal, session, App, GuiResult, SizeSpec, Timer, Ui, Window, WindowSpec};
use model::*;
use os32api::KernelAPI;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let mut app = match Filer::build() {
        Ok(a) => a,
        Err(e) => return e.code(),
    };
    let _ = libos32gui::run(&mut app);
    /* 念のため: 通常終了でも fd を残さない (§10)。 */
    app.shutdown();
    0
}

/* ================================================================ */
/*  画面の寸法 (PM の /api/mouse 台本はこの数字を使う)                */
/* ================================================================ */

/// 窓の外形 (画面座標)。クライアント原点は +(2, 20) — 枠 2px + タイトル 18px。
const WIN_X: i16 = 8;
const WIN_Y: i16 = 8;
const WIN_W: i16 = 576;
const WIN_H: i16 = 344;

/// ルートコンテナの余白 / 間隔。
const ROOT_PAD: i16 = 4;
const ROOT_GAP: i16 = 4;
/// 左ツリーの幅。
const TREE_W: i16 = 160;
/// 下のパス行の高さ。
const PATH_H: i16 = 18;

/* --- 右ペインの中身 (ペイン矩形の左上からの相対 px) --- */
/// 見出し行の高さ。
const HEADER_H: i32 = 16;
/// 1 行の高さ (listbox と揃える)。
const ROW_H: i32 = 18;
/// 行領域の開始 y (枠 1 + 見出し 16 + 区切り 1)。
const ROWS_Y0: i32 = 18;
const COL_ICON: i32 = 3;
const COL_NAME: i32 = 22;
const COL_SIZE: i32 = 232;
const COL_TYPE: i32 = 330;
/// 名前欄に入る半角文字数 ((232-22-6)/8)。
const NAME_CHARS: usize = 25;

/* --- コンテキストメニュー (クライアント面内オーバレイ、契約 F5) --- */
const MENU_W: i32 = 96;
const MENU_ITEM_H: i32 = 16;
const MENU_PAD: i32 = 2;
const MENU_MAX: usize = 6;

/// ダブルクリックとみなす間隔 (tick = 10ms)。
const DBLCLICK_TICKS: u32 = 30;
/// コピーを進めるタイマ番号。
const TIMER_COPY: u8 = 1;

const PANE_TREE: u8 = 0;
const PANE_LIST: u8 = 1;

/* --- メニュー項目 --- */
const MI_RUN: u8 = 1;
const MI_OPEN: u8 = 2;
const MI_COPY: u8 = 3;
const MI_MOVE: u8 = 4;
const MI_RENAME: u8 = 5;
const MI_DELETE: u8 = 6;
const MI_NEWFOLDER: u8 = 7;
const MI_REFRESH: u8 = 8;
/// 印刷 (票 N4b §2)。選択中ファイルを `os32gui_print_file` でホストへ。
const MI_PRINT: u8 = 9;

/* --- 保留中の操作 (モーダルの結果をどう読むか) --- */
const PEND_NONE: u8 = 0;
const PEND_MKDIR: u8 = 1;
const PEND_RENAME: u8 = 2;
const PEND_DELETE_FILE: u8 = 3;
const PEND_RMDIR: u8 = 4;
const PEND_MOVE: u8 = 5;
const PEND_COPY_DST: u8 = 6;
const PEND_COPY_OVERWRITE: u8 = 7;
/// エラー表示だけのメッセージボックス (結果は捨てる)。
const PEND_ERROR: u8 = 8;
/// 移動の実行確認 (Yes → 移動先の入力へ)。
const PEND_MOVE_CONFIRM: u8 = 9;
/// ディレクトリ改名の確認 (Yes → 新しい名前の入力へ)。
const PEND_RENAME_CONFIRM: u8 = 10;

/* ================================================================ */
/*  アプリの状態                                                     */
/* ================================================================ */

struct Filer {
    win: Option<Window>,
    tree_lb: WidgetId,
    /// 右ペインの矩形を借りるための空ラベル。
    right_pad: WidgetId,
    /// パス行の矩形を借りるための空ラベル。
    path_pad: WidgetId,

    active: u8,
    tree_sel: i32,
    list_sel: usize,
    list_top: usize,

    /* 生のボタンイベント (同じイベントの widget 合成より先に見える) */
    cur_button: u8,
    cur_x: i32,
    swallow: bool,

    /* ツリーの左クリック。選択が変わらないと `on_select` が来ないので、
     * 印を付けて `after_commit` で拾う (§10 差し戻し 1)。 */
    tree_click: bool,
    tree_click_y: i32,

    /* ダブルクリックの合成 */
    last_tick: u32,
    last_row: i32,

    /* コンテキストメニュー */
    menu_open: bool,
    menu_rect: Rect,
    menu_items: [u8; MENU_MAX],
    menu_dis: [bool; MENU_MAX],
    menu_n: usize,
    menu_sel: i32,

    /* モーダルの状態機械 */
    pending: u8,
    dialog: u16,
    target: [u8; PATH_CAP],
    target_len: usize,
    target_dir: bool,
    dst: [u8; PATH_CAP],
    dst_len: usize,

    /* コピー */
    job: CopyJob,
    timer: Option<Timer>,

    status: [u8; 64],
    status_len: usize,
    /// `session_launch` が受理された後は新しい操作を始めない (§10)。
    launched: bool,
}

impl Filer {
    fn build() -> GuiResult<Filer> {
        let info = libos32gui::gapi::screen_info();
        let sw = info.width as i32;
        let sh = info.height as i32;
        /* タスクバー 24px を避ける (契約 D1)。画面が小さければ縮める。 */
        let w = clamp(WIN_W as i32, 320, sw - 2 * WIN_X as i32);
        let h = clamp(WIN_H as i32, 200, sh - 24 - 2 * WIN_Y as i32);

        let win = Window::create(
            &WindowSpec::new(b"File Manager", Rect::new(WIN_X, WIN_Y, w as i16, h as i16))
                .min_size(320, 200),
        )?;

        let root = widget::column(ROOT_PAD, ROOT_GAP)?;

        let panes = widget::row(0, ROOT_GAP)?;
        let tree_lb = widget::listbox()?;
        let right_pad = widget::label(b"")?;
        widget::add(panes, tree_lb, SizeSpec::Fixed(TREE_W))?;
        widget::add(panes, right_pad, SizeSpec::Flex(1))?;
        widget::add(root, panes, SizeSpec::Flex(1))?;

        let path_pad = widget::label(b"")?;
        widget::add(root, path_pad, SizeSpec::Fixed(PATH_H))?;

        win.set_root(root)?;
        win.set_focus()?;

        let mut f = Filer {
            win: Some(win),
            tree_lb,
            right_pad,
            path_pad,
            active: PANE_LIST,
            tree_sel: 0,
            list_sel: 0,
            list_top: 0,
            cur_button: 0,
            cur_x: 0,
            swallow: false,
            tree_click: false,
            tree_click_y: 0,
            last_tick: 0,
            last_row: -1,
            menu_open: false,
            menu_rect: Rect::EMPTY,
            menu_items: [0; MENU_MAX],
            menu_dis: [false; MENU_MAX],
            menu_n: 0,
            menu_sel: 0,
            pending: PEND_NONE,
            dialog: 0,
            target: [0; PATH_CAP],
            target_len: 0,
            target_dir: false,
            dst: [0; PATH_CAP],
            dst_len: 0,
            job: CopyJob::NEW,
            timer: None,
            status: [0; 64],
            status_len: 0,
            launched: false,
        };

        /* 起点は `/` (§1)。 */
        tree_init();
        f.rebuild_tree();
        let mut root_path = [0u8; PATH_CAP];
        root_path[0] = b'/';
        f.navigate(&root_path);
        Ok(f)
    }

    fn win_id(&self) -> u32 {
        match self.win.as_ref() {
            Some(w) => w.id(),
            None => 0,
        }
    }

    /// 終了処理。fd を残さない (§10)。
    fn shutdown(&mut self) {
        self.timer = None;
        self.job.abort();
    }

    /* ------------------------------------------------------------ */
    /*  再描画の申告 (描くのは次の Paint、契約 G4)                    */
    /* ------------------------------------------------------------ */

    fn repaint_all(&self) {
        if let Some(w) = self.win.as_ref() {
            let _ = w.invalidate_all();
        }
    }

    fn repaint(&self, r: Rect) {
        if r.is_empty() {
            return;
        }
        if let Some(w) = self.win.as_ref() {
            let _ = w.invalidate(r);
        }
    }

    /// 右ペインの 1 行の矩形 (見えていなければ空)。
    fn row_rect(&self, r: &Rect, row: usize) -> Rect {
        if row < self.list_top {
            return Rect::EMPTY;
        }
        let i = row - self.list_top;
        if i >= self.visible_rows(r) {
            return Rect::EMPTY;
        }
        Rect::new(
            r.x,
            (r.y as i32 + ROWS_Y0 + (i as i32) * ROW_H) as i16,
            r.w,
            ROW_H as i16,
        )
    }

    /// 行領域だけ (見出しと枠の下)。
    fn rows_band(&self, r: &Rect) -> Rect {
        let h = r.h as i32 - ROWS_Y0;
        if h <= 0 {
            return Rect::EMPTY;
        }
        Rect::new(r.x, (r.y as i32 + ROWS_Y0) as i16, r.w, h as i16)
    }

    /// 選択が動いたときの再描画。**動いた 2 行とパス行だけ**を申告する
    /// (見出しも枠も変わらない)。スクロールしたときは行領域だけ。
    ///
    /// ペイン全面 (約 400x300 = 12 万画素) を毎回塗り直すと、1 打鍵の描画に
    /// 1.3 秒かかっていた (v1.2 G3 実測)。行 2 本なら 1/15 以下になる。
    /// パス行は選択中の名前を出しているので必ず一緒に出し直す (差し戻し B)。
    fn repaint_sel(&self, old_sel: usize, old_top: usize) {
        let r = widget::rect(self.right_pad);
        if r.is_empty() {
            return;
        }
        if old_top != self.list_top {
            /* スクロールした: 行が全部ずれるので帯ごと。 */
            self.repaint(self.rows_band(&r));
        } else {
            self.repaint(self.row_rect(&r, old_sel));
            if self.list_sel != old_sel {
                self.repaint(self.row_rect(&r, self.list_sel));
            }
        }
        self.repaint_path();
    }

    fn repaint_path(&self) {
        self.repaint(widget::rect(self.path_pad));
    }

    /* ------------------------------------------------------------ */
    /*  現在地 (§2 / §3)                                             */
    /* ------------------------------------------------------------ */

    /// `path` (絶対パス) へ移動し、右ペインを **1 回だけ** 読み直す。
    fn navigate(&mut self, path: &[u8]) {
        let n = match check_abs(path) {
            Ok(n) => n,
            Err(e) => {
                self.error(b"Open", e);
                return;
            }
        };
        {
            let st = fs();
            let mut i = 0;
            while i < n {
                st.cwd[i] = path[i];
                i += 1;
            }
            st.cwd[n] = 0;
            st.cwd_len = n;
        }
        self.reload();
    }

    /// 現在地を読み直す (作成 / 削除 / 改名の後と F5)。
    fn reload(&mut self) {
        let rc = reload_entries();
        self.list_sel = 0;
        self.list_top = 0;
        self.last_row = -1;
        if rc < 0 {
            self.error(b"Read directory", rc);
        }
        self.repaint_all();
    }

    /// 選択中の行の絶対パスを [`Filer::target`] に置く。`..` は対象外。
    fn take_target(&mut self) -> bool {
        let (name, is_dir) = {
            let st = fs();
            match st.row_entry(self.list_sel) {
                None => return false,
                Some(e) => {
                    let mut n = [0u8; NAME_CAP];
                    let l = e.nlen as usize;
                    n[..l].copy_from_slice(&e.name[..l]);
                    (n, e.is_dir)
                }
            }
        };
        if name[0] == b'.' && name[1] == b'.' && name[2] == 0 {
            self.status_msg(b"select an entry first");
            return false;
        }
        let mut p = [0u8; PATH_CAP];
        let cwd = {
            let st = fs();
            let mut c = [0u8; PATH_CAP];
            c[..st.cwd_len].copy_from_slice(&st.cwd[..st.cwd_len]);
            c
        };
        match path_join(&cwd, &name, &mut p) {
            Ok(n) => {
                self.target = p;
                self.target_len = n;
                self.target_dir = is_dir;
                true
            }
            Err(e) => {
                self.error(b"Path", e);
                false
            }
        }
    }

    /* ------------------------------------------------------------ */
    /*  左ツリー (§1)                                                */
    /* ------------------------------------------------------------ */

    /// ツリーの表示行を組み直す (`indent + [+]/[-] + name`)。
    fn rebuild_tree(&mut self) {
        let _ = widget::list_clear(self.tree_lb);
        let n = fs().ntree;
        let mut i = 0;
        while i < n {
            let mut buf = [0u8; 40];
            let (depth, expanded) = {
                let st = fs();
                (st.tree[i].depth as usize, st.tree[i].expanded)
            };
            let mut k = 0usize;
            let mut d = 0usize;
            while d < depth && k + 2 < buf.len() {
                buf[k] = b' ';
                buf[k + 1] = b' ';
                k += 2;
                d += 1;
            }
            k = fmt::put(&mut buf, k, if expanded { b"[-] " } else { b"[+] " });
            let k = {
                let st = fs();
                fmt::put(&mut buf, k, st.tree[i].name_slice())
            };
            let _ = widget::list_add(self.tree_lb, &buf[..k]);
            i += 1;
        }
        if self.tree_sel >= n as i32 {
            self.tree_sel = if n > 0 { n as i32 - 1 } else { 0 };
        }
        let _ = widget::list_set_selection(self.tree_lb, self.tree_sel);
    }

    /// 枝を開く / 畳む。
    fn tree_toggle(&mut self, node: usize) {
        let expanded = {
            let st = fs();
            if node >= st.ntree {
                return;
            }
            st.tree[node].expanded
        };
        if expanded {
            tree_collapse(node);
        } else {
            let rc = tree_expand(node);
            if rc < 0 {
                self.error(b"Read directory", rc);
            }
        }
        self.tree_sel = node as i32;
        self.rebuild_tree();
    }

    /// 枝へ移動する (畳んでいれば開く)。
    fn tree_go(&mut self, node: usize) {
        let mut p = [0u8; PATH_CAP];
        if let Err(e) = tree_path(node, &mut p) {
            self.error(b"Path", e);
            return;
        }
        let expanded = {
            let st = fs();
            node < st.ntree && st.tree[node].expanded
        };
        if !expanded {
            let rc = tree_expand(node);
            if rc < 0 {
                self.error(b"Read directory", rc);
            }
        }
        self.tree_sel = node as i32;
        self.rebuild_tree();
        self.navigate(&p);
    }

    /// ツリー行の左クリックの処理。`[+]` / `[-]` の 3 桁 (24px) を叩いたら
    /// 開閉だけ、それ以外はその枝へ移動する。`on_select` と `after_commit`
    /// (選択が変わらないクリック) の両方から呼ぶ。
    fn tree_click_at(&mut self, node: usize, cur_x: i32) {
        let depth = {
            let st = fs();
            if node >= st.ntree {
                return;
            }
            st.tree[node].depth as i32
        };
        let r = widget::rect(self.tree_lb);
        let mx0 = r.x as i32 + 3 + depth * 16;
        if cur_x >= mx0 && cur_x < mx0 + 24 {
            self.tree_toggle(node);
        } else {
            self.tree_go(node);
        }
    }

    /// クリックの y が実在するツリー行に当たっているか。
    ///
    /// listbox は先頭行 (`top`) を公開しないが、**項目数が見える行数以下なら
    /// `top` は必ず 0** — `list_clear` が 0 に戻し、`ensure_visible` は選択が
    /// 下へはみ出したときしか動かさないため。あふれているときは可視行が
    /// すべて実項目なので、矩形の中ならどこでも当たり。
    fn tree_row_hit(&self, y: i32) -> bool {
        let r = widget::rect(self.tree_lb);
        if r.is_empty() {
            return false;
        }
        let row_h = LIST_ROW_H as i32;
        let n = fs().ntree;
        let h = r.h as i32 - 2;
        let vis = if h < row_h { 1 } else { (h / row_h) as usize };
        if n > vis {
            return true;
        }
        let rel = y - (r.y as i32 + 1);
        if rel < 0 {
            return false;
        }
        ((rel / row_h) as usize) < n
    }

    /* ------------------------------------------------------------ */
    /*  右ペインの行 (§3 / §4)                                       */
    /* ------------------------------------------------------------ */

    fn visible_rows(&self, r: &Rect) -> usize {
        let h = r.h as i32 - ROWS_Y0 - 1;
        if h < ROW_H {
            return 1;
        }
        (h / ROW_H) as usize
    }

    /// ペイン内の y から行番号を引く。
    fn row_at(&self, r: &Rect, y: i32) -> Option<usize> {
        let y0 = r.y as i32 + ROWS_Y0;
        if y < y0 {
            return None;
        }
        let i = ((y - y0) / ROW_H) as usize;
        if i >= self.visible_rows(r) {
            return None;
        }
        let row = self.list_top + i;
        if row >= fs().nent {
            return None;
        }
        Some(row)
    }

    fn ensure_visible(&mut self) {
        let r = widget::rect(self.right_pad);
        if r.is_empty() {
            return;
        }
        let vis = self.visible_rows(&r);
        if self.list_sel < self.list_top {
            self.list_top = self.list_sel;
        } else if self.list_sel >= self.list_top + vis {
            self.list_top = self.list_sel + 1 - vis;
        }
    }

    /// RETURN / ダブルクリック / メニューの Open・Run。
    fn activate_row(&mut self, row: usize) {
        let (name, is_dir) = {
            let st = fs();
            match st.row_entry(row) {
                None => return,
                Some(e) => {
                    let mut n = [0u8; NAME_CAP];
                    let l = e.nlen as usize;
                    n[..l].copy_from_slice(&e.name[..l]);
                    (n, e.is_dir)
                }
            }
        };
        if name[0] == b'.' && name[1] == b'.' && name[2] == 0 {
            self.go_parent();
            return;
        }
        let cwd = {
            let st = fs();
            let mut c = [0u8; PATH_CAP];
            c[..st.cwd_len].copy_from_slice(&st.cwd[..st.cwd_len]);
            c
        };
        let mut p = [0u8; PATH_CAP];
        if let Err(e) = path_join(&cwd, &name, &mut p) {
            self.error(b"Path", e);
            return;
        }
        if is_dir {
            self.navigate(&p);
        } else if is_bin(&name) {
            self.launch(&p);
        } else {
            self.status_msg(b"not an executable (.bin)");
            self.repaint_path();
        }
    }

    fn go_parent(&mut self) {
        let mut p = [0u8; PATH_CAP];
        {
            let st = fs();
            let mut c = [0u8; PATH_CAP];
            c[..st.cwd_len].copy_from_slice(&st.cwd[..st.cwd_len]);
            path_parent(&c, &mut p);
        }
        self.navigate(&p);
    }

    /// `.bin` を起動してもらう (契約 F1: `exec_run` は呼ばない)。
    fn launch(&mut self, path: &[u8]) {
        if self.launched {
            return;
        }
        let n = cstr_len(path);
        match session::session_launch(&path[..n]) {
            Ok(()) => {
                self.launched = true;
                self.status_msg(b"launch accepted - waiting for Quit");
                self.repaint_path();
            }
            /* ERR_FULL / NOSYS / INVAL は終了せずに見せる (§4)。 */
            Err(e) => self.error(b"Run", e.code()),
        }
    }

    /* ------------------------------------------------------------ */
    /*  コンテキストメニュー (契約 F5)                                */
    /* ------------------------------------------------------------ */

    fn open_menu(&mut self, x: i32, y: i32, on_row: bool) {
        self.menu_n = 0;
        self.menu_sel = 0;
        let mut items: [u8; MENU_MAX] = [0; MENU_MAX];
        let mut dis: [bool; MENU_MAX] = [false; MENU_MAX];
        let mut n = 0usize;

        if on_row {
            let (is_dir, bin, is_parent) = {
                let st = fs();
                match st.row_entry(self.list_sel) {
                    None => (false, false, true),
                    Some(e) => (
                        e.is_dir,
                        is_bin(e.name_slice()),
                        e.nlen == 2 && e.name[0] == b'.' && e.name[1] == b'.',
                    ),
                }
            };
            if is_parent {
                /* `..` は「開く」だけ。 */
                items[n] = MI_OPEN;
                n += 1;
            } else if is_dir {
                items[n] = MI_OPEN;
                n += 1;
                items[n] = MI_COPY;
                dis[n] = true; /* 再帰コピーは v1.2 対象外 (§7) */
                n += 1;
                items[n] = MI_MOVE;
                n += 1;
                items[n] = MI_RENAME;
                n += 1;
                items[n] = MI_DELETE;
                n += 1;
            } else {
                if bin {
                    items[n] = MI_RUN;
                    n += 1;
                }
                items[n] = MI_COPY;
                n += 1;
                items[n] = MI_MOVE;
                n += 1;
                items[n] = MI_RENAME;
                n += 1;
                items[n] = MI_DELETE;
                n += 1;
                /* 票 N4b §2: ファイルだけ「印刷」を出す (ディレクトリ / `..` は
                 * 対象外)。bin でも 6 項目 (RUN/COPY/MOVE/RENAME/DELETE/PRINT) で
                 * MENU_MAX に収まる。 */
                items[n] = MI_PRINT;
                n += 1;
            }
        } else {
            items[n] = MI_NEWFOLDER;
            n += 1;
            items[n] = MI_REFRESH;
            n += 1;
        }

        self.menu_items = items;
        self.menu_dis = dis;
        self.menu_n = n;

        /* クライアント矩形の中へ clamp する (契約 F5)。 */
        let (cw, ch) = match self.win.as_ref() {
            Some(w) => w.client_size(),
            None => return,
        };
        let mw = MENU_W;
        let mh = MENU_PAD * 2 + n as i32 * MENU_ITEM_H;
        let mut mx = x;
        let mut my = y;
        if mx + mw > cw as i32 {
            mx = cw as i32 - mw;
        }
        if my + mh > ch as i32 {
            my = ch as i32 - mh;
        }
        if mx < 0 {
            mx = 0;
        }
        if my < 0 {
            my = 0;
        }
        self.menu_rect = Rect::new(mx as i16, my as i16, mw as i16, mh as i16);
        self.menu_open = true;
        self.repaint(self.menu_rect);
    }

    /// 閉じるときは下地を描き直す (契約 F5)。
    fn close_menu(&mut self) {
        if !self.menu_open {
            return;
        }
        self.menu_open = false;
        self.repaint(self.menu_rect);
        self.menu_rect = Rect::EMPTY;
    }

    fn invoke(&mut self, item: u8) {
        match item {
            MI_OPEN => {
                let row = self.list_sel;
                self.activate_row(row);
            }
            MI_RUN => {
                let row = self.list_sel;
                self.activate_row(row);
            }
            MI_NEWFOLDER => self.op_mkdir(),
            MI_REFRESH => self.reload(),
            MI_RENAME => self.op_rename(),
            MI_DELETE => self.op_delete(),
            MI_COPY => self.op_copy(),
            MI_MOVE => self.op_move(),
            MI_PRINT => self.op_print(),
            _ => {}
        }
    }

    /* ------------------------------------------------------------ */
    /*  ファイル操作 (§5 / §6) — すべて非同期モーダル経由             */
    /* ------------------------------------------------------------ */

    /// 新しい操作を始められるか (モーダル待ち / コピー中 / 起動要求後は不可)。
    fn busy(&mut self) -> bool {
        if self.launched {
            self.status_msg(b"launch pending");
            self.repaint_path();
            return true;
        }
        if self.pending != PEND_NONE || self.job.active {
            self.status_msg(b"busy");
            self.repaint_path();
            return true;
        }
        false
    }

    fn ask(&mut self, kind: u16, msg: &[u8], pend: u8) {
        match modal::modal_open(self.win_id(), kind, msg) {
            Ok(id) => {
                self.pending = pend;
                self.dialog = id;
            }
            Err(e) => self.error(b"Dialog", e.code()),
        }
    }

    fn op_mkdir(&mut self) {
        if self.busy() {
            return;
        }
        self.ask(
            GUI_MODAL_INPUT,
            b"New folder name (no '/'):",
            PEND_MKDIR,
        );
    }

    fn op_rename(&mut self) {
        if self.busy() || !self.take_target() {
            return;
        }
        if self.target_dir {
            /* ディレクトリの改名は中身ごと動くので 1 枚挟む (§10 差し戻し 2)。 */
            let mut buf = [0u8; 96];
            let n = self.confirm_msg(&mut buf, b"Rename directory ");
            self.ask(GUI_MODAL_YES_NO, &buf[..n], PEND_RENAME_CONFIRM);
            return;
        }
        self.ask(
            GUI_MODAL_INPUT,
            b"Rename to (name only):",
            PEND_RENAME,
        );
    }

    /// 確認モーダルの本文 `<head><basename> ?` を組む (1 行しか描かれない)。
    fn confirm_msg(&self, buf: &mut [u8; 96], head: &[u8]) -> usize {
        let off = basename_off(&self.target[..self.target_len]);
        let mut n = fmt::put(buf, 0, head);
        n = fmt::put(buf, n, fit(&self.target[off..self.target_len], 48));
        fmt::put(buf, n, b" ?")
    }

    fn op_delete(&mut self) {
        if self.busy() || !self.take_target() {
            return;
        }
        /* モーダルの本文は 1 行しか描かれない (WM は改行を扱わない) ので
         * basename だけを出す。 */
        let mut buf = [0u8; 96];
        let head: &[u8] =
            if self.target_dir { b"Remove directory " } else { b"Delete file " };
        let n = self.confirm_msg(&mut buf, head);
        let pend = if self.target_dir { PEND_RMDIR } else { PEND_DELETE_FILE };
        self.ask(GUI_MODAL_YES_NO, &buf[..n], pend);
    }

    fn op_copy(&mut self) {
        if self.busy() || !self.take_target() {
            return;
        }
        if self.target_dir {
            /* 再帰コピーは v1.2 対象外 (§6)。 */
            self.error(b"Copy", OS32_ERR_ISDIR);
            return;
        }
        self.ask(
            GUI_MODAL_INPUT,
            b"Copy to (absolute path):",
            PEND_COPY_DST,
        );
    }

    fn op_move(&mut self) {
        if self.busy() || !self.take_target() {
            return;
        }
        /* 移動は元の場所から消える。まず対象を見せて確認する (§10 差し戻し 2)。 */
        let mut buf = [0u8; 96];
        let n = self.confirm_msg(&mut buf, b"Move ");
        self.ask(GUI_MODAL_YES_NO, &buf[..n], PEND_MOVE_CONFIRM);
    }

    /// 選択中ファイルを印刷する (票 N4b §2)。`os32gui_print_file` はストリーミング
    /// (メモリ一定) なので大きいファイルでも溢れない。GUI 配下では待ちが park に
    /// なり WM は回るが、**このアプリの UI は印刷が終わるまで固まる** ので、先に
    /// 「Printing...」を出しておく (park 前に描けるかは WM 次第。現状は戻ってから
    /// 反映されるが、意図として先に置く)。
    fn op_print(&mut self) {
        if self.busy() || !self.take_target() {
            return;
        }
        if self.target_dir {
            /* ディレクトリは印刷対象外 (メニューにも出さないが念のため)。 */
            self.error(b"Print", OS32_ERR_ISDIR);
            return;
        }
        self.status_msg(b"Printing...");
        self.repaint_path();

        let off = basename_off(&self.target[..self.target_len]);
        let mut pages: u32 = 0;
        let mut svc: u32 = 0;
        let rc = libos32gui::host::print_file(
            &self.target[off..self.target_len],
            &self.target[..self.target_len],
            Some(&mut pages),
            Some(&mut svc),
        );
        if rc >= 0 {
            let mut buf = [0u8; 64];
            let mut n = fmt::put(&mut buf, 0, b"Printed ");
            n = fmt::put_u32(&mut buf, n, pages);
            n = fmt::put(&mut buf, n, b" page(s)");
            self.status_msg(&buf[..n]);
            self.repaint_path();
        } else if rc == os32api::host::HOST_ESERVICE {
            /* 業務失敗: svc に 409 / 500 / 503 (票 §1)。数値で区別する。 */
            let mut buf = [0u8; 96];
            let mut n = fmt::put(&mut buf, 0, b"Print service ");
            n = fmt::put_u32(&mut buf, n, svc);
            self.status_msg(&buf[..n]);
            self.repaint_path();
            if self.pending == PEND_NONE {
                if let Ok(id) = modal::modal_open(self.win_id(), GUI_MODAL_OK, &buf[..n]) {
                    self.pending = PEND_ERROR;
                    self.dialog = id;
                }
            }
        } else {
            /* リンク未確立 / NIC 無し / タイムアウト / 引数不正 / IO / 中断 (HOST_E*)。 */
            self.error(b"Print", rc);
        }
    }

    /// 入力された名前を現在地に足して `sys_mkdir`。
    fn do_mkdir(&mut self, name: &[u8]) {
        if let Err(e) = check_name(name) {
            self.error(b"New Folder", e);
            return;
        }
        let cwd = self.cwd_copy();
        let mut p = [0u8; PATH_CAP];
        if let Err(e) = path_join(&cwd, name, &mut p) {
            self.error(b"New Folder", e);
            return;
        }
        let rc = mkdir(&p);
        if rc < 0 {
            self.error(b"New Folder", rc);
            return;
        }
        self.status_msg(b"folder created");
        self.reload();
    }

    fn do_rename(&mut self, name: &[u8]) {
        if let Err(e) = check_name(name) {
            self.error(b"Rename", e);
            return;
        }
        let cwd = self.cwd_copy();
        let mut p = [0u8; PATH_CAP];
        if let Err(e) = path_join(&cwd, name, &mut p) {
            self.error(b"Rename", e);
            return;
        }
        let src = self.target_copy();
        let rc = rename(&src, &p);
        if rc < 0 {
            self.error(b"Rename", rc);
            return;
        }
        self.status_msg(b"renamed");
        self.reload();
    }

    fn do_move(&mut self, dstpath: &[u8]) {
        if let Err(e) = check_abs(dstpath) {
            self.error(b"Move", e);
            return;
        }
        let mut d = [0u8; PATH_CAP];
        copy_path(&mut d, dstpath);
        let src = self.target_copy();
        /* 同一 FS 内だけ。cross-FS は VFS が ERR_INVAL を返すのでそのまま出す (§5)。 */
        let rc = rename(&src, &d);
        if rc < 0 {
            self.error(b"Move", rc);
            return;
        }
        self.status_msg(b"moved");
        self.reload();
    }

    fn do_delete(&mut self, dir: bool) {
        let p = self.target_copy();
        let rc = if dir { rmdir(&p) } else { unlink(&p) };
        if rc < 0 {
            self.error(if dir { b"Remove directory" } else { b"Delete" }, rc);
            return;
        }
        self.status_msg(if dir { b"directory removed" } else { b"file deleted" });
        self.reload();
    }

    /// コピー先が決まった。既存なら上書き確認 (§6)。
    fn do_copy_dst(&mut self, dstpath: &[u8]) {
        if let Err(e) = check_abs(dstpath) {
            self.error(b"Copy", e);
            return;
        }
        let mut d = [0u8; PATH_CAP];
        let mut dl = copy_path(&mut d, dstpath);

        let mut stbuf = Stat::ZERO;
        let mut exists = stat(&d, &mut stbuf) == 0;
        if exists && stbuf.is_dir() {
            /* ディレクトリを指されたら元の名前で中へ入れる。 */
            let src = self.target_copy();
            let off = basename_off(&src);
            let mut joined = [0u8; PATH_CAP];
            match path_join(&d, &src[off..], &mut joined) {
                Ok(n) => {
                    d = joined;
                    dl = n;
                }
                Err(e) => {
                    self.error(b"Copy", e);
                    return;
                }
            }
            stbuf = Stat::ZERO;
            exists = stat(&d, &mut stbuf) == 0;
            if exists && stbuf.is_dir() {
                self.error(b"Copy", OS32_ERR_ISDIR);
                return;
            }
        }
        /* コピー元と同じ実体を指していないか (レビュー指摘 P1、2026-09-10)。
         * `CopyJob::start` は読む前に宛先を O_TRUNC で開くので、同一だと
         * **元ファイルを空にしてから空をコピーする** = 中身が消える。
         *
         * 判定は 2 本立てにする:
         *   1. inode — `/a/./b` や余分な `/` のような別表記を捉えられる。
         *      ただし **inode を返さないファイルシステムがある**
         *      (`fs/hostdrvfs.c` は st_dev/st_ino を 0 のままにする)。
         *      0 のまま比べると `/host` 配下の任意の 2 ファイルが「同一」に
         *      なって正当なコピーまで拒否するので、**非 0 のときだけ**信じる。
         *   2. パス — inode が使えないときの受け皿。**生の文字列では足りない**。
         *      filer の `check_abs` は正規化しないが VFS は `.` `..` `//` を
         *      畳むので、`/host/a.txt` と `/host/./a.txt` は文字列としては別物
         *      なのに同じファイルを開く。VFS と同じ規則で正規化してから比べる
         *      (2026-09-10 のレビュー指摘 P1/blocker)。
         *      正規化できない (深さ・長さ超過) ときは判定を諦めず、
         *      **安全側に倒して拒否する** — 中身を失うより断る方がまし。 */
        if exists {
            let src = self.target_copy();
            let mut sst = Stat::ZERO;
            let same_ino = stat(&src, &mut sst) == 0
                && sst.st_ino != 0
                && sst.st_dev == stbuf.st_dev
                && sst.st_ino == stbuf.st_ino;
            let mut ns = [0u8; PATH_CAP];
            let mut nd = [0u8; PATH_CAP];
            let same_path = match (normalize_abs(&src, &mut ns),
                                   normalize_abs(&d, &mut nd)) {
                (Some(a), Some(b)) => a == b && ns[..a] == nd[..b],
                _ => true,
            };
            if same_path || same_ino {
                self.error(b"Copy", ERR_SAME_FILE);
                return;
            }
        }
        self.dst = d;
        self.dst_len = dl;
        if exists {
            let mut buf = [0u8; 96];
            let off = basename_off(&self.dst[..self.dst_len]);
            let mut n = fmt::put(&mut buf, 0, b"Overwrite ");
            n = fmt::put(&mut buf, n, fit(&self.dst[off..self.dst_len], 48));
            n = fmt::put(&mut buf, n, b" ?");
            self.ask(GUI_MODAL_YES_NO, &buf[..n], PEND_COPY_OVERWRITE);
        } else {
            self.start_copy(true);
        }
    }

    /// コピーを開始してタイマを張る (§6: 1 周 16KB)。
    fn start_copy(&mut self, created: bool) {
        let src = self.target_copy();
        let mut d = [0u8; PATH_CAP];
        d[..self.dst_len + 1].copy_from_slice(&self.dst[..self.dst_len + 1]);
        let rc = self.job.start(&src, &d, created);
        if rc < 0 {
            self.error(b"Copy", rc);
            return;
        }
        if let Some(w) = self.win.as_ref() {
            self.timer = Timer::repeating(w, TIMER_COPY, 1).ok();
        }
        if self.timer.is_none() {
            /* タイマが張れないなら中断する (同期で回すと UI が止まる)。 */
            self.job.abort();
            self.error(b"Copy", -13);
            return;
        }
        self.status_msg(b"copying...");
        self.repaint_path();
    }

    /* ------------------------------------------------------------ */
    /*  小道具                                                       */
    /* ------------------------------------------------------------ */

    fn cwd_copy(&self) -> [u8; PATH_CAP] {
        let st = fs();
        let mut c = [0u8; PATH_CAP];
        c[..st.cwd_len].copy_from_slice(&st.cwd[..st.cwd_len]);
        c
    }

    fn target_copy(&self) -> [u8; PATH_CAP] {
        let mut c = [0u8; PATH_CAP];
        c[..self.target_len].copy_from_slice(&self.target[..self.target_len]);
        c
    }

    fn status_msg(&mut self, msg: &[u8]) {
        self.status_len = fmt::put(&mut self.status, 0, msg);
    }

    /// VFS エラーを握り潰さない (§9)。操作名 + 名前 + 数値を出す。
    fn error(&mut self, op: &[u8], code: i32) {
        let mut buf = [0u8; 160];
        let mut n = fmt::put(&mut buf, 0, op);
        n = fmt::put(&mut buf, n, b" failed: ");
        n = fmt::put(&mut buf, n, err_name(code));
        n = fmt::put(&mut buf, n, b" (");
        n = fmt::put_num(&mut buf, n, code);
        n = fmt::put(&mut buf, n, b")");
        self.status_msg(&buf[..n]);
        self.repaint_path();
        if self.pending != PEND_NONE {
            /* 未 consume の結果があると MODAL_OPEN は ERR_FULL (契約 M2)。
             * その場合はステータス行だけに出す。 */
            return;
        }
        match modal::modal_open(self.win_id(), GUI_MODAL_OK, &buf[..n]) {
            Ok(id) => {
                self.pending = PEND_ERROR;
                self.dialog = id;
            }
            Err(_) => {}
        }
    }
}

/* ================================================================ */
/*  ハンドラ                                                         */
/* ================================================================ */

impl App for Filer {
    /// 生イベント。**ウィジェット合成より先**に来るので、ここでボタン種別
    /// (左 1 / 右 2、契約 D4) と座標を控え、右ペインの当たり判定を済ませる。
    fn on_raw(&mut self, _ui: &mut Ui, ev: &GuiEvent) {
        self.swallow = false;
        self.cur_button = 0;
        if ev.kind != GUI_EV_BUTTON || ev.sub == 0 {
            return;
        }
        let b = ev.button();
        let x = b.x as i32;
        let y = b.y as i32;
        self.cur_button = b.button;
        self.cur_x = x;

        if self.menu_open {
            /* メニューが開いている間は下のウィジェットに触らせない。 */
            self.swallow = true;
            self.cur_button = 0;
            if b.button != 1 {
                return;
            }
            if self.menu_rect.contains(x, y) {
                let k = (y - self.menu_rect.y as i32 - MENU_PAD) / MENU_ITEM_H;
                let ok = k >= 0 && (k as usize) < self.menu_n && !self.menu_dis[k as usize];
                let item = if ok { self.menu_items[k as usize] } else { 0 };
                self.close_menu();
                if ok {
                    self.invoke(item);
                }
            } else {
                self.close_menu();
            }
            return;
        }

        let rp = widget::rect(self.right_pad);
        if !rp.is_empty() && rp.contains(x, y) {
            self.active = PANE_LIST;
            let row = self.row_at(&rp, y);
            if b.button == 2 {
                let old_sel = self.list_sel;
                if let Some(rw) = row {
                    self.list_sel = rw;
                }
                self.repaint_sel(old_sel, self.list_top);
                self.open_menu(x, y, row.is_some());
                return;
            }
            if b.button == 1 {
                if let Some(rw) = row {
                    /* ダブルクリックは自前で合成する (同じ行 / 30 tick 以内)。 */
                    let t = os32api::get_tick();
                    let dbl =
                        self.last_row == rw as i32 && t.wrapping_sub(self.last_tick) <= DBLCLICK_TICKS;
                    let old_sel = self.list_sel;
                    self.list_sel = rw;
                    self.last_row = rw as i32;
                    self.last_tick = t;
                    self.repaint_sel(old_sel, self.list_top);
                    if dbl {
                        self.last_row = -1;
                        self.activate_row(rw);
                    }
                }
            }
            return;
        }

        let tr = widget::rect(self.tree_lb);
        if !tr.is_empty() && tr.contains(x, y) {
            /* 行の確定はライブラリ側 (スクロール量を持っているのはあちら)。
             * ここではペインを切り替え、左クリックなら印を付けるだけ。
             * 選択が変わるクリックは `on_select` が、変わらないクリック
             * (すでに選ばれている行) は `after_commit` が処理する。 */
            self.active = PANE_TREE;
            if b.button == 1 {
                self.tree_click = true;
                self.tree_click_y = y;
            }
        }
    }

    /// 1 周の commit の後。選択が変わらなかったツリーのクリックをここで拾う
    /// (ライブラリは同じ行を叩いても `WEV_SELECT` を出さない)。`on_select`
    /// が処理したときは印が下りているので、二重には効かない。
    fn after_commit(&mut self, _ui: &mut Ui) {
        if !self.tree_click {
            return;
        }
        self.tree_click = false;
        if self.menu_open || self.swallow {
            return;
        }
        if !self.tree_row_hit(self.tree_click_y) {
            return; /* 最終行より下の余白 */
        }
        let sel = widget::list_selection(self.tree_lb);
        if sel < 0 {
            return;
        }
        self.tree_sel = sel;
        self.tree_click_at(sel as usize, self.cur_x);
    }

    /// 左ツリーの選択。クリック由来のときだけ移動する (矢印での移動では読み直さない)。
    fn on_select(&mut self, _ui: &mut Ui, w: WidgetId, index: i32) {
        if w != self.tree_lb {
            return;
        }
        /* このクリックはここで片付ける (`after_commit` の取りこぼし用の印を下ろす)。 */
        self.tree_click = false;
        if self.swallow || self.menu_open || (self.active != PANE_TREE && self.cur_button == 0) {
            /* メニュー操作 / 右ペイン操作の巻き添えは戻す。 */
            let _ = widget::list_set_selection(self.tree_lb, self.tree_sel);
            return;
        }
        self.tree_sel = index;
        if self.cur_button != 1 {
            return; /* キー移動と右クリックは選択だけ */
        }
        self.tree_click_at(index as usize, self.cur_x);
    }

    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, ch: u8, _mods: u8, down: bool) {
        if !down {
            return;
        }
        if self.menu_open {
            if scan == SCAN_ESC {
                self.close_menu();
            } else if scan == SCAN_UP {
                if self.menu_sel > 0 {
                    self.menu_sel -= 1;
                    self.repaint(self.menu_rect);
                }
            } else if scan == SCAN_DOWN {
                if (self.menu_sel as usize) + 1 < self.menu_n {
                    self.menu_sel += 1;
                    self.repaint(self.menu_rect);
                }
            } else if scan == SCAN_RETURN {
                let k = self.menu_sel as usize;
                let ok = k < self.menu_n && !self.menu_dis[k];
                let item = if ok { self.menu_items[k] } else { 0 };
                self.close_menu();
                if ok {
                    self.invoke(item);
                }
            }
            return;
        }

        if scan == SCAN_ESC {
            ui.quit();
            return;
        }
        if scan == SCAN_TAB {
            self.active = if self.active == PANE_TREE { PANE_LIST } else { PANE_TREE };
            self.repaint_all();
            return;
        }
        if scan == SCAN_F5 {
            self.reload();
            return;
        }
        if scan == SCAN_RETURN {
            if self.active == PANE_TREE {
                let n = self.tree_sel as usize;
                self.tree_go(n);
            } else {
                let row = self.list_sel;
                self.activate_row(row);
            }
            return;
        }
        if scan == SCAN_LEFT {
            if self.active == PANE_TREE {
                let n = self.tree_sel as usize;
                let expanded = {
                    let st = fs();
                    n < st.ntree && st.tree[n].expanded
                };
                if expanded {
                    self.tree_toggle(n);
                }
            } else {
                self.go_parent();
            }
            return;
        }
        if scan == SCAN_RIGHT {
            if self.active == PANE_TREE {
                let n = self.tree_sel as usize;
                let expanded = {
                    let st = fs();
                    n < st.ntree && st.tree[n].expanded
                };
                if !expanded {
                    self.tree_toggle(n);
                }
            }
            return;
        }

        if self.active == PANE_LIST {
            let nent = fs().nent;
            let r = widget::rect(self.right_pad);
            let page = if r.is_empty() { 1 } else { self.visible_rows(&r) };
            let old_sel = self.list_sel;
            let old_top = self.list_top;
            let mut moved = true;
            if scan == SCAN_UP {
                if self.list_sel > 0 {
                    self.list_sel -= 1;
                }
            } else if scan == SCAN_DOWN {
                if self.list_sel + 1 < nent {
                    self.list_sel += 1;
                }
            } else if scan == SCAN_ROLLDOWN {
                self.list_sel = if self.list_sel + page < nent { self.list_sel + page } else if nent > 0 { nent - 1 } else { 0 };
            } else if scan == SCAN_ROLLUP {
                self.list_sel = self.list_sel.saturating_sub(page);
            } else if scan == SCAN_HOME {
                self.list_sel = 0;
            } else {
                moved = false;
            }
            if moved {
                self.ensure_visible();
                self.repaint_sel(old_sel, old_top);
                return;
            }
        }

        /* 1 文字ショートカット (/api/key の台本用)。 */
        match ch {
            b'n' | b'N' => self.op_mkdir(),
            b'r' | b'R' => self.op_rename(),
            b'c' | b'C' => self.op_copy(),
            b'm' | b'M' => self.op_move(),
            b'd' | b'D' => self.op_delete(),
            b'p' | b'P' => self.op_print(),
            _ => {}
        }
    }

    /// モーダルの完了。**必ず先に `modal_result` で consume** してから次を開く (§10)。
    fn on_modal(&mut self, _ui: &mut Ui, dialog: u16, _result: i16) {
        let mut val = [0u8; 256];
        let r = modal::modal_result(dialog, &mut val);
        let op = self.pending;
        self.pending = PEND_NONE;
        if dialog != self.dialog {
            return; /* 知らないダイアログ */
        }
        let m = match r {
            Ok(m) => m,
            Err(e) => {
                if op != PEND_ERROR {
                    self.error(b"Dialog", e.code());
                }
                return;
            }
        };
        if op == PEND_ERROR || op == PEND_NONE {
            return;
        }
        if m.result != GUI_MODAL_RESULT_OK {
            /* Cancel / No。確認で止めたときは掴んでいた対象も捨てる。 */
            if op == PEND_MOVE_CONFIRM || op == PEND_RENAME_CONFIRM {
                self.target_len = 0;
                self.target_dir = false;
                self.status_msg(b"cancelled");
                self.repaint_path();
            }
            return;
        }
        let n = if m.copied > 255 { 255 } else { m.copied };
        val[n] = 0;
        let mut value = [0u8; PATH_CAP];
        value[..n + 1].copy_from_slice(&val[..n + 1]);

        match op {
            /* 確認で Yes。掴んだ対象はそのままに、次の入力を開く
             * (`pending` は上で NONE に戻してあるので `ask` が使える)。 */
            PEND_MOVE_CONFIRM => self.ask(
                GUI_MODAL_INPUT,
                b"Move to (absolute path):",
                PEND_MOVE,
            ),
            PEND_RENAME_CONFIRM => self.ask(
                GUI_MODAL_INPUT,
                b"Rename to (name only):",
                PEND_RENAME,
            ),
            PEND_MKDIR => self.do_mkdir(&value),
            PEND_RENAME => self.do_rename(&value),
            PEND_MOVE => self.do_move(&value),
            PEND_COPY_DST => self.do_copy_dst(&value),
            PEND_COPY_OVERWRITE => self.start_copy(false),
            PEND_DELETE_FILE => self.do_delete(false),
            PEND_RMDIR => self.do_delete(true),
            _ => {}
        }
    }

    /// コピーを 1 周分だけ進める (§6)。ここへ戻るので UI と Quit は生きている。
    fn on_timer(&mut self, _ui: &mut Ui, _window: u32, id: u8) {
        if id != TIMER_COPY {
            return;
        }
        if !self.job.active {
            self.timer = None;
            return;
        }
        let rc = self.job.step();
        if rc > 0 {
            let mut buf = [0u8; 48];
            let k = fmt::put(&mut buf, 0, b"copying... ");
            let k = fmt::put_u32(&mut buf, k, self.job.copied);
            let k = fmt::put(&mut buf, k, b" B");
            self.status_msg(&buf[..k]);
            self.repaint_path();
            return;
        }
        self.timer = None;
        if rc < 0 {
            /* 途中失敗。fd は step() が閉じ、作りかけは消してある (§6)。 */
            self.error(b"Copy", rc);
            self.reload();
        } else {
            self.status_msg(b"copy done");
            self.reload();
        }
    }

    /// WM からの終了要求 (`REPLACE_APP` など)。fd を残さず戻る (§4 / §10)。
    fn on_quit(&mut self, ui: &mut Ui, _reason: u8) {
        self.shutdown();
        ui.quit();
    }

    fn on_close(&mut self, ui: &mut Ui, _window: u32) {
        self.shutdown();
        self.win = None;
        ui.quit();
    }

    /// レイアウトが変わったら見えている行数が変わる。
    fn on_configure(&mut self, _ui: &mut Ui, _window: u32) {
        self.ensure_visible();
    }

    /// ウィジェット木の後 (契約 G2: 基底クリップは Paint 矩形に固定済み)。
    fn on_paint(&mut self, _ui: &mut Ui, _window: u32, surface: SurfaceId, _rect: Rect) {
        self.draw_right(surface);
        self.draw_path(surface);
        if self.menu_open {
            self.draw_menu(surface);
        }
    }
}

/* ================================================================ */
/*  描画 (右ペイン / パス行 / メニュー)                               */
/* ================================================================ */

impl Filer {
    fn draw_right(&self, surface: SurfaceId) {
        let r = widget::rect(self.right_pad);
        if r.is_empty() {
            return;
        }
        fill_rect(surface, r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_EDIT_BG));
        draw_rect(surface, r, Style::pen(GUI_COLOR_SHADOW));

        /* 見出し */
        let hdr = Style::new(GUI_COLOR_TEXT, GUI_COLOR_FACE);
        fill_rect(
            surface,
            Rect::new(r.x + 1, r.y + 1, r.w - 2, HEADER_H as i16),
            hdr,
        );
        let hy = r.y as i32 + 1;
        text(surface, r.x as i32 + COL_NAME, hy, b"Name", hdr);
        text(surface, r.x as i32 + COL_SIZE, hy, b"Size", hdr);
        text(surface, r.x as i32 + COL_TYPE, hy, b"Type", hdr);
        hline(
            surface,
            r.x as i32 + 1,
            r.y as i32 + 1 + HEADER_H,
            r.w as i32 - 2,
            Style::pen(GUI_COLOR_SHADOW),
        );

        /* 行 */
        let vis = self.visible_rows(&r);
        let nent = fs().nent;
        let mut i = 0usize;
        while i < vis {
            let row = self.list_top + i;
            if row >= nent {
                break;
            }
            let y = r.y as i32 + ROWS_Y0 + (i as i32) * ROW_H;
            let sel = row == self.list_sel;
            let (fg, bg) = if sel {
                (GUI_COLOR_SEL_TEXT, GUI_COLOR_SEL_BG)
            } else {
                (GUI_COLOR_TEXT, GUI_COLOR_EDIT_BG)
            };
            let style = Style::new(fg, bg);
            if sel {
                fill_rect(
                    surface,
                    Rect::new(r.x + 1, y as i16, r.w - 2, ROW_H as i16),
                    style,
                );
            }
            let st = fs();
            let e = &st.entries[st.order[row] as usize];
            /* アイコン (§8) */
            let ic = if e.is_dir {
                &icons::ICON_FOLDER
            } else if is_bin(e.name_slice()) {
                &icons::ICON_EXEC
            } else {
                &icons::ICON_FILE
            };
            draw_icon16(surface, r.x as i32 + COL_ICON, y + 1, ic);
            /* 名前 (欄からはみ出さないよう UTF-8 境界で切る) */
            let name = fit(e.name_slice(), NAME_CHARS);
            text(surface, r.x as i32 + COL_NAME, y + 1, name, style);
            /* サイズ */
            let mut sbuf = [0u8; 16];
            let sn = if e.is_dir {
                fmt::put(&mut sbuf, 0, b"-")
            } else {
                fmt::put_u32(&mut sbuf, 0, e.size)
            };
            text(surface, r.x as i32 + COL_SIZE, y + 1, &sbuf[..sn], style);
            /* 種別 */
            let mut tbuf = [0u8; 8];
            let tn = type_of(e, &mut tbuf);
            text(surface, r.x as i32 + COL_TYPE, y + 1, &tbuf[..tn], style);
            i += 1;
        }
    }

    fn draw_path(&self, surface: SurfaceId) {
        let r = widget::rect(self.path_pad);
        if r.is_empty() {
            return;
        }
        let style = Style::new(GUI_COLOR_TEXT, GUI_COLOR_FACE);
        fill_rect(surface, r, style);
        hline(
            surface,
            r.x as i32,
            r.y as i32,
            r.w as i32,
            Style::pen(GUI_COLOR_LIGHT),
        );
        let mut buf = [0u8; 128];
        let st = fs();
        let mut n = fmt::put(&mut buf, 0, st.cwd_slice());
        /* いま選んでいる行 — 破壊的なキー (d / r / m) が何に効くかを見せる。 */
        if let Some(e) = st.row_entry(self.list_sel) {
            n = fmt::put(&mut buf, n, b"  [");
            n = fmt::put(&mut buf, n, fit(e.name_slice(), 24));
            n = fmt::put(&mut buf, n, b"]");
        }
        if st.ent_overflow {
            n = fmt::put(&mut buf, n, b"  [truncated list]");
        }
        if self.status_len > 0 {
            n = fmt::put(&mut buf, n, b"  |  ");
            n = fmt::put(&mut buf, n, &self.status[..self.status_len]);
        }
        let max = (r.w as usize) / 8;
        let shown = fit(&buf[..n], max);
        text(surface, r.x as i32 + 2, r.y as i32 + 1, shown, style);
    }

    fn draw_menu(&self, surface: SurfaceId) {
        let m = self.menu_rect;
        if m.is_empty() {
            return;
        }
        fill_rect(surface, m, Style::new(GUI_COLOR_TEXT, GUI_COLOR_FACE));
        draw_rect(surface, m, Style::pen(GUI_COLOR_TEXT));
        let mut k = 0usize;
        while k < self.menu_n {
            let y = m.y as i32 + MENU_PAD + (k as i32) * MENU_ITEM_H;
            let sel = self.menu_sel == k as i32;
            let fg = if self.menu_dis[k] {
                GUI_COLOR_DISABLED
            } else if sel {
                GUI_COLOR_SEL_TEXT
            } else {
                GUI_COLOR_TEXT
            };
            let bg = if sel { GUI_COLOR_SEL_BG } else { GUI_COLOR_FACE };
            let style = Style::new(fg, bg);
            if sel {
                fill_rect(
                    surface,
                    Rect::new(m.x + 1, y as i16, m.w - 2, MENU_ITEM_H as i16),
                    style,
                );
            }
            text(surface, m.x as i32 + 4, y, item_label(self.menu_items[k]), style);
            k += 1;
        }
    }
}

/* ================================================================ */
/*  小道具                                                           */
/* ================================================================ */

/// PC-98 スキャンコード (`drivers/kbd.h` KEY_F5。`libos32gui::widget` にない分)。
const SCAN_F5: u8 = 0x66;

fn clamp(v: i32, lo: i32, hi: i32) -> i32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

/// `max` 文字 (半角換算) に収める。UTF-8 の途中では切らない。
fn fit(s: &[u8], max: usize) -> &[u8] {
    if s.len() <= max {
        return s;
    }
    let mut n = max;
    while n > 0 && (s[n] & 0xC0) == 0x80 {
        n -= 1;
    }
    &s[..n]
}

/// 種別欄の文字列 (`DIR` / `BIN` / 拡張子 / `FILE`)。
fn type_of(e: &Entry, out: &mut [u8; 8]) -> usize {
    if e.is_dir {
        return fmt::put(out, 0, b"DIR");
    }
    let s = e.name_slice();
    let mut dot = usize::MAX;
    let mut i = 0usize;
    while i < s.len() {
        if s[i] == b'.' {
            dot = i;
        }
        i += 1;
    }
    if dot == usize::MAX || dot + 1 >= s.len() {
        return fmt::put(out, 0, b"FILE");
    }
    let mut n = 0usize;
    let mut k = dot + 1;
    while k < s.len() && n < 4 {
        let c = s[k];
        out[n] = if c.is_ascii_lowercase() { c - 32 } else { c };
        n += 1;
        k += 1;
    }
    n
}

fn item_label(item: u8) -> &'static [u8] {
    match item {
        MI_RUN => b"Run",
        MI_OPEN => b"Open",
        MI_COPY => b"Copy",
        MI_MOVE => b"Move",
        MI_RENAME => b"Rename",
        MI_DELETE => b"Delete",
        MI_NEWFOLDER => b"New Folder",
        MI_REFRESH => b"Refresh",
        MI_PRINT => b"Print",
        _ => b"",
    }
}
