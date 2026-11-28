//! Public AV HAL structures shared by the two 32-bit targets.
#![allow(non_camel_case_types)]

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_Fmt {
    pub rate: u32,
    pub bits: u8,
    pub channels: u8,
    pub enc: u8,
    pub flags: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_DevInfo {
    pub size: u32,
    pub dev: u32,
    pub kind: u32,
    pub flags: u32,
    pub name: [u8; 32],
    pub driver: [u8; 16],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_Caps {
    pub size: u32,
    pub dev: u32,
    pub kind: u32,
    pub fmt_count: u32,
    pub fmt: [SND2_Fmt; 16],
    pub period_min: u32,
    pub period_max: u32,
    pub ring_frames: u32,
    pub rsv_lead: u32,
    pub latency_min_us: u32,
    pub volume_steps: u32,
    pub hw_mix_streams: u32,
    pub hw_3d_streams: u32,
    pub dsp_units: u32,
    pub reserved: [u32; 19],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_DspUnit {
    pub id: u32,
    pub kind: u32,
    pub modes: u32,
    pub in_fmt_mask: u32,
    pub out_fmt_mask: u32,
    pub param_count: u32,
    pub max_frames: u32,
    pub reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_DspParam {
    pub id: u32,
    pub min: i32,
    pub max: i32,
    pub def: i32,
    pub name: [u8; 16],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_StreamDesc {
    pub size: u32,
    pub dev: u32,
    pub fmt: SND2_Fmt,
    pub period_frames: u32,
    pub lead_periods: u32,
    pub flags: u32,
    pub reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_Pos {
    pub size: u32,
    pub state: u32,
    pub played: u32,
    pub written: u32,
    pub cap: u32,
    pub underruns: u32,
    pub epoch: u32,
    pub time_ms: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_FmEv {
    pub kind: u8,
    pub bank: u8,
    pub reg: u8,
    pub val: u8,
    pub at_us: u32,
    pub seq: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_ChipCaps {
    pub size: u32,
    pub dev: u32,
    pub kind: u32,
    pub chip: u32,
    pub clock_hz: u32,
    pub base_port: u16,
    pub id_port: u16,
    pub banks: u8,
    pub fm_ch: u8,
    pub ssg_ch: u8,
    pub rhythm_ch: u8,
    pub flags: u32,
    pub adpcm_bytes: u32,
    pub reserved: [u32; 7],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SND2_RegW {
    pub bank: u8,
    pub reg: u8,
    pub val: u8,
    pub rsv: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_Gamepad {
    pub buttons: u16,
    pub lt: u8,
    pub rt: u8,
    pub lx: i16,
    pub ly: i16,
    pub rx: i16,
    pub ry: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_PadState {
    pub packet: u32,
    pub pad: IN2_Gamepad,
    pub time_ms: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_Vibration {
    pub left: u16,
    pub right: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_PadCaps {
    pub size: u32,
    pub r#type: u8,
    pub subtype: u8,
    pub flags: u16,
    pub caps: IN2_Gamepad,
    pub vib: IN2_Vibration,
    pub vid: u16,
    pub pid: u16,
    pub source: u8,
    pub map_src: u8,
    pub rsv: u16,
    pub name: [u8; 32],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_DevInfo {
    pub size: u32,
    pub dev: u8,
    pub kind: u8,
    pub source: u8,
    pub flags: u8,
    pub vid: u16,
    pub pid: u16,
    pub name: [u8; 20],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventKey {
    pub sc: u8,
    pub down: u8,
    pub mods: u8,
    pub rsv: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventMove {
    pub dx: i16,
    pub dy: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventBtn {
    pub down: u8,
    pub changed: u8,
    pub rsv: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventWheel {
    pub delta: i16,
    pub rsv: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventPad {
    pub buttons: u16,
    pub changed: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventAbs {
    pub x: u16,
    pub y: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_EventLost {
    pub reason: u16,
    pub count: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union IN2_EventData {
    pub key: IN2_EventKey,
    pub r#move: IN2_EventMove,
    pub btn: IN2_EventBtn,
    pub wheel: IN2_EventWheel,
    pub pad: IN2_EventPad,
    pub abs: IN2_EventAbs,
    pub lost: IN2_EventLost,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_Event {
    pub time_ms: u32,
    pub seq: u16,
    pub dev: u8,
    pub r#type: u8,
    pub u: IN2_EventData,
    pub flags: u16,
    pub rsv: u16,
}

pub const IN2_STATUS_ACQUIRE_GEN: usize = 0;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct IN2_Status {
    pub size: u32,
    pub acquired: u32,
    pub focus_owner: u32,
    pub focus_gen: u32,
    pub dropped: u32,
    pub rsv: [u32; 3],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_MouseState {
    pub dx: i32,
    pub dy: i32,
    pub buttons: u32,
    pub wheel: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct IN2_WmState {
    pub size: u32,
    pub gen: u32,
    pub excl: u32,
    pub owner: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct V86_Begin {
    pub size: u32,
    pub path: [u8; 64],
    pub second: [u8; 64],
    pub flags: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct V86_RunIn {
    pub size: u32,
    pub slice_ms: u32,
    pub reply_valid: u32,
    pub reply: [u8; 4],
    pub snd_status: u32,
    pub shadow_seq: u32,
    pub rsv: [u32; 2],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct V86_ReadQ {
    pub bank: u8,
    pub reg: u8,
    pub kind: u8,
    pub byte: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct V86_Exit {
    pub size: u32,
    pub reason: u32,
    pub port: u32,
    pub width: u32,
    pub fifo_n: u32,
    pub guest_us: u32,
    pub code: u32,
    pub nq: u32,
    pub q: [V86_ReadQ; 4],
}
