//! reqs.rs — op ごとの要求 / 応答構造体 (C `os32_gui_shared.h` の GuiReq* / GuiResp*
//! を Rust へ写したもの)。**正典は C ヘッダ。末尾追記のみ (契約 T5)。**
//!
//! os32api::gui::proto には GuiWinSpec / GuiString / GuiRect16 だけが写されている
//! (C1 の領分)。gshell が要求ブロックから読む残りの小構造体をここに置く。
//! レイアウトは C の #[repr(C)] と一致させる (要求ブロック 512B 以内)。

#![allow(dead_code)]

use os32api::gui::proto::{GuiRect16, GuiString};

/* 単一ハンドル対象 (DESTROY / RAISE / SET_FOCUS / CLIENT_RECT)。arg で渡す場合も
 * あるが、CLIENT_RECT のように応答が要るものは要求ブロック経由。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWindow {
    pub window: u32,
}

/* INVALIDATE (op 5) 用。共有ヘッダ os32_gui_shared.h 末尾に
 * `typedef struct { u32 window; GuiRect16 rect; } GuiReqInvalidate;` (12B) が
 * 追記済み (2026-09-06、前回の申し送り分) なので、こちらはその写し。
 * **PM への申し送り**: C1 の `proto.rs` にはまだ写されていない (C2 が要求
 * ブロックへ書くときに要る)。 */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqInvalidate {
    pub window: u32,
    pub rect: GuiRect16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinMove {
    pub window: u32,
    pub x: i16,
    pub y: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinResize {
    pub window: u32,
    pub w: i16,
    pub h: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinShow {
    pub window: u32,
    pub show: u8,
    pub _pad: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqTextCursor {
    pub window: u32,
    pub x: i16,
    pub y: i16,
    pub visible: u8,
    pub _pad: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqWinTitle {
    pub window: u32,
    pub title: GuiString,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqSurfCreate {
    pub w: i16,
    pub h: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqTimerSet {
    pub window: u32,
    pub timer_id: u8,       /* Timer イベントの sub と同幅 (契約 U5) */
    pub repeat: u8,         /* 0 = 単発 (1 回発火して WM が消す) */
    pub interval_ticks: u16, /* tick (10ms) 単位 */
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqTimerKill {
    pub window: u32,
    pub timer_id: u8,
    pub _pad: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiReqModal {
    pub buttons: u16,
    pub _pad: u16,
    pub message: GuiString,
}

/* ---- 応答 ---- */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespWindow {
    pub result: i32,
    pub window: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespSurface {
    pub result: i32,
    pub surface: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespRect {
    pub result: i32,
    pub rect: GuiRect16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiRespModal {
    pub result: i32,
    pub button: i16,
    pub _pad: i16,
}
