/// Independent live timer storage for the private timer ABI.
#[repr(C)]
pub struct TimerState {
    pub status: u8,
    pub control: u8,
    pub reg_a: [u8; 2],
    pub period_a: i32,
    pub count_a: i32,
    pub period_b: i32,
    pub count_b: i32,
    pub step: i32,
}

#[repr(C)]
pub struct Operator {
    pub out: i32,
    pub previous_out: i32,
    pub feedback_input: i32,
    pub delta: u32,
    pub detune: u32,
    pub detune2: u32,
    pub multiple: u32,
    pub phase: u32,
    pub phase_delta: u32,
    pub phase_delta_lfo: i32,
    pub block_note: u32,
    pub eg_level: i32,
    pub eg_next_level: i32,
    pub eg_count: i32,
    pub eg_count_delta: i32,
    pub eg_out: i32,
    pub tl_out: i32,
    pub eg_rate: i32,
    pub eg_curve_count: i32,
    pub ssg_offset: i32,
    pub ssg_vector: i32,
    pub ssg_phase: i32,
    pub key_scale_rate: u32,
    pub eg_phase: u32,
    pub sensitivity: u32,
    pub total_level: u32,
    pub total_level_latch: u32,
    pub attack: u32,
    pub decay: u32,
    pub sustain_rate: u32,
    pub sustain_level: u32,
    pub release: u32,
    pub key_scale: u32,
    pub ssg_type: u32,
    pub key_on: u8,
    pub amplitude_modulation: u8,
    pub dirty: u8,
    pub mute: u8,
}

#[repr(C)]
pub struct Channel {
    pub operators: [Operator; 4],
    pub buffer: [i32; 4],
    pub feedback: u32,
    pub algorithm: u32,
}

#[repr(C)]
pub struct Psg {
    pub registers: [u8; 16],
    pub output_level: [u32; 3],
    pub tone_count: [u32; 3],
    pub tone_period: [u32; 3],
    pub envelope_count: u32,
    pub envelope_period: u32,
    pub noise_count: u32,
    pub noise_period: u32,
    pub mask: i32,
}

/// RAM is borrowed from C. The CPU streaming FIFO is independent of RAM.
#[repr(C)]
pub struct Adpcm {
    pub ram: *mut u8,
    pub start: u32,
    pub stop: u32,
    pub address: u32,
    pub limit: u32,
    pub delta_n: u32,
    pub read_buffer: u32,
    pub level: i32,
    pub volume: i32,
    pub phase: i32,
    pub phase_delta: i32,
    pub sample: i32,
    pub step: i32,
    pub output: i32,
    pub previous_mix: i32,
    pub current_mix: i32,
    pub playing: u8,
    pub muted: u8,
    pub control1: u8,
    pub control2: u8,
    pub granularity: i8,
    pub registers: [u8; 8],
    pub cpu_stream: u8,
    pub fifo_current: u8,
    pub fifo_low: u8,
    pub fifo_read: u32,
    pub fifo_write: u32,
    pub fifo_count: u32,
    pub fifo: [u8; 65536],
}

/// The caller keeps the waveform valid across resets.
#[repr(C)]
pub struct RhythmVoice {
    pub samples_s16le: *const u8,
    pub frames: u32,
    pub source_rate: u32,
    pub position: u32,
    pub step: u32,
    pub pan: u8,
    pub level: i8,
    pub volume: i32,
}

#[repr(C)]
pub struct AudioState {
    pub channels: [Channel; 6],
    pub psg: Psg,
    pub adpcm: Adpcm,
    pub rhythm: [RhythmVoice; 6],
    pub rhythm_key: u8,
    pub rhythm_ready: u8,
    pub prescaler: u8,
    pub reg22: u8,
    pub reg29: u8,
    pub timer_control_shadow: u8,
    pub rhythm_level: i8,
    pub rhythm_volume: i32,
    pub rhythm_mask: i32,
    pub pan: [u8; 6],
    pub fnum_latch: [u8; 9],
    pub fnum: [u32; 6],
    pub channel3_fnum: [u32; 3],
    pub status: u32,
    pub status_mask: u32,
    pub status_next: u32,
    pub lfo_count: u32,
    pub lfo_step: u32,
    pub am_level: u32,
    pub pm_level: u32,
    pub pm_value: i32,
    pub stereo_scratch: [i32; 512],
}

/// Combined state for the standalone oracle wrapper.
#[repr(C)]
pub struct OpnaState {
    pub audio: AudioState,
    pub timer: TimerState,
}

