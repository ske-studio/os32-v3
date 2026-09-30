//! GUI 共有プロトコル定数 (Rust 側の写し) — 契約 T5 / TASKS §4。
//!
//! **正典は C 側の `sdk/include/os32/os32_gui_shared.h`** と数表
//! `docs/tasks/gui/PROTO_LAYOUT.md`。このファイルはその値を 1:1 で写したもので、
//! PM の `tools/check_gui_proto.py` が C ヘッダ / この `proto.rs` / PROTO_LAYOUT の
//! 三者を突き合わせる。**数値を動かすときは 3 か所同時に。以後は末尾追記のみ。**
//!
//! ここに描画ロジックは置かない (値と `#[repr(C)]` レイアウトだけ)。描画は
//! `userland/rust/libos32gui`、SHM/イベントの読み書きは W/C の各実装が持つ。
#![allow(dead_code)]
#![allow(non_upper_case_globals)]

use core::mem::{offset_of, size_of};

/* ======================================================================== */
/*  プロトコルバージョン                                                     */
/* ======================================================================== */
pub const GUI_PROTO_VERSION: u16 = 1;
/// この proto.rs が対応する KAPI 版 (PROTO_LAYOUT.md「バージョン」)。
pub const GUI_KAPI_VERSION: u32 = 41;

/* ======================================================================== */
/*  op 番号 (gui_call の第1引数) — PROTO_LAYOUT「op 番号」                    */
/* ======================================================================== */
pub const GUI_OP_NONE: u32 = 0;
pub const GUI_OP_INIT: u32 = 1;
pub const GUI_OP_POLL: u32 = 2;
pub const GUI_OP_WAIT: u32 = 3;
pub const GUI_OP_COMMIT: u32 = 4;
pub const GUI_OP_INVALIDATE: u32 = 5;
pub const GUI_OP_STATS: u32 = 6;
pub const GUI_OP_LEASE_PALETTE: u32 = 7;

pub const GUI_OP_WIN_CREATE: u32 = 16;
pub const GUI_OP_WIN_DESTROY: u32 = 17;
pub const GUI_OP_WIN_MOVE: u32 = 18;
pub const GUI_OP_WIN_RESIZE: u32 = 19;
pub const GUI_OP_WIN_SHOW: u32 = 20;
pub const GUI_OP_WIN_SET_TITLE: u32 = 21;
pub const GUI_OP_WIN_CLIENT_RECT: u32 = 22;
pub const GUI_OP_WIN_RAISE: u32 = 23;
pub const GUI_OP_WIN_SET_FOCUS: u32 = 24;
pub const GUI_OP_WIN_SET_TEXT_CURSOR: u32 = 25;

pub const GUI_OP_SURF_CREATE: u32 = 32;
pub const GUI_OP_SURF_DESTROY: u32 = 33;

pub const GUI_OP_TIMER_SET: u32 = 48;
pub const GUI_OP_TIMER_KILL: u32 = 49;

pub const GUI_OP_MODAL_OPEN: u32 = 64;
/// 完了したモーダルの結果取得 (v1.2 M1)。67〜79 は予約。
pub const GUI_OP_MODAL_RESULT: u32 = 65;
/// gshell へのセッション要求 (v1.2 S4)。古い WM は `OS32_ERR_NOSYS` を返す。
pub const GUI_OP_SESSION_REQUEST: u32 = 66;

/// カーネル内部 op (exec_exit → WM)。アプリは送らない。
pub const GUI_OP_OWNER_EXIT: u32 = 80;

/* ======================================================================== */
/*  イベント種別 (GuiEvent.kind) — U2 の並び順どおり 1 から                  */
/* ======================================================================== */
pub const GUI_EV_NONE: u8 = 0;
pub const GUI_EV_PAINT: u8 = 1;
pub const GUI_EV_CONFIGURE: u8 = 2;
pub const GUI_EV_CLOSE: u8 = 3;
pub const GUI_EV_FOCUS: u8 = 4;
pub const GUI_EV_KEY: u8 = 5;
pub const GUI_EV_TEXT: u8 = 6;
pub const GUI_EV_POINTER: u8 = 7;
pub const GUI_EV_BUTTON: u8 = 8;
pub const GUI_EV_TIMER: u8 = 9;
pub const GUI_EV_WIDGET: u8 = 10;
pub const GUI_EV_MODAL: u8 = 11;
pub const GUI_EV_QUIT: u8 = 12;
pub const GUI_EV_PALETTE: u8 = 13;

/* ======================================================================== */
/*  Style.flags (u8) — G6                                                    */
/* ======================================================================== */
pub const GUI_STYLE_TRANSPARENT_BG: u8 = 0x01;
pub const GUI_STYLE_XOR: u8 = 0x02;
pub const GUI_STYLE_DOTTED: u8 = 0x04;
pub const GUI_STYLE_DITHER50: u8 = 0x08;

/* ウィンドウフラグ GUI_WF_* (u16) — 契約 U1 / os32_gui_shared.h と同値 (レビュー ③) */
pub const GUI_WF_VISIBLE: u16 = 0x0001;
pub const GUI_WF_HAS_CLOSE: u16 = 0x0002;
pub const GUI_WF_MOVABLE: u16 = 0x0004;
pub const GUI_WF_BORDER: u16 = 0x0008;
pub const GUI_WF_DEFAULT: u16 =
    GUI_WF_VISIBLE | GUI_WF_HAS_CLOSE | GUI_WF_MOVABLE | GUI_WF_BORDER;

/* ======================================================================== */
/*  システム色 (16 色固定、役割名で参照) — G6                                */
/* ======================================================================== */
pub const GUI_COLOR_TEXT: u8 = 0;
pub const GUI_COLOR_TITLE_ACTIVE: u8 = 1;
pub const GUI_COLOR_SEL_BG: u8 = 1;
pub const GUI_COLOR_SHADOW: u8 = 2;
pub const GUI_COLOR_DISABLED: u8 = 3;
pub const GUI_COLOR_OK: u8 = 4;
pub const GUI_COLOR_WARN: u8 = 5;
pub const GUI_COLOR_FACE: u8 = 6;
pub const GUI_COLOR_TITLE_INACTIVE: u8 = 6;
pub const GUI_COLOR_WINDOW: u8 = 7;
pub const GUI_COLOR_TITLE_TEXT: u8 = 7;
pub const GUI_COLOR_CLOSE: u8 = 8;
pub const GUI_COLOR_ALERT: u8 = 8;
pub const GUI_COLOR_LINK: u8 = 9;
pub const GUI_COLOR_ACCENT: u8 = 10;
pub const GUI_COLOR_LIGHT: u8 = 11;
pub const GUI_COLOR_DESKTOP: u8 = 12;
pub const GUI_COLOR_HIGHLIGHT: u8 = 13;
pub const GUI_COLOR_SEL_TEXT: u8 = 14;
pub const GUI_COLOR_EDIT_BG: u8 = 15;

/// RGB 各成分 0〜15 (PC-98 16 色)。C 側 `GuiRgb` と同一レイアウト。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

/// システムパレット (GUI_COLOR_* の並びと一致) — G6 / PROTO_LAYOUT の色表。
pub static GUI_SYSTEM_PALETTE: [GuiRgb; 16] = [
    GuiRgb { r: 0, g: 0, b: 0 },    /*  0 TEXT           */
    GuiRgb { r: 0, g: 0, b: 8 },    /*  1 TITLE_ACTIVE   */
    GuiRgb { r: 6, g: 6, b: 6 },    /*  2 SHADOW         */
    GuiRgb { r: 9, g: 9, b: 9 },    /*  3 DISABLED       */
    GuiRgb { r: 0, g: 10, b: 0 },   /*  4 OK             */
    GuiRgb { r: 14, g: 12, b: 0 },  /*  5 WARN           */
    GuiRgb { r: 12, g: 12, b: 12 }, /*  6 FACE           */
    GuiRgb { r: 15, g: 15, b: 15 }, /*  7 WINDOW         */
    GuiRgb { r: 12, g: 0, b: 0 },   /*  8 CLOSE          */
    GuiRgb { r: 0, g: 10, b: 14 },  /*  9 LINK           */
    GuiRgb { r: 12, g: 0, b: 12 },  /* 10 ACCENT         */
    GuiRgb { r: 14, g: 14, b: 14 }, /* 11 LIGHT          */
    GuiRgb { r: 0, g: 8, b: 10 },   /* 12 DESKTOP        */
    GuiRgb { r: 0, g: 0, b: 15 },   /* 13 HIGHLIGHT      */
    GuiRgb { r: 15, g: 15, b: 15 }, /* 14 SEL_TEXT       */
    GuiRgb { r: 15, g: 15, b: 14 }, /* 15 EDIT_BG        */
];

/* ======================================================================== */
/*  上限 (P 性能規約 / U6)                                                    */
/* ======================================================================== */
pub const GUI_MAX_WINDOWS: usize = 16;
pub const GUI_MAX_SURFACES: usize = 16;
pub const GUI_MAX_WIDGETS: usize = 64;
pub const GUI_MAX_LIST_ITEMS: usize = 128;
pub const GUI_MAX_TIMERS: usize = 8;
pub const GUI_MAX_CLIP_DEPTH: usize = 8;
pub const GUI_MAX_DAMAGE: usize = 8;
pub const GUI_MAX_STRING: usize = 256;

/* 複数行編集ウィジェット WK_TEXTAREA (票 TASK_EDIT_GUI §2、決裁 A1)。
 * **部品は本文を持たない** — アプリが本文を持ち、見えている行だけを写す。 */
/// 見える行のプール (全 textarea で共有)。
pub const GUI_MAX_TEXTAREA_ROWS: usize = 32;
/// 見える 1 行のバイト数 (80 桁 = 全角 40 字 * 3B + 余白)。
pub const GUI_TEXTAREA_ROW_CAP: usize = 192;
/// 溜めておける確定文字列 (`GuiEvtText.utf8` と同じ)。
pub const GUI_TEXTAREA_INPUT_CAP: usize = 8;

/* ======================================================================== */
/*  ヘッダフラグ (GuiSlotHeader.flags)                                        */
/* ======================================================================== */
pub const GUI_HDR_FLAG_OVERFLOW: u16 = 0x0001;

/* ======================================================================== */
/*  エラー番号 (os32_kapi_shared.h / PROTO_LAYOUT)                            */
/* ======================================================================== */
pub const OS32_ERR_IO: i32 = -1;
pub const OS32_ERR_NOTFOUND: i32 = -2;
pub const OS32_ERR_INVAL: i32 = -9; /* = ERR_ARG */
pub const OS32_ERR_NOSYS: i32 = -10;
pub const OS32_ERR_STALE: i32 = -11;
pub const OS32_ERR_VERSION: i32 = -12;
pub const OS32_ERR_FULL: i32 = -13;
/// 「いまは無い / 後でもう一度」。`exec_resume` が鍵待ちのアプリを注入リング
/// が空のまま起こそうとしたときだけ返す (票 K7 §5 指摘 B、KAPI v47)。
pub const OS32_ERR_AGAIN: i32 = -14;

/* ======================================================================== */
/*  スロット内レイアウト (1 スロット = 16KB) — 契約 T2 / PROTO_LAYOUT         */
/* ======================================================================== */
pub const GUI_SLOT_SIZE: usize = 0x4000; /* 16KB (memmap.h) */
pub const GUI_SLOT_MAX: usize = 4; /* スロット 0〜3 */

pub const GUI_SLOT_HDR_OFF: usize = 0;
pub const GUI_SLOT_REQ_OFF: usize = 16;
pub const GUI_SLOT_REQ_SIZE: usize = 512;
pub const GUI_SLOT_RESP_OFF: usize = 528;
pub const GUI_SLOT_RESP_SIZE: usize = 512;
pub const GUI_SLOT_RING_OFF: usize = 1040;
pub const GUI_RING_CAPACITY: usize = 128;
pub const GUI_SLOT_RING_SIZE: usize = GUI_RING_CAPACITY * 16; /* 2048 */
pub const GUI_SLOT_ARGS_OFF: usize = 3088;
pub const GUI_SLOT_ARGS_SIZE: usize = 8192;

/* GUI 予約 SHM の位置 = kapi.shm_base + GUI_SHM_OFFSET + slot × GUI_SLOT_SIZE。
 * 正典は memmap.h (MEM_SHM_GUI_BASE) / C の os32_gui_shared.h (GUI_SHM_OFFSET)。
 * gshell と libos32gui の二重定義を解消するためここに写す (2026-09-06)。 */
pub const GUI_SHM_OFFSET: u32 = 0x28000;

/* ======================================================================== */
/*  基本図形 — 座標は描画先サーフェスのローカル (G1)                          */
/* ======================================================================== */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRect16 {
    pub x: i16,
    pub y: i16,
    pub w: i16,
    pub h: i16,
}

/* ======================================================================== */
/*  スロットヘッダ (16B) — 契約 T2 / PROTO_LAYOUT                             */
/*  ring_head はアプリ (消費者)、ring_tail は WM (生産者) だけが書く。         */
/* ======================================================================== */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiSlotHeader {
    pub proto_version: u16, /* @0 */
    pub flags: u16,         /* @2  GUI_HDR_FLAG_* */
    pub seq: u32,           /* @4 */
    pub ring_head: u16,     /* @8  アプリが書く (消費) */
    pub ring_tail: u16,     /* @10 WM が書く (生産) */
    pub dropped: u16,       /* @12 取りこぼし累計差分 */
    pub reserved: u16,      /* @14 */
}

/* ======================================================================== */
/*  イベント (16B 固定) — 契約 U2 / PROTO_LAYOUT                              */
/*                                                                          */
/*  C 側は payload を union で持つが、Rust では [u8;8] の生ペイロードとして    */
/*  持ち、型付きペイロード構造体 (下) で読み出す。数値レイアウトは同一。       */
/* ======================================================================== */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiEvent {
    pub kind: u8,         /* @0  GUI_EV_* */
    pub sub: u8,          /* @1  種別ごとの小さな値 */
    pub serial: u16,      /* @2  入力系のみ。他は 0 */
    pub window: u32,      /* @4  index:16 | generation:16 */
    pub payload: [u8; 8], /* @8 */
}

/* 型付きペイロード (payload の中身。相対オフセットが C 側 union と一致する) */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiEvtKey {
    pub scan: u8, /* +0 */
    pub ch: u8,   /* +1 */
    pub mods: u8, /* +2 */
    pub _pad: [u8; 5],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiEvtText {
    pub utf8: [u8; 8], /* +0 */
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiEvtPointer {
    pub x: i16, /* +0 */
    pub y: i16, /* +2 */
    pub _pad: [u8; 4],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiEvtButton {
    pub x: i16,     /* +0 */
    pub y: i16,     /* +2 */
    pub button: u8, /* +4 */
    pub _pad: [u8; 3],
}
#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct GuiEvtWidget {
    pub widget: u16, /* +0 */
    pub value: i32,  /* +2 (非整列) */
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiEvtModal {
    pub dialog: u16, /* +0 */
    pub result: i16, /* +2 */
    pub _pad: [u8; 4],
}

impl GuiEvent {
    /// Paint / Configure のペイロード矩形。
    #[inline]
    pub fn rect(&self) -> GuiRect16 {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiRect16) }
    }
    #[inline]
    pub fn key(&self) -> GuiEvtKey {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiEvtKey) }
    }
    #[inline]
    pub fn text(&self) -> GuiEvtText {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiEvtText) }
    }
    #[inline]
    pub fn pointer(&self) -> GuiEvtPointer {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiEvtPointer) }
    }
    #[inline]
    pub fn button(&self) -> GuiEvtButton {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiEvtButton) }
    }
    #[inline]
    pub fn widget(&self) -> GuiEvtWidget {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiEvtWidget) }
    }
    #[inline]
    pub fn modal(&self) -> GuiEvtModal {
        unsafe { core::ptr::read_unaligned(self.payload.as_ptr() as *const GuiEvtModal) }
    }
}

/* ======================================================================== */
/*  要求構造体 (末尾追記のみ) — 契約 T2                                       */
/* ======================================================================== */

/// 長さ前置文字列 (最大 255 バイト)。UTF-8 境界で切り詰め。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiString {
    pub len: u8,
    pub s: [u8; 255],
}

/// create_window の spec (U1)。title は 40B 固定。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiWinSpec {
    pub title: [u8; 40], /* @0 */
    pub rect: GuiRect16, /* @40 */
    pub flags: u16,      /* @48 */
    pub min_w: i16,      /* @50 */
    pub min_h: i16,      /* @52 */
}

/* ======================================================================== */
/*  静的表明 — C 側 os32_gui_shared.h の STATIC_ASSERT と 1:1                 */
/* ======================================================================== */

const _: () = assert!(size_of::<GuiRect16>() == 8);

const _: () = assert!(size_of::<GuiSlotHeader>() == 16);
const _: () = assert!(offset_of!(GuiSlotHeader, proto_version) == 0);
const _: () = assert!(offset_of!(GuiSlotHeader, flags) == 2);
const _: () = assert!(offset_of!(GuiSlotHeader, seq) == 4);
const _: () = assert!(offset_of!(GuiSlotHeader, ring_head) == 8);
const _: () = assert!(offset_of!(GuiSlotHeader, ring_tail) == 10);
const _: () = assert!(offset_of!(GuiSlotHeader, dropped) == 12);

const _: () = assert!(size_of::<GuiEvtWidget>() == 6);
const _: () = assert!(size_of::<GuiEvent>() == 16);
const _: () = assert!(offset_of!(GuiEvent, kind) == 0);
const _: () = assert!(offset_of!(GuiEvent, sub) == 1);
const _: () = assert!(offset_of!(GuiEvent, serial) == 2);
const _: () = assert!(offset_of!(GuiEvent, window) == 4);
const _: () = assert!(offset_of!(GuiEvent, payload) == 8);
/* ペイロードの各フィールドの絶対オフセット (U2 の表) */
const _: () = assert!(offset_of!(GuiEvent, payload) + offset_of!(GuiEvtWidget, value) == 10);
const _: () = assert!(offset_of!(GuiEvent, payload) + offset_of!(GuiEvtButton, button) == 12);
const _: () = assert!(offset_of!(GuiEvent, payload) + offset_of!(GuiEvtKey, mods) == 10);
const _: () = assert!(offset_of!(GuiEvent, payload) + offset_of!(GuiEvtPointer, y) == 10);
const _: () = assert!(offset_of!(GuiEvent, payload) + offset_of!(GuiEvtModal, result) == 10);

/* スロット内オフセットの連鎖と収まり */
const _: () = assert!(GUI_SLOT_REQ_OFF == GUI_SLOT_HDR_OFF + 16);
const _: () = assert!(GUI_SLOT_RESP_OFF == GUI_SLOT_REQ_OFF + GUI_SLOT_REQ_SIZE);
const _: () = assert!(GUI_SLOT_RING_OFF == GUI_SLOT_RESP_OFF + GUI_SLOT_RESP_SIZE);
const _: () = assert!(GUI_SLOT_ARGS_OFF == GUI_SLOT_RING_OFF + GUI_SLOT_RING_SIZE);
const _: () = assert!(GUI_SLOT_ARGS_OFF + GUI_SLOT_ARGS_SIZE <= GUI_SLOT_SIZE);
const _: () = assert!(GUI_SLOT_RING_SIZE == 2048);

const _: () = assert!(size_of::<GuiString>() == 256);
const _: () = assert!(offset_of!(GuiWinSpec, rect) == 40);
const _: () = assert!(offset_of!(GuiWinSpec, flags) == 48);
const _: () = assert!(size_of::<GuiWinSpec>() <= GUI_SLOT_REQ_SIZE);
/* パレットの要素数は型 [GuiRgb; 16] が保証する (静的 static は const 文脈から参照
 * できないため .len() の assert は置かない)。 */

/* ======================================================================== */
/*  op ごとの要求 / 応答構造体 (C os32_gui_shared.h の GuiReq* / GuiResp*)   */
/*  正典は C ヘッダ。末尾追記のみ (契約 T5)。W1 (gshell) と C2 (libos32gui)  */
/*  が共有する。レイアウトは #[repr(C)] で C と一致させる。                   */
/* ======================================================================== */

/* 単一ハンドルを対象にする op (DESTROY / RAISE / SET_FOCUS / CLIENT_RECT)。
 * W1 は arg≠0 ならそれをハンドルとみなし、0 なら要求ブロックのこれを読む。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWindow { pub window: u32 }                                   /* 4B */

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinMove { pub window: u32, pub x: i16, pub y: i16 }          /* MOVE 8B */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinResize { pub window: u32, pub w: i16, pub h: i16 }        /* RESIZE 8B */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinShow { pub window: u32, pub show: u8, pub _pad: [u8; 3] } /* SHOW 8B */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqTextCursor { pub window: u32, pub x: i16, pub y: i16, pub visible: u8, pub _pad: u8 }
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinTitle { pub window: u32, pub title: GuiString }           /* SET_TITLE 260B */

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqSurfCreate { pub w: i16, pub h: i16 }                        /* SURF_CREATE */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqTimerSet { pub window: u32, pub timer_id: u8, pub repeat: u8, pub interval_ticks: u16 } /* 契約 U5、8B */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqTimerKill { pub window: u32, pub timer_id: u8, pub _pad: [u8; 3] }
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqModal { pub buttons: u16, pub _pad: u16, pub message: GuiString } /* MODAL_OPEN 260B */

/* INVALIDATE (op 5): 契約 G4 の invalidate(window, rect)。rect はクライアント座標。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqInvalidate { pub window: u32, pub rect: GuiRect16 }          /* 12B */

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespWindow { pub result: i32, pub window: u32 }                 /* CREATE -> window */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespSurface { pub result: i32, pub surface: u32 }               /* SURF_CREATE -> surface */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespRect { pub result: i32, pub rect: GuiRect16 }               /* CLIENT_RECT -> rect */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespModal { pub result: i32, pub button: i16, pub _pad: i16 }   /* MODAL_OPEN -> button */

const _: () = assert!(size_of::<GuiReqWinMove>() == 8);
const _: () = assert!(size_of::<GuiReqInvalidate>() == 12);
const _: () = assert!(size_of::<GuiReqTimerSet>() == 8);
const _: () = assert!(size_of::<GuiReqWinTitle>() <= GUI_SLOT_REQ_SIZE);
const _: () = assert!(size_of::<GuiReqModal>() <= GUI_SLOT_REQ_SIZE);
const _: () = assert!(size_of::<GuiRespRect>() <= GUI_SLOT_RESP_SIZE);

/* LEASE_PALETTE (op 7、契約 G8): first/count と色 16 本を要求ブロックに直接。count=0 で返却。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqLease { pub first: u16, pub count: u16, pub rgb: [GuiRgb; 16] }   /* 52B */
const _: () = assert!(size_of::<GuiReqLease>() == 52);

/* MODAL_OPEN (op 64、契約 U4): GuiReqModal.buttons と GuiEvtModal.result の値 (W2)。 */
pub const GUI_MODAL_OK: u16 = 0;
pub const GUI_MODAL_OK_CANCEL: u16 = 1;
pub const GUI_MODAL_YES_NO: u16 = 2;
pub const GUI_MODAL_FILE_OPEN: u16 = 3;
/// 1 行入力 (OK / Cancel) — v1.2 M4。prompt は既存 `GuiReqModal.message`。
pub const GUI_MODAL_INPUT: u16 = 4;
pub const GUI_MODAL_RESULT_CANCEL: i16 = 0;
pub const GUI_MODAL_RESULT_OK: i16 = 1;

/* ======================================================================== */
/*  v1.2 追記 (K5) — MODAL_RESULT / SESSION_REQUEST                          */
/*                                                                          */
/*  正典は C 側 os32_gui_shared.h と docs/tasks/gui/v12/CONTRACTS.md         */
/*  §1 (S4 / S5) / §3 (M1 / M4)。GUI_PROTO_VERSION は 1 のまま、末尾追記のみ。*/
/*                                                                          */
/*  MODAL_RESULT (op 65) の consume 規則 (M2): WM はスロットごとに完了済みの  */
/*  結果を 1 件だけ保持し、未 consume の間は MODAL_OPEN が OS32_ERR_FULL、    */
/*  ID 不一致と二重 consume は OS32_ERR_STALE、owner 回収で破棄する。         */
/*                                                                          */
/*  SESSION_REQUEST (op 66) の意味 (S2 / S3): 戻り値 0 は**受理**であって完了 */
/*  ではない。WM は値を私有メモリへ写して pending を立てるだけで、VFS /       */
/*  exec_run / system.cfg 更新はしない。pending 中は OS32_ERR_FULL、不正は    */
/*  OS32_ERR_INVAL、古い WM は OS32_ERR_NOSYS。                              */
/* ======================================================================== */

/// SessionAction の種別 (`GuiReqSession.action`)。0 は「要求なし」で予約。
pub const GUI_SESSION_LAUNCH: u8 = 1;
pub const GUI_SESSION_SWITCH_CUI: u8 = 2;
pub const GUI_SESSION_SHUTDOWN: u8 = 3;

/// `GUI_EV_QUIT` (kind 12) の `GuiEvent.sub` に入る終了理由。0 は予約 (S5)。
/// Quit は制御イベントなのでリング満杯でも捨てず sticky に再配送する。
pub const GUI_QUIT_REASON_REPLACE_APP: u8 = 1;
pub const GUI_QUIT_REASON_SWITCH_CUI: u8 = 2;
pub const GUI_QUIT_REASON_SHUTDOWN: u8 = 3;

/* MODAL_RESULT (op 65) の要求 / 応答。value は MessageBox なら空、File Open は
 * 選択した絶対パス、Input は入力した UTF-8 (最大 255B)。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqModalResult { pub dialog: u16, pub _pad: u16 }               /* 4B */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespModalResult { pub result: i16, pub dialog: u16, pub value: GuiString } /* 260B */

/* SESSION_REQUEST (op 66) の要求。LAUNCH は 1〜255B の絶対パス、
 * SWITCH_CUI / SHUTDOWN は value 空 (len=0)。flags は v1.2 では 0 のみ。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqSession { pub action: u8, pub flags: u8, pub _pad: u16, pub value: GuiString } /* 260B */

const _: () = assert!(size_of::<GuiReqModalResult>() == 4);
const _: () = assert!(size_of::<GuiRespModalResult>() == 260);
const _: () = assert!(size_of::<GuiReqSession>() == 260);
const _: () = assert!(offset_of!(GuiReqModalResult, dialog) == 0);
const _: () = assert!(offset_of!(GuiReqModalResult, _pad) == 2);
const _: () = assert!(offset_of!(GuiRespModalResult, result) == 0);
const _: () = assert!(offset_of!(GuiRespModalResult, dialog) == 2);
const _: () = assert!(offset_of!(GuiRespModalResult, value) == 4);
const _: () = assert!(offset_of!(GuiReqSession, action) == 0);
const _: () = assert!(offset_of!(GuiReqSession, flags) == 1);
const _: () = assert!(offset_of!(GuiReqSession, _pad) == 2);
const _: () = assert!(offset_of!(GuiReqSession, value) == 4);
const _: () = assert!(size_of::<GuiReqModalResult>() <= GUI_SLOT_REQ_SIZE);
const _: () = assert!(size_of::<GuiReqSession>() <= GUI_SLOT_REQ_SIZE);
const _: () = assert!(size_of::<GuiRespModalResult>() <= GUI_SLOT_RESP_SIZE);
