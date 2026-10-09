//! wm.rs — ウィンドウマネージャの中核状態 (票 W1 の構成「wm.rs」)。
//!
//! Window 表 (16)、WindowId (index:16 | generation:16)、Z 順、フォーカス、
//! 所有者、Configure、そして SHM スロット / タイマの表を持つ単一グローバル状態
//! `GuiState` を定義する。他のモジュール (ring / damage / visible / input /
//! chrome / timer / handler / pump) はこの `GuiState` を `&mut` で受けて操作する。
//!
//! 座標系の規約:
//!   - ウィンドウ矩形 (`x,y,w,h`) は枠・タイトルバーを含む **外形** (画面座標)。
//!   - クライアント領域 (アプリが描く面) は外形の内側。`client_origin()`。
//!   - 損傷 (dirty) / 可視領域 (vis) は **クライアントローカル座標** (0,0 =
//!     クライアント原点) で持つ。`Paint` の矩形もクライアントローカル (契約 G1/U2)。

#![allow(dead_code)]

use core::cell::UnsafeCell;

use os32api::gui::proto::{
    GuiRgb, GuiWinSpec, GUI_MAX_DAMAGE, GUI_MAX_TIMERS, GUI_MAX_WINDOWS, GUI_SLOT_MAX,
    GUI_WF_BORDER, GUI_WF_HAS_CLOSE, GUI_WF_MOVABLE, GUI_WF_VISIBLE, OS32_ERR_FULL, OS32_ERR_INVAL,
    OS32_ERR_STALE,
};

use crate::cursor::Cursor;
use crate::{
    chrome, cursor, damage, desktop, fep, input, kbdnav, lease, modal, session, startmenu, taskbar,
    visible,
};

/* ================================================================ */
/*  レイアウト定数 (ピクセル)                                        */
/* ================================================================ */
pub const TITLEBAR_H: i32 = 18;
pub const BORDER_W: i32 = 2;
pub const CLOSE_BTN: i32 = 14;
/// 可視領域 / 損傷を持てる矩形数の上限 (契約 U6: 可視 16 / 損傷 8)。
pub const MAX_VIS: usize = 16;
pub const MAX_DMG: usize = GUI_MAX_DAMAGE; /* 8 */
/// 32px 境界 (契約 G4、既存 gfx_add_dirty_rect と同じ規則)。
pub const DAMAGE_SNAP: i32 = 32;

/* WM が予約する SHM 内オフセット。**自前で定義しない** — 2026-09-17 まで
 * ここに 0x30000 の 3 つめの写しがあり、C と Rust proto を突き合わせる
 * make check-gui-proto の網から外れていた (決裁 D1 で +0x28000 へ動いた)。
 * 正典は include/memmap.h の MEM_SHM_GUI_OFFSET。 */
pub use os32api::gui::proto::GUI_SHM_OFFSET;

/* ================================================================ */
/*  Rect — 内部演算用 i32 矩形                                       */
/* ================================================================ */
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

impl Rect {
    pub const EMPTY: Rect = Rect {
        x: 0,
        y: 0,
        w: 0,
        h: 0,
    };

    #[inline]
    pub fn new(x: i32, y: i32, w: i32, h: i32) -> Rect {
        Rect { x, y, w, h }
    }
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.w <= 0 || self.h <= 0
    }
    #[inline]
    pub fn right(&self) -> i32 {
        self.x + self.w
    }
    #[inline]
    pub fn bottom(&self) -> i32 {
        self.y + self.h
    }
    #[inline]
    pub fn contains(&self, px: i32, py: i32) -> bool {
        px >= self.x && px < self.right() && py >= self.y && py < self.bottom()
    }
    /// 交差矩形 (空なら w/h<=0)。
    pub fn intersect(&self, o: &Rect) -> Rect {
        let x0 = if self.x > o.x { self.x } else { o.x };
        let y0 = if self.y > o.y { self.y } else { o.y };
        let x1 = if self.right() < o.right() {
            self.right()
        } else {
            o.right()
        };
        let y1 = if self.bottom() < o.bottom() {
            self.bottom()
        } else {
            o.bottom()
        };
        Rect {
            x: x0,
            y: y0,
            w: x1 - x0,
            h: y1 - y0,
        }
    }
    #[inline]
    pub fn intersects(&self, o: &Rect) -> bool {
        !self.intersect(o).is_empty()
    }
    #[inline]
    pub fn translate(&self, dx: i32, dy: i32) -> Rect {
        Rect {
            x: self.x + dx,
            y: self.y + dy,
            w: self.w,
            h: self.h,
        }
    }
    /// 2 矩形を含む最小の外接矩形。
    pub fn union(&self, o: &Rect) -> Rect {
        if self.is_empty() {
            return *o;
        }
        if o.is_empty() {
            return *self;
        }
        let x0 = if self.x < o.x { self.x } else { o.x };
        let y0 = if self.y < o.y { self.y } else { o.y };
        let x1 = if self.right() > o.right() {
            self.right()
        } else {
            o.right()
        };
        let y1 = if self.bottom() > o.bottom() {
            self.bottom()
        } else {
            o.bottom()
        };
        Rect {
            x: x0,
            y: y0,
            w: x1 - x0,
            h: y1 - y0,
        }
    }
}

/* ================================================================ */
/*  RectSet — 固定容量の互いに素な矩形集合                           */
/* ================================================================ */
#[derive(Clone, Copy)]
pub struct RectSet {
    pub rects: [Rect; MAX_VIS],
    pub len: usize,
}

impl RectSet {
    pub const EMPTY: RectSet = RectSet {
        rects: [Rect::EMPTY; MAX_VIS],
        len: 0,
    };

    #[inline]
    pub fn clear(&mut self) {
        self.len = 0;
    }
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    /// 追加 (空・容量超過は無視)。**溢れは呼び出し側が全面フォールバックで扱う。**
    #[inline]
    pub fn push(&mut self, r: Rect) -> bool {
        if r.is_empty() {
            return true;
        }
        if self.len >= MAX_VIS {
            return false;
        }
        self.rects[self.len] = r;
        self.len += 1;
        true
    }
    #[inline]
    pub fn as_slice(&self) -> &[Rect] {
        &self.rects[..self.len]
    }
    /// 合計面積 (present バイト見積り用)。
    pub fn area(&self) -> i64 {
        let mut a: i64 = 0;
        let mut i = 0;
        while i < self.len {
            a += (self.rects[i].w as i64) * (self.rects[i].h as i64);
            i += 1;
        }
        a
    }
}

/* ================================================================ */
/*  WindowId 符号化: index:16 | generation:16 (契約 T4 / U2)          */
/*  generation は 1 から。id==0 は無効。                             */
/* ================================================================ */
#[inline]
pub fn make_id(index: usize, gen: u16) -> u32 {
    ((gen as u32) << 16) | ((index as u32) & 0xFFFF)
}
#[inline]
pub fn id_index(id: u32) -> usize {
    (id & 0xFFFF) as usize
}
#[inline]
pub fn id_gen(id: u32) -> u16 {
    (id >> 16) as u16
}

/* ================================================================ */
/*  Win — ウィンドウ 1 枚                                            */
/* ================================================================ */
#[derive(Clone, Copy)]
pub struct Win {
    pub used: bool,
    pub gen: u16,     /* 現在の generation (再利用で ++) */
    pub owner: i32,   /* exec ネスト段 (res_owner_get) */
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
    pub flags: u16,
    pub min_w: i32,
    pub min_h: i32,
    pub title: [u8; 40],
    pub visible: bool,
    /* 損傷の 3 状態 (契約 G4)。クライアントローカル座標。 */
    pub dirty: RectSet,  /* invalidate 済み・Paint 未発行 */
    pub issued: RectSet, /* Paint 発行済み・COMMIT 待ち */
    /* 可視領域 (クライアントローカル、互いに素、≤16)。 */
    pub vis: RectSet,
    /* 可視領域が 16 矩形に収まらず一部を捨てた (レビュー #4 ⑤)。dirty が残る限り
     * OP_POLL ごとに vis_rot を進めて計算順を回し、捨てる断片を入れ替える
     * (契約 G4「超過分は次の周」)。 */
    pub vis_capped: bool,
    pub vis_rot: u32,
    /* Configure 通知が必要 (座標確定時に立てる)。 */
    pub configure_pending: bool,
    /* テキストカーソル (SET_TEXT_CURSOR)。FEP の未確定文字列を描く原点 (W2)。 */
    pub tc_x: i32,
    pub tc_y: i32,
    pub tc_visible: bool,
    /* パレットのリース (契約 G8、W2)。フォーカスを得たときに WM が入れる。
     * アプリは範囲を分けて複数回呼ぶ (lease_test は 1〜6 と 8〜15 の 2 回) ので、
     * **借りている index のビット集合 + 16 色分の色**で持つ。 */
    pub lease_mask: u16,
    pub lease_rgb: [GuiRgb; 16],
    /* ---- WM が持つ窓の状態 (票 KBD_NAV §1-2 の窓メニュー) ----
     * 最小化 = `visible` を落としてタスクバーにだけ残す (アプリの表示意図
     * `GUI_WF_VISIBLE` は触らない)。アプリが show / hide したら解ける。 */
    pub minimized: bool,
    /// 最大化中 (作業領域いっぱい)。`restore` が元の外形。
    pub maximized: bool,
    pub restore: Rect,
}

impl Win {
    pub const EMPTY: Win = Win {
        used: false,
        gen: 1,
        owner: 0,
        x: 0,
        y: 0,
        w: 0,
        h: 0,
        flags: 0,
        min_w: 0,
        min_h: 0,
        title: [0; 40],
        visible: false,
        dirty: RectSet::EMPTY,
        issued: RectSet::EMPTY,
        vis: RectSet::EMPTY,
        vis_capped: false,
        vis_rot: 0,
        configure_pending: false,
        tc_x: 0,
        tc_y: 0,
        tc_visible: false,
        lease_mask: 0,
        lease_rgb: [GuiRgb { r: 0, g: 0, b: 0 }; 16],
        minimized: false,
        maximized: false,
        restore: Rect::EMPTY,
    };

    /// 外形矩形 (画面座標)。
    #[inline]
    pub fn outer(&self) -> Rect {
        Rect::new(self.x, self.y, self.w, self.h)
    }
    #[inline]
    pub fn has_border(&self) -> bool {
        (self.flags & GUI_WF_BORDER) != 0
    }
    #[inline]
    pub fn has_close(&self) -> bool {
        (self.flags & GUI_WF_HAS_CLOSE) != 0
    }
    #[inline]
    pub fn movable(&self) -> bool {
        (self.flags & GUI_WF_MOVABLE) != 0
    }
    /// クライアント原点 (画面座標)。枠 + タイトルバーの内側。
    #[inline]
    pub fn client_origin(&self) -> (i32, i32) {
        (self.x + BORDER_W, self.y + BORDER_W + TITLEBAR_H)
    }
    /// クライアントの大きさ。
    #[inline]
    pub fn client_size(&self) -> (i32, i32) {
        let cw = self.w - BORDER_W * 2;
        let ch = self.h - BORDER_W * 2 - TITLEBAR_H;
        (if cw < 0 { 0 } else { cw }, if ch < 0 { 0 } else { ch })
    }
    /// クライアント矩形 (画面座標)。
    #[inline]
    pub fn client_rect_screen(&self) -> Rect {
        let (ox, oy) = self.client_origin();
        let (cw, ch) = self.client_size();
        Rect::new(ox, oy, cw, ch)
    }
    /// タイトルバー矩形 (画面座標)。
    #[inline]
    pub fn titlebar_rect(&self) -> Rect {
        Rect::new(
            self.x + BORDER_W,
            self.y + BORDER_W,
            self.w - BORDER_W * 2,
            TITLEBAR_H,
        )
    }
    /// 閉じるボタン矩形 (画面座標)。
    #[inline]
    pub fn close_rect(&self) -> Rect {
        let pad = (TITLEBAR_H - CLOSE_BTN) / 2;
        Rect::new(
            self.x + self.w - BORDER_W - CLOSE_BTN - pad,
            self.y + BORDER_W + pad,
            CLOSE_BTN,
            CLOSE_BTN,
        )
    }
    #[inline]
    pub fn id(&self, index: usize) -> u32 {
        make_id(index, self.gen)
    }
}

/* ================================================================ */
/*  Slot — SHM スロット (アプリ 1 本 = 16KB)                         */
/* ================================================================ */
#[derive(Clone, Copy)]
pub struct Slot {
    pub used: bool,
    pub owner: i32,
    pub serial: u16,               /* 入力イベントの serial (契約 T3) */
    pub last_reported_dropped: u16, /* dropped の「前回申告分」(消し込み用) */
}

impl Slot {
    pub const EMPTY: Slot = Slot {
        used: false,
        owner: 0,
        serial: 0,
        last_reported_dropped: 0,
    };
}

/* ================================================================ */
/*  Timer — 8 本 / アプリ (契約 U5)                                  */
/* ================================================================ */
#[derive(Clone, Copy)]
pub struct Timer {
    pub used: bool,
    pub owner: i32,
    pub window: u32,        /* 完全な WindowId */
    pub timer_id: u8,       /* 契約 U5 (Timer イベントの sub と同幅) */
    pub interval: u32,      /* ティック単位 (最小 1) */
    pub repeat: bool,       /* false = 単発 (発火後に消す) */
    pub next_deadline: u32, /* 発火予定 tick */
}

impl Timer {
    pub const EMPTY: Timer = Timer {
        used: false,
        owner: 0,
        window: 0,
        timer_id: 0,
        interval: 0,
        repeat: false,
        next_deadline: 0,
    };
}

/* ================================================================ */
/*  GuiState — 単一グローバル状態                                    */
/* ================================================================ */
pub struct GuiState {
    pub inited: bool,
    pub screen_w: i32,
    pub screen_h: i32,

    pub windows: [Win; GUI_MAX_WINDOWS],
    /* Z 順: 背面→前面。zorder[z_count-1] が最前面 = フォーカス。値は index。 */
    pub zorder: [usize; GUI_MAX_WINDOWS],
    pub z_count: usize,

    pub slots: [Slot; GUI_SLOT_MAX],
    pub timers: [Timer; GUI_MAX_TIMERS * GUI_SLOT_MAX],

    pub shm_base: u32,

    /* WM 自身の UI 状態 (契約 X3 でのみ進める)。 */
    pub drag_index: i32, /* ドラッグ中ウィンドウの index。-1=なし */
    pub drag_dx: i32,
    pub drag_dy: i32,
    pub drag_frame: Rect,   /* 現在描いているドラッグ枠 (空=未描画。XOR ではなく実線) */
    /// 最後にアクティブ (前面) のクロームで描いた窓の id (0 = なし)。前面が
    /// 替わったら旧・新の両方を描き直す ([`sync_active_chrome`])。
    pub chrome_active: u32,
    pub mouse_x: i32,
    pub mouse_y: i32,
    pub prev_buttons: u8,
    /// 実マウス (`mouse_poll`) の前回値。マウスキー (票 KBD_NAV §1-3) が
    /// `mouse_x/y` を動かすので、「実マウスが動いたか」はこちらと比べる
    /// (動いたら実マウスの値から続ける = 最後に動いた方が勝つ)。
    pub real_x: i32,
    pub real_y: i32,
    /// マウスキーが押している合成ボタン (MOUSE_BTN_*)。実マウスとは別に持ち、
    /// `prev_buttons` の差分には混ぜない (票 KBD_NAV §1-3)。
    pub synth_buttons: u8,
    /// キーボードでの WM 操作 (票 KBD_NAV)。
    pub kn: crate::kbdnav::KbdNav,

    /* WM が所有する画面損傷 (デスクトップ + クローム)。画面座標。 */
    pub screen_dirty: RectSet,

    /* 入力取りこぼしの累計 (kbd_dropped_count の前回値)。 */
    pub last_kbd_dropped: u32,

    /* マウスカーソル (損傷とは別経路。契約 W1 作業 7)。 */
    pub cursor: Cursor,

    /* 直近に読んだ tick (取り込み時刻の記録 P2 と期限判定で共有)。 */
    pub now: u32,

    /* ポンプ (X4) の再入防止。 */
    pub in_pump: bool,

    /* ---- パレットのリース (契約 G8、W2) ---- */
    /// `gfx_screen_info()` が申告する貸せる範囲 (契約 G5、決め打ち禁止)。
    /// `Win::lease_mask` (窓が借りている index) とは別物なので `cap_` を付ける。
    pub cap_lease_mask: u16,
    pub cap_lease_first: u16,
    pub cap_lease_count: u16,
    /// いま**実際に適用されている**リース元ウィンドウの完全な id (0 = 無し)。
    pub lease_applied: u32,
    /// リースの内容が変わったので入れ直しが要る。
    pub lease_dirty: bool,

    /* 標準単独ループ用フラグ。 */
    pub quit: bool,
    pub launch_pending: bool,
    /* X4 (ポンプ) で FEP がオンのとき退避する raw 打鍵 (レビュー #4 ②)。FEP の
     * 変換 (辞書検索) は X3 でだけ行う。次の X3 が先頭から取り込む。 */
    pub pending_raw: [i32; 32],
    pub pending_raw_n: usize,
    /// CTRL+STOP を raw で見た (契約 T6)。OP_WAIT の待ちループがこれで抜けて
    /// アプリへ戻し、カーネルが syscall の出口で畳む。消費したら 0 に戻す。
    pub abort_seen: bool,
    /// F2 のファイル選択で選んだ実行ファイル (NUL 終端)。0 長なら既定のデモ。
    pub launch_path: [u8; 256],
    pub launch_path_len: usize,

    /// 設定レジストリから読んで**画面に適用済み**の値 (票 S4 §2)。
    /// 編集中の値 (`modal`) と再読込値 (`settings` の私有状態) とは別物。
    pub cfg: crate::settings::GuiCfg,
}

impl GuiState {
    pub const NEW: GuiState = GuiState {
        inited: false,
        screen_w: 640,
        screen_h: 400,
        windows: [Win::EMPTY; GUI_MAX_WINDOWS],
        zorder: [0; GUI_MAX_WINDOWS],
        z_count: 0,
        slots: [Slot::EMPTY; GUI_SLOT_MAX],
        timers: [Timer::EMPTY; GUI_MAX_TIMERS * GUI_SLOT_MAX],
        shm_base: 0,
        drag_index: -1,
        drag_dx: 0,
        drag_dy: 0,
        drag_frame: Rect::EMPTY,
        chrome_active: 0,
        mouse_x: 320,
        mouse_y: 200,
        prev_buttons: 0,
        real_x: 320,
        real_y: 200,
        synth_buttons: 0,
        kn: crate::kbdnav::KbdNav::NEW,
        screen_dirty: RectSet::EMPTY,
        last_kbd_dropped: 0,
        cursor: Cursor::EMPTY,
        now: 0,
        in_pump: false,
        cap_lease_mask: 0,
        cap_lease_first: 0,
        cap_lease_count: 0,
        lease_applied: 0,
        lease_dirty: false,
        quit: false,
        launch_pending: false,
        pending_raw: [0; 32],
        pending_raw_n: 0,
        abort_seen: false,
        launch_path: [0; 256],
        launch_path_len: 0,
        cfg: crate::settings::GuiCfg::NEW,
    };
}

/// generation を 1〜0x7FFF の範囲で進める。**0x8000 以上にしない**のは、
/// WindowId (`gen << 16 | index`) を `gui_call` の戻り値 (i32) として非負で
/// 返すため (負値は `OS32_ERR_*` の領域)。
#[inline]
pub fn next_gen(g: u16) -> u16 {
    let n = g.wrapping_add(1) & 0x7FFF;
    if n == 0 {
        1
    } else {
        n
    }
}

pub(crate) struct GuiCell(pub(crate) UnsafeCell<GuiState>);
unsafe impl Sync for GuiCell {}
pub(crate) static GUI: GuiCell = GuiCell(UnsafeCell::new(GuiState::NEW));

/// グローバル状態への可変参照 (単一スレッド前提)。
#[inline]
pub fn g() -> &'static mut GuiState {
    unsafe { &mut *GUI.0.get() }
}

/* ================================================================ */
/*  GuiState — ウィンドウ / スロット / Z 順ヘルパ                    */
/* ================================================================ */
impl GuiState {
    /// 完全な id → 有効なウィンドウ index。generation 不一致・破棄済みは None。
    pub fn win_by_id(&self, id: u32) -> Option<usize> {
        if id == 0 {
            return None;
        }
        let idx = id_index(id);
        if idx >= GUI_MAX_WINDOWS {
            return None;
        }
        let w = &self.windows[idx];
        if w.used && w.gen == id_gen(id) {
            Some(idx)
        } else {
            None
        }
    }

    /// 最前面の**可視**ウィンドウの index (無ければ None)。キーボードフォーカスは
    /// これ。非表示の窓は Z 順に残っていてもフォーカスを持たない (レビュー #3 ⑥:
    /// 最前面を hide しても見えない窓へ Key/Pointer が配送され続けていた)。
    pub fn front_index(&self) -> Option<usize> {
        let mut z = self.z_count;
        while z > 0 {
            z -= 1;
            let i = self.zorder[z];
            if self.windows[i].used && self.windows[i].visible {
                return Some(i);
            }
        }
        None
    }
    /// 最前面ウィンドウの owner (無ければ 0)。
    pub fn front_owner(&self) -> i32 {
        match self.front_index() {
            Some(i) => self.windows[i].owner,
            None => 0,
        }
    }
    /// 最前面ウィンドウの完全な id (無ければ 0)。
    pub fn front_id(&self) -> u32 {
        match self.front_index() {
            Some(i) => self.windows[i].id(i),
            None => 0,
        }
    }

    fn z_add_top(&mut self, index: usize) {
        if self.z_count < GUI_MAX_WINDOWS {
            self.zorder[self.z_count] = index;
            self.z_count += 1;
        }
    }
    fn z_remove(&mut self, index: usize) {
        let mut i = 0;
        while i < self.z_count {
            if self.zorder[i] == index {
                let mut j = i;
                while j + 1 < self.z_count {
                    self.zorder[j] = self.zorder[j + 1];
                    j += 1;
                }
                self.z_count -= 1;
                return;
            }
            i += 1;
        }
    }
    /// index を最前面へ (= フォーカス)。Z 順の変化で露出計算が要る。
    pub fn bring_to_front(&mut self, index: usize) {
        if self.front_index() == Some(index) {
            return;
        }
        self.z_remove(index);
        self.z_add_top(index);
    }

    /// index を最背面へ (GRPH+ESC、票 KBD_NAV §1-2)。
    pub fn send_to_back(&mut self, index: usize) {
        if self.z_of(index).is_none() {
            return;
        }
        self.z_remove(index);
        let mut j = self.z_count;
        while j > 0 {
            self.zorder[j] = self.zorder[j - 1];
            j -= 1;
        }
        self.zorder[0] = index;
        self.z_count += 1;
    }

    /// z (0=背面) 番目のウィンドウ index。
    #[inline]
    pub fn z_at(&self, z: usize) -> usize {
        self.zorder[z]
    }
    /// index の Z 位置 (背面 0)。無ければ None。
    pub fn z_of(&self, index: usize) -> Option<usize> {
        let mut i = 0;
        while i < self.z_count {
            if self.zorder[i] == index {
                return Some(i);
            }
            i += 1;
        }
        None
    }

    /// 空きウィンドウスロットの index。
    pub fn alloc_win(&mut self) -> Option<usize> {
        let mut i = 0;
        while i < GUI_MAX_WINDOWS {
            if !self.windows[i].used {
                return Some(i);
            }
            i += 1;
        }
        None
    }

    /// owner → スロット番号。
    pub fn slot_of_owner(&self, owner: i32) -> Option<usize> {
        let mut i = 0;
        while i < GUI_SLOT_MAX {
            if self.slots[i].used && self.slots[i].owner == owner {
                return Some(i);
            }
            i += 1;
        }
        None
    }
    /// owner にスロットを割り当てる (既にあればそれ)。v1 は常に 0 を優先。
    pub fn alloc_slot(&mut self, owner: i32) -> Option<usize> {
        if let Some(s) = self.slot_of_owner(owner) {
            return Some(s);
        }
        let mut i = 0;
        while i < GUI_SLOT_MAX {
            if !self.slots[i].used {
                self.slots[i] = Slot::EMPTY;
                self.slots[i].used = true;
                self.slots[i].owner = owner;
                return Some(i);
            }
            i += 1;
        }
        None
    }

    /// WM 所有の画面損傷 (デスクトップ + クローム) に 1 矩形を足す。
    ///
    /// `RectSet::push` は容量 (16) を超えると**黙って捨てる**ので、そのまま
    /// 使うと塗り残しが残る。溢れた分は先頭と union して「過剰申告 = 安全側」に
    /// 倒す (転送量が増えるだけで、画面が壊れることは無い)。
    pub fn dirty_screen(&mut self, r: Rect) {
        let clip = r.intersect(&Rect::new(0, 0, self.screen_w, self.screen_h));
        if clip.is_empty() {
            return;
        }
        if !self.screen_dirty.push(clip) {
            self.screen_dirty.rects[0] = self.screen_dirty.rects[0].union(&clip);
        }
    }

    /// 画面座標の点 (px,py) を含む最前面の可視ウィンドウ index。
    pub fn hit_window(&self, px: i32, py: i32) -> Option<usize> {
        let mut z = self.z_count;
        while z > 0 {
            z -= 1;
            let idx = self.zorder[z];
            let w = &self.windows[idx];
            if w.used && w.visible && w.outer().contains(px, py) {
                return Some(idx);
            }
        }
        None
    }

    /// F2 のファイル選択が決めた起動パスを控える (NUL 終端で保持)。
    pub fn set_launch_path(&mut self, path: &[u8], len: usize) {
        let n = if len > 254 { 254 } else { len };
        let mut i = 0;
        while i < n {
            self.launch_path[i] = path[i];
            i += 1;
        }
        self.launch_path[n] = 0;
        self.launch_path_len = n;
    }

    /* ---- 所有者回収 (契約 T4 / U8。GUI_OP_OWNER_EXIT) ---- */
    pub fn reclaim_owner(&mut self, owner: i32) {
        /* ウィンドウ */
        let mut i = 0;
        while i < GUI_MAX_WINDOWS {
            if self.windows[i].used && self.windows[i].owner == owner {
                if self.lease_applied == self.windows[i].id(i) {
                    self.lease_dirty = true;
                }
                self.windows[i].lease_mask = 0;
                let vac = self.windows[i].outer();
                self.z_remove(i);
                let gen = next_gen(self.windows[i].gen);
                self.windows[i] = Win::EMPTY;
                self.windows[i].gen = gen;
                self.dirty_screen(vac);
                if self.drag_index == i as i32 {
                    drop_drag_frame(self);
                }
            }
            i += 1;
        }
        /* タイマ */
        let mut t = 0;
        while t < self.timers.len() {
            if self.timers[t].used && self.timers[t].owner == owner {
                self.timers[t] = Timer::EMPTY;
            }
            t += 1;
        }
        /* スロット */
        let mut s = 0;
        while s < GUI_SLOT_MAX {
            if self.slots[s].used && self.slots[s].owner == owner {
                self.slots[s] = Slot::EMPTY;
            }
            s += 1;
        }
    }
}

/// ドラッグ (マウス / キーボードの移動・サイズ) 中の窓が消えた: 枠を消して
/// 終わらせる (代行レビュー P3-5: 枠が画面に残っていた)。
/// キーボードの移動・サイズの状態 (`kn.kmode`) もここで終わらせる — 残すと、
/// 同じ添字に後から作られた窓のマウスドラッグが「キーボードの移動中」に見えて
/// 枠が追従しなかった (代行レビュー P3-A)。
fn drop_drag_frame(st: &mut GuiState) {
    let f = st.drag_frame;
    st.drag_index = -1;
    st.drag_frame = Rect::EMPTY;
    kbdnav::drop_kmove(st);
    input::erase_frame(st, f);
}

/// GUI_WF_VISIBLE ビットを visible フラグへ同期する補助。
#[inline]
pub fn wf_visible(flags: u16) -> bool {
    (flags & GUI_WF_VISIBLE) != 0
}

/* ================================================================ */
/*  作業領域 (契約 V12-D の D1)                                       */
/* ================================================================ */

/// 画面からタスクバー (下端 24px) を除いた作業領域。
/// 640x400 → 640x376 / 640x480 → 640x456。
#[inline]
pub fn work_area(st: &GuiState) -> Rect {
    Rect::new(0, 0, st.screen_w, st.screen_h - taskbar::TASKBAR_H)
}

/// 外形 (x,y,w,h) を作業領域へクランプした左上座標を返す。
///
/// **新規配置とドラッグ確定のときだけ**使う (契約 D1)。既存の窓が作業領域の
/// 外に居ること自体は fault にしないので、既存座標を勝手には動かさない。
/// 作業領域に収まらない大きさの窓は左上に寄せる (掴める場所を残す)。
pub fn clamp_to_work_area(st: &GuiState, x: i32, y: i32, w: i32, h: i32) -> (i32, i32) {
    let wa = work_area(st);
    let nx = if w > wa.w {
        wa.x
    } else if x < wa.x {
        wa.x
    } else if x + w > wa.right() {
        wa.right() - w
    } else {
        x
    };
    let ny = if h > wa.h {
        wa.y
    } else if y < wa.y {
        wa.y
    } else if y + h > wa.bottom() {
        wa.bottom() - h
    } else {
        y
    };
    (nx, ny)
}

/* ================================================================ */
/*  present / compositor (WM が所有する画面 = デスクトップ + クローム) */
/*                                                                  */
/*  クライアント面の内側には触れない (アプリの COMMIT が present する)。 */
/* ================================================================ */

/// 画面座標の矩形を **転送キューへ積む** (まだ VRAM へは出さない)。
///
/// `gfx_present_rect` は 1 回ごとに `gfx_present_dirty` を呼ぶので
/// `gfx_stats().commits` が矩形の数だけ増える。契約 P の「commit はループ 1 周
/// 1 回」を守るため、WM は矩形を `gfx_add_dirty_rect` で積み、最後に
/// [`flush_present`] を 1 回だけ呼ぶ。
pub fn queue_present(st: &GuiState, r: Rect) {
    /* 全画面 GFX 中は画面を持っていない (票 T8 D4a)。 */
    if crate::fullscreen::active() {
        return;
    }
    let clip = r.intersect(&Rect::new(0, 0, st.screen_w, st.screen_h));
    if clip.is_empty() {
        return;
    }
    unsafe {
        (os32api::api().gfx_add_dirty_rect)(clip.x, clip.y, clip.w, clip.h);
    }
}

/// 積んだ矩形をまとめて VRAM へ転送する (`commits` += 1)。積んだものが
/// 無ければカーネル側で早期 return する (commits は増えない)。
pub fn flush_present() {
    /* 全画面 GFX 中は画面を持っていない (票 T8 D4a)。 */
    if crate::fullscreen::active() {
        return;
    }
    unsafe {
        (os32api::api().gfx_present_dirty)();
    }
}

/// 1 矩形だけを即転送する近道 (積む + 流す)。
pub fn present_rect(st: &GuiState, r: Rect) {
    queue_present(st, r);
    flush_present();
}

/// 画面座標の矩形にデスクトップ + クロームを再合成する (バックバッファのみ)。
/// クライアント面は「窓の外形 − 内側」ではなく **窓の外形を chrome が描く枠のみ**
/// を触り、内側 (アプリ描画) は残す。デスクトップは「矩形 − 全窓外形」に塗る。
pub fn composite_rect(st: &GuiState, r: Rect) {
    /* 全画面 GFX 中は 1 画素も書かない (票 T8 D4a)。dirty は溜めておき、
     * 復帰の `composite_full` / `flush_screen_dirty` でまとめて出す。 */
    if crate::fullscreen::active() {
        return;
    }
    let clip = r.intersect(&Rect::new(0, 0, st.screen_w, st.screen_h));
    if clip.is_empty() {
        return;
    }
    /* デスクトップ: clip から全ての可視窓の外形を引いた残りを塗る。 */
    let mut region = RectSet::EMPTY;
    region.push(clip);
    let mut z = 0;
    while z < st.z_count {
        let idx = st.zorder[z];
        let w = &st.windows[idx];
        if w.used && w.visible {
            let (next, _ok) = visible::region_subtract_rect(&region, w.outer());
            region = next;
        }
        z += 1;
    }
    let mut i = 0;
    let mut hit_hint = false;
    while i < region.len {
        let d = region.rects[i];
        desktop::fill(st, d);
        if d.intersects(&desktop::hint_rect()) {
            hit_hint = true;
        }
        i += 1;
    }
    if hit_hint {
        desktop::draw_hint(st);
    }
    /* クローム: clip に掛かる窓を背面→前面で描き直す。
     *
     * 枠は **その窓の可視な外形 (外形 − 前面窓の外形)** で切って描く。切らずに
     * 描くと背面窓の右辺・下辺の 1px が前面窓のクライアント面に落ち、WM は
     * クライアント面を持たないので消せない (W-3、2026-09-11: File Manager の
     * リスト面を gui_bench の右辺と Help の下辺が貫いた)。
     *
     * `clip` との交差は取らない。装飾は clip より広く書くが、書く先は自分の
     * 外形の中だけなので他の窓・下地を壊さない。逆に clip で切ると、部分損傷の
     * 周に**文字セルの一部だけ**が塗り直されて字が欠ける (`kcg_draw_utf8` に
     * クリップが無く、セル単位でしか描けないため)。 */
    let mono = lease::mono(st);
    let mut z2 = 0;
    while z2 < st.z_count {
        let idx = st.zorder[z2];
        let w = &st.windows[idx];
        if w.used && w.visible && w.outer().intersects(&clip) {
            let active = z2 + 1 == st.z_count;
            let mut frame = RectSet::EMPTY;
            frame.push(w.outer());
            let mut za = z2 + 1;
            while za < st.z_count {
                let above = &st.windows[st.zorder[za]];
                if above.used && above.visible {
                    /* 容量超過で断片を捨てても「描かない」側に倒れるだけ
                     * (compute_vis と同じ方針)。上書き事故より欠けを採る。 */
                    let (next, _ok) = visible::region_subtract_rect(&frame, above.outer());
                    frame = next;
                }
                za += 1;
            }
            let mut f = 0;
            while f < frame.len {
                chrome::draw_window_chrome(w, active, mono, frame.rects[f]);
                f += 1;
            }
        }
        z2 += 1;
    }
    /* モーダルダイアログは WM 自身の窓なので、クロームの最後に直接描く
     * (契約 U8 / U4)。可視領域の計算でも「上にある窓」として扱われる。 */
    modal::draw(st, clip);
    /* タスクバーとメニューは最前面 (契約 D1 / D2 / D4)。可視領域からも
     * 引いてあるので、アプリの Paint / COMMIT がここへ来ることは無い。 */
    taskbar::draw(st, clip);
    startmenu::draw(st, clip);
}

/// 画面全体を合成して present する (起動時・フルスクリーン GFX からの復帰)。
/// **全画面 present は WM だけ** (契約 G4)。カーソルは描き直す。
pub fn composite_full(st: &mut GuiState) {
    /* 全画面 GFX 中は 1 画素も書かない (票 T8 D4a)。復帰は所有者が 1 に
     * 戻ってから (`crate::restore_screen`)。 */
    if crate::fullscreen::active() {
        return;
    }
    let whole = Rect::new(0, 0, st.screen_w, st.screen_h);
    cursor::discard(st); /* 下地は全部塗り替わる */
    composite_rect(st, whole);
    st.screen_dirty.clear();
    /* FEP の未確定行 / 候補窓は下地ごと消えたので次の周で描き直す。 */
    fep::mark_redraw();
    cursor::show(st);
    queue_present(st, whole);
    flush_present();
}

/// WM の最前面物 (モーダル / FEP の候補窓 / タスクバー / Start メニュー) のうち、
/// `regions` のどれかに掛かって**描き直しが要るもの**の矩形 (下から順に modal,
/// taskbar, startmenu, fep。要らないものは空)。描き直した物が後の物を上書きし得るので、
/// 描き直す物の矩形も判定範囲に足していく。描かずに決めるので、呼ぶ側はこれを見て
/// カーソルの退避を先に決められる (Codex レビュー 3 回目: カーソルは最後に 1 回)。
///
/// **重なり順は X3 の合成と同じ**: `composite_rect` がモーダル → タスクバー →
/// メニューを描き、FEP (未確定行 / 候補窓) はその後に `redraw_now` / `post_cycle` が
/// 単独で描く = FEP が最上位。ここで FEP をタスクバーより下にすると、画面下端で
/// タスクバーに重なる候補窓が、FEP だけを描く周 (候補の更新) と枠の周 (ここ) とで
/// 上下が入れ替わって点滅した (Codex レビュー 4 回目 P2)。FEP を最上位に置けば、
/// FEP だけを描き直す経路は順序を崩さない。
pub fn overlays_to_refresh(st: &GuiState, regions: &[Rect]) -> [Rect; 4] {
    let cands = [modal::rect(), taskbar::rect(st), startmenu::rect(), fep::rect()];
    let mut out = [Rect::EMPTY; 4];
    let mut i = 0;
    while i < 4 {
        let r = cands[i];
        if !r.is_empty() {
            let mut hit = regions.iter().any(|g| !g.is_empty() && g.intersects(&r));
            let mut j = 0;
            while j < i {
                if !out[j].is_empty() && out[j].intersects(&r) {
                    hit = true;
                }
                j += 1;
            }
            if hit {
                out[i] = r;
            }
        }
        i += 1;
    }
    out
}

/// [`overlays_to_refresh`] が返した物を描き直して present に積む (カーソルは呼ぶ側)。
pub fn refresh_overlays(st: &mut GuiState, list: &[Rect; 4]) {
    if !list[0].is_empty() && modal::refresh_if_hit(st, list[0]) {
        queue_present(st, list[0]);
    }
    if !list[1].is_empty() && taskbar::refresh_if_hit(st, list[1]) {
        queue_present(st, list[1]);
    }
    if !list[2].is_empty() && startmenu::refresh_if_hit(st, list[2]) {
        queue_present(st, list[2]);
    }
    if !list[3].is_empty() && fep::refresh_if_hit(st, list[3]) {
        queue_present(st, list[3]);
    }
}

/// 前面 (= アクティブのクローム) が前回の合成から替わっていたら、旧前面と新前面の
/// 外形を画面損傷に足す。
///
/// タイトルの色は z の最上位かどうかで決まるが、前面を替える経路 (マウス・
/// タスクバー・GRPH+TAB・set_focus・raise・閉じる・最小化 …) は新しい前面の外形
/// しか損傷にしていなかった。`composite_rect` は損傷に**掛かった**窓のクロームだけを
/// 描き直すので、旧前面が新前面と重なっていないと旧前面のタイトルがアクティブ色の
/// まま残った (2026-09-29 の束ねビルドの報告)。経路ごとに足すと漏れるので、合成の
/// 入口で 1 か所にまとめて見る。
fn sync_active_chrome(st: &mut GuiState) {
    let front = st.front_id();
    if front == st.chrome_active {
        return;
    }
    if let Some(oi) = st.win_by_id(st.chrome_active) {
        if st.windows[oi].used && st.windows[oi].visible {
            let o = st.windows[oi].outer();
            st.dirty_screen(o);
        }
    }
    if let Some(ni) = st.front_index() {
        let o = st.windows[ni].outer();
        st.dirty_screen(o);
    }
    st.chrome_active = front;
}

/// WM が溜めた画面損傷 (デスクトップ + クローム) を合成して present し、クリアする。
/// アプリのクライアント面 (COMMIT) とは独立。commit は 1 回にまとめる。
pub fn flush_screen_dirty(st: &mut GuiState) {
    /* 全画面 GFX 中は画面を持っていない (票 T8 D4a)。**dirty は落とさずに溜める**
     * (復帰の `composite_full` がまとめて片付ける)。ここで先に戻るのは、下の
     * `fep::redraw_now` / ドラッグ枠が合成の門を通らずに画素を置くため。 */
    if crate::fullscreen::active() {
        return;
    }
    sync_active_chrome(st);
    let dragging = st.drag_index >= 0;
    if st.screen_dirty.is_empty() && !dragging {
        return;
    }
    /* 下地を描き替えるので、まずカーソルを外して下地を戻す。 */
    cursor::hide(st);

    let n = st.screen_dirty.len;
    let fr = fep::rect();
    let mut fep_hit = false;
    let mut i = 0;
    while i < n {
        let r = st.screen_dirty.rects[i];
        composite_rect(st, r);
        queue_present(st, r);
        if !fr.is_empty() && fr.intersects(&r) {
            fep_hit = true;
        }
        i += 1;
    }
    st.screen_dirty.clear();
    /* FEP の未確定行 / 候補窓は最前面。下地を塗り替えたら描き直す。 */
    if fep_hit {
        fep::redraw_now(st);
        queue_present(st, fr);
    }

    /* ドラッグ中なら枠を再描画 (合成で消えているため)。枠は最前面物の下
     * (掛かった最前面物は描き直す。COMMIT の側と同じ重なり順)。 */
    if dragging {
        let f = st.drag_frame;
        crate::input::draw_live_frame(st, &[]);
        queue_present(st, f);
    }

    cursor::show(st);
    let cr = cursor::rect(st);
    queue_present(st, cr);
    flush_present();
}

/* ================================================================ */
/*  WM の周期 (契約 X3 / 単独ループ)                                 */
/* ================================================================ */

/// WM の 1 周: 入力取り込み → パレットのリース調停 → FEP の配置 →
/// クローム/デスクトップの present → FEP の描画。
/// 文脈は [`input::Ctx`] (X3 = Wait / Standalone、X4 = Pump)。
///
/// 順序が肝: FEP の配置決め ([`fep::pre_cycle`]) を **`flush_screen_dirty` の前**に
/// 置き、旧 ∪ 新の領域を先に損傷として積む。こうすると同じ周で下地が塗り直され、
/// その上に FEP が乗る (逆順にすると「描く → 次の周で消される → また描く」で
/// ちらつく)。
pub fn wm_cycle(st: &mut GuiState, ctx: input::Ctx) {
    input::capture(st, ctx);
    if ctx != input::Ctx::Pump {
        /* モーダルの X3 分 (W4): 保留していた VFS 走査と、sticky な
         * `GUI_EV_MODAL` の再配送 (リングに空きができるまで諦めない)。 */
        modal::x3_cycle(st);
        /* Start メニューの `/usr/bin` 走査 (契約 D2: X3 で 1 回だけ)。 */
        startmenu::x3_cycle(st);
        /* sticky な `GUI_EV_QUIT` の再配送 (契約 S5)。 */
        session::x3_cycle(st);
        /* T2g: PARKED な裏の slot へ。KAPI は X3 / 単独ループだけ (T8)。 */
        crate::trim::deliver(st);
        /* 時計 (1 秒粒度) と窓ボタンの変化 (契約 D1 / D3)。 */
        taskbar::x3_cycle(st);
        lease::reconcile(st);
        fep::pre_cycle(st);
        flush_screen_dirty(st);
        fep::post_cycle(st);
        /* 音はフォーカスに追従して排他 (決裁 D9-4、受入 G10)。KAPI を呼ぶので
         * X1 では行わない (契約 T8) — X3 と単独ループの周期だけ。フォーカスが
         * 動いていなければ何もしない。 */
        crate::multiapp::sync_snd_focus(st);
    }
}

/* ================================================================ */
/*  フォーカス (契約 U1 の set_focus)                                */
/* ================================================================ */

/// ウィンドウを最前面へ出してフォーカスを移す。`Focus` イベントを両者へ流す。
pub fn set_focus(st: &mut GuiState, owner: i32, id: u32) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    if st.front_index() == Some(index) {
        return 0;
    }
    /* WM が最小化した窓へのアプリの set_focus は何もしない (Win98 の SetFocus も
     * 復元しない)。前へ出すと、不可視の窓が Z の最前面に居るのに打鍵は可視の
     * 窓へ行き、Focus だけが食い違う (代行レビュー P2-2)。 */
    if st.windows[index].minimized {
        return 0;
    }
    /* 未確定文字は元の窓へ確定する (票 KBD_NAV §1-6)。ここは X1 なので
     * 変換 (辞書) は走らせず、次の X3 の頭で元の窓へ流す。 */
    focus_leaving(st, false);
    let old_id = st.front_id();
    st.bring_to_front(index);
    visible::recompute_and_expose(st);
    let outer = st.windows[index].outer();
    st.dirty_screen(outer);
    let new_id = st.windows[index].id(index);
    input::emit_focus_change(st, old_id, new_id);
    0
}

/// フォーカスが別の窓へ移る**直前**に呼ぶ (票 KBD_NAV §1-6)。FEP の未確定文字を
/// いまのフォーカス窓へ確定する。`x3` = 変換を走らせてよい文脈 (WM の UI)。
/// X1 (アプリの要求) では予約だけして、次の X3 の頭で同じ窓へ流す。
pub fn focus_leaving(st: &mut GuiState, x3: bool) {
    let old = st.front_id();
    if old == 0 {
        return;
    }
    if x3 {
        fep::commit_to(st, old);
    } else {
        fep::defer_commit(old);
    }
}

/// WM 自身がフォーカスを移す共通経路 (X3。マウス・タスクバー・キー操作)。
/// FEP の確定 → 最前面 → 露出 → `Focus`。既に最前面なら何もしない。
pub fn activate_index(st: &mut GuiState, index: usize) {
    if st.windows[index].minimized {
        restore_minimized(st, index);
        return;
    }
    if st.front_index() == Some(index) {
        return;
    }
    focus_leaving(st, true);
    let old_front = st.front_id();
    st.bring_to_front(index);
    visible::recompute_and_expose(st);
    let outer = st.windows[index].outer();
    st.dirty_screen(outer);
    let new_front = st.windows[index].id(index);
    input::emit_focus_change(st, old_front, new_front);
}

/* ================================================================ */
/*  最小化 / 最大化 / 元のサイズ (票 KBD_NAV §1-2 の窓メニュー、X3)  */
/* ================================================================ */

/// 最小化: 画面から外してタスクバーにだけ残す。MRU の末尾へ回す。
pub fn minimize(st: &mut GuiState, index: usize) {
    let w = st.windows[index];
    if !w.used || w.minimized || !w.visible {
        return;
    }
    let was_front = st.front_index() == Some(index);
    if was_front {
        focus_leaving(st, true);
    }
    let old_front = st.front_id();
    st.windows[index].minimized = true;
    st.windows[index].visible = false;
    st.dirty_screen(w.outer());
    visible::recompute_and_expose(st);
    crate::kbdnav::mru_to_end(st, w.id(index));
    let new_front = st.front_id();
    if new_front != old_front {
        input::emit_focus_change(st, old_front, new_front);
    }
}

/// 最小化を解いて最前面へ出し、フォーカスを移す (`Focus` は 1 回)。
pub fn restore_minimized(st: &mut GuiState, index: usize) {
    if !st.windows[index].used || !st.windows[index].minimized {
        return;
    }
    focus_leaving(st, true);
    let old_front = st.front_id();
    st.windows[index].minimized = false;
    st.windows[index].visible = true;
    damage::set_dirty_full(&mut st.windows[index]);
    st.bring_to_front(index);
    visible::recompute_and_expose(st);
    let outer = st.windows[index].outer();
    st.dirty_screen(outer);
    let new_front = st.windows[index].id(index);
    if new_front != old_front {
        input::emit_focus_change(st, old_front, new_front);
    }
}

/// 外形を WM が決めた矩形へ置き換える (最大化 / 元のサイズ / キーボードの
/// 移動・サイズ)。旧 ∪ 新を損傷にし、クライアントを全面再描画させて
/// `Configure` を流す (マウスのドラッグ確定と同じ手順)。
pub fn set_outer(st: &mut GuiState, index: usize, r: Rect) {
    let old = st.windows[index].outer();
    if old == r {
        return;
    }
    st.windows[index].x = r.x;
    st.windows[index].y = r.y;
    st.windows[index].w = r.w;
    st.windows[index].h = r.h;
    st.dirty_screen(old);
    st.dirty_screen(r);
    visible::recompute_and_expose(st);
    damage::set_dirty_full(&mut st.windows[index]);
    st.windows[index].configure_pending = true;
    input::emit_configure(st, index);
}

/// 最大化: 作業領域いっぱいにする。元の外形は `restore` に控える。
pub fn maximize(st: &mut GuiState, index: usize) {
    if st.windows[index].maximized {
        return;
    }
    let old = st.windows[index].outer();
    st.windows[index].restore = old;
    st.windows[index].maximized = true;
    let wa = work_area(st);
    set_outer(st, index, wa);
}

/// 元のサイズ: 最大化を解く (最小化なら元に戻す)。
pub fn restore_size(st: &mut GuiState, index: usize) {
    if st.windows[index].minimized {
        restore_minimized(st, index);
        return;
    }
    if !st.windows[index].maximized {
        return;
    }
    st.windows[index].maximized = false;
    let r = st.windows[index].restore;
    set_outer(st, index, r);
}

/// 窓の大きさの下限 (`resize_window` と同じ規則)。
pub fn min_size(w: &Win) -> (i32, i32) {
    let mut mw = w.min_w;
    let mut mh = w.min_h;
    if mw < 60 {
        mw = 60;
    }
    if mh < TITLEBAR_H + 8 {
        mh = TITLEBAR_H + 8;
    }
    (mw, mh)
}

/* ================================================================ */
/*  ウィンドウ操作 (契約 U1)。owner 検証つき。                        */
/*  返り値: 成功 = id (>=0) もしくは 0、失敗 = OS32_ERR_*。            */
/* ================================================================ */

/// id を owner が操作してよいか検証して index を返す。
fn resolve_owned(st: &GuiState, owner: i32, id: u32) -> Result<usize, i32> {
    match st.win_by_id(id) {
        None => Err(OS32_ERR_STALE),
        Some(i) => {
            if st.windows[i].owner != owner {
                Err(OS32_ERR_INVAL)
            } else {
                Ok(i)
            }
        }
    }
}

/// create_window。返り値: 完全な WindowId (i32 として非負) / OS32_ERR_FULL。
pub fn create_window(st: &mut GuiState, owner: i32, spec: &GuiWinSpec) -> i32 {
    let index = match st.alloc_win() {
        Some(i) => i,
        None => return OS32_ERR_FULL,
    };
    let gen = {
        let g = st.windows[index].gen;
        if g == 0 {
            1
        } else {
            g
        }
    };
    let mut w = Win::EMPTY;
    w.used = true;
    w.gen = gen;
    w.owner = owner;
    let mut ww = spec.rect.w as i32;
    let mut wh = spec.rect.h as i32;
    if ww < 60 {
        ww = 60;
    }
    if wh < TITLEBAR_H + 8 {
        wh = TITLEBAR_H + 8;
    }
    /* 新規配置は作業領域 (画面 − タスクバー) へクランプする (契約 D1)。 */
    let (nx, ny) = clamp_to_work_area(st, spec.rect.x as i32, spec.rect.y as i32, ww, wh);
    w.x = nx;
    w.y = ny;
    w.w = ww;
    w.h = wh;
    w.flags = if spec.flags == 0 {
        os32api::gui::proto::GUI_WF_DEFAULT
    } else {
        spec.flags
    };
    w.min_w = spec.min_w as i32;
    w.min_h = spec.min_h as i32;
    /* title (40B 固定、NUL 終端を保証)。 */
    let mut t = 0;
    while t < 39 {
        w.title[t] = spec.title[t];
        if spec.title[t] == 0 {
            break;
        }
        t += 1;
    }
    w.title[39] = 0;
    w.visible = wf_visible(w.flags);
    w.configure_pending = true;
    st.windows[index] = w;
    if w.visible {
        focus_leaving(st, false); /* X1 (票 KBD_NAV §1-6) */
    }
    let old_front = st.front_id();
    st.z_add_top(index);
    /* 初回は全面 dirty + 露出計算。 */
    damage::set_dirty_full(&mut st.windows[index]);
    visible::recompute_and_expose(st);
    st.dirty_screen(st.windows[index].outer());
    /* 可視で生成された窓は最前面 = フォーカス。旧 front に out、新窓に in を
     * 流す (C2 の観察: 窓 1 枚のアプリが Focus{in} を一度も受け取れなかった)。 */
    let new_front = st.front_id();
    if new_front != old_front {
        input::emit_focus_change(st, old_front, new_front);
    }
    /* generation は 1〜0x7FFF に制限してあるので i32 として必ず非負。 */
    st.windows[index].id(index) as i32
}

pub fn destroy_window(st: &mut GuiState, owner: i32, id: u32) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    lease::release_window(st, index);
    let vac = st.windows[index].outer();
    st.z_remove(index);
    let gen = next_gen(st.windows[index].gen);
    st.windows[index] = Win::EMPTY;
    st.windows[index].gen = gen;
    if st.drag_index == index as i32 {
        drop_drag_frame(st);
    }
    st.dirty_screen(vac);
    visible::recompute_and_expose(st);
    0
}

pub fn move_window(st: &mut GuiState, owner: i32, id: u32, x: i32, y: i32) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    let old = st.windows[index].outer();
    /* アプリが動かしたら最大化ではない (元のサイズの記憶は捨てる)。 */
    st.windows[index].maximized = false;
    st.windows[index].x = x;
    st.windows[index].y = y;
    st.dirty_screen(old);
    st.dirty_screen(st.windows[index].outer());
    damage::set_dirty_full(&mut st.windows[index]);
    st.windows[index].configure_pending = true;
    visible::recompute_and_expose(st);
    0
}

pub fn resize_window(st: &mut GuiState, owner: i32, id: u32, w: i32, h: i32) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    let old = st.windows[index].outer();
    let mut nw = w;
    let mut nh = h;
    if nw < st.windows[index].min_w {
        nw = st.windows[index].min_w;
    }
    if nh < st.windows[index].min_h {
        nh = st.windows[index].min_h;
    }
    if nw < 60 {
        nw = 60;
    }
    if nh < TITLEBAR_H + 8 {
        nh = TITLEBAR_H + 8;
    }
    st.windows[index].maximized = false;
    st.windows[index].w = nw;
    st.windows[index].h = nh;
    st.dirty_screen(old);
    st.dirty_screen(st.windows[index].outer());
    damage::set_dirty_full(&mut st.windows[index]);
    st.windows[index].configure_pending = true;
    visible::recompute_and_expose(st);
    0
}

pub fn show_window(st: &mut GuiState, owner: i32, id: u32, show: bool) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    let outer = st.windows[index].outer();
    if show && !st.windows[index].visible {
        focus_leaving(st, false); /* X1 (票 KBD_NAV §1-6) */
    }
    let old_front = st.front_id();
    /* アプリの show / hide は WM の最小化を解く (hide ならタスクバーからも消える)。 */
    st.windows[index].minimized = false;
    st.windows[index].visible = show;
    if show {
        st.windows[index].flags |= GUI_WF_VISIBLE;
        damage::set_dirty_full(&mut st.windows[index]);
    } else {
        st.windows[index].flags &= !GUI_WF_VISIBLE;
    }
    st.dirty_screen(outer);
    visible::recompute_and_expose(st);
    /* フォーカス = 最前面の可視窓。hide / show で変わったら Focus を流す。 */
    let new_front = st.front_id();
    if new_front != old_front {
        input::emit_focus_change(st, old_front, new_front);
    }
    0
}

pub fn set_title(st: &mut GuiState, owner: i32, id: u32, title: &[u8], len: usize) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    let n = if len > 39 { 39 } else { len };
    let mut i = 0;
    while i < 40 {
        st.windows[index].title[i] = 0;
        i += 1;
    }
    let mut k = 0;
    while k < n {
        st.windows[index].title[k] = title[k];
        k += 1;
    }
    st.windows[index].title[39] = 0;
    /* タイトルバーだけ再描画。 */
    st.dirty_screen(st.windows[index].titlebar_rect());
    0
}

pub fn raise(st: &mut GuiState, owner: i32, id: u32) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    if st.front_index() != Some(index) {
        st.bring_to_front(index);
        st.dirty_screen(st.windows[index].outer());
        visible::recompute_and_expose(st);
    }
    0
}

pub fn client_rect(st: &GuiState, owner: i32, id: u32) -> Result<Rect, i32> {
    let index = resolve_owned(st, owner, id)?;
    let (cox, coy) = st.windows[index].client_origin();
    let (cw, ch) = st.windows[index].client_size();
    Ok(Rect::new(cox, coy, cw, ch))
}

pub fn set_text_cursor(
    st: &mut GuiState,
    owner: i32,
    id: u32,
    x: i32,
    y: i32,
    visible_flag: bool,
) -> i32 {
    let index = match resolve_owned(st, owner, id) {
        Ok(i) => i,
        Err(e) => return e,
    };
    st.windows[index].tc_x = x;
    st.windows[index].tc_y = y;
    st.windows[index].tc_visible = visible_flag;
    /* FEP の未確定文字列 / 候補窓はこの位置に出る (契約 U2a、W2)。
     * 出ている最中に動いたら配置し直す (描画は X3)。 */
    if !fep::rect().is_empty() && st.front_index() == Some(index) {
        fep::mark_redraw();
    }
    0
}

/* ================================================================ */
/*  パレット / 画面情報 (起動時)                                     */
/* ================================================================ */

/// G6 のシステムパレットを gfx_set_palette で入れる (契約 G6)。
pub fn install_system_palette() {
    let a = unsafe { os32api::api() };
    let mut i = 0;
    while i < 16 {
        let c = os32api::gui::proto::GUI_SYSTEM_PALETTE[i];
        unsafe {
            (a.gfx_set_palette)(i as i32, c.r, c.g, c.b);
        }
        i += 1;
    }
}

/// gfx_screen_info を 1 回読んで画面寸法を控える (契約 G5、決め打ち禁止)。
pub fn read_screen_info(st: &mut GuiState) {
    #[repr(C)]
    #[derive(Clone, Copy)]
    struct ScreenInfo {
        width: u16,
        height: u16,
        bpp: u8,
        format: u8,
        flags: u32,
        lease_mask: u16,
        lease_first: u16,
        lease_count: u16,
        reserved: [u16; 5],
    }
    let mut si = ScreenInfo {
        width: 640,
        height: 400,
        bpp: 4,
        format: 0,
        flags: 0,
        lease_mask: 0,
        lease_first: 0,
        lease_count: 0,
        reserved: [0; 5],
    };
    unsafe {
        (os32api::api().gfx_screen_info)(&mut si as *mut ScreenInfo as *mut u8);
    }
    if si.width > 0 && si.height > 0 {
        st.screen_w = si.width as i32;
        st.screen_h = si.height as i32;
    }
    /* パレットのリースで貸せる範囲 (契約 G8 / G5)。決め打ちしない。 */
    st.cap_lease_mask = si.lease_mask;
    st.cap_lease_first = si.lease_first;
    st.cap_lease_count = si.lease_count;
}

/* ================================================================ */
/*  パレット全体の退避・復元 (フルスクリーン GFX プログラムの前後)   */
/*  契約 G6 / G8: WM が前後で退避・復元する。                        */
/* ================================================================ */

/// いまの 16 色を読み出す (`gfx_get_palette`)。
pub fn save_palette() -> [u8; 48] {
    let a = unsafe { os32api::api() };
    let mut out = [0u8; 48];
    let mut i = 0;
    while i < 16 {
        let mut r: u8 = 0;
        let mut g: u8 = 0;
        let mut b: u8 = 0;
        unsafe {
            (a.gfx_get_palette)(i as i32, &mut r, &mut g, &mut b);
        }
        out[i * 3] = r;
        out[i * 3 + 1] = g;
        out[i * 3 + 2] = b;
        i += 1;
    }
    out
}

/// [`save_palette`] で控えた 16 色を戻す。
pub fn restore_palette(p: &[u8; 48]) {
    let a = unsafe { os32api::api() };
    let mut i = 0;
    while i < 16 {
        unsafe {
            (a.gfx_set_palette)(i as i32, p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        }
        i += 1;
    }
}

/* 合成器のホスト試験 (`host/integration.py` でだけ組む)。 */
#[cfg(test)]
#[path = "../host/wm_composite_tests.rs"]
mod wm_composite_tests;
