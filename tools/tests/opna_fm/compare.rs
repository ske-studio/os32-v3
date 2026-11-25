mod channel;
mod operator;
mod state;
mod tables;

use operator::Clock;
use state::Channel;

extern "C" {
    fn setup(p: u32, algo: u32, fb: u32, shape: u32);
    fn tick(n: u32, mode: u32) -> i32;
}

fn main() {
    compare_mix();
    // Mid-note SSG enable leaves fmgen outside its asserted table domain.
    let mut edge: Channel = unsafe { core::mem::zeroed() };
    let clock = Clock::new(0);
    edge.reset(clock);
    edge.key_control(1, clock);
    let op = &mut edge.operators[0];
    op.set_ssg(8);
    op.prepare(clock);
    assert_eq!((op.ssg_offset, op.ssg_vector), (512, 1));
    // Channel is repr(C) and consists exclusively of integer fields.
    let mut ch: Channel = unsafe { core::mem::zeroed() };
    for p in 0..3 {
        let clock = Clock::new(p);
        for algo in 0..8 {
            for fb in 0..8 {
                for shape in [0, 8, 9, 10, 11, 12, 13, 14, 15] {
                    unsafe {
                        setup(p as u32, algo, fb, shape);
                    }
                    ch.key_control(0, clock);
                    ch.reset(clock);
                    ch.set_algorithm(algo);
                    ch.set_feedback(fb);
                    ch.set_ms(0x37);
                    ch.set_fnum(0x37e3);
                    for (i, op) in ch.operators.iter_mut().enumerate() {
                        let i = i as u32;
                        op.set_dt(i + 3);
                        op.set_dt2(i);
                        op.set_multiple(i * 5);
                        op.set_tl(i * 3, false);
                        op.set_ks(i);
                        op.set_ar(48 + i * 5);
                        op.set_dr(53);
                        op.set_sr(55);
                        op.set_sl(12 + i * 4);
                        op.set_rr(50);
                        op.set_ssg(shape);
                        op.set_amon(i != 1);
                    }
                    ch.prepare(clock);
                    ch.key_control(15, clock);
                    for n in 0..1024 {
                        match n {
                            701 => ch.key_control(5, clock),
                            812 => ch.key_control(15, clock),
                            911 => {
                                ch.set_kckf(101, 37);
                                ch.prepare(clock);
                            }
                            _ => {}
                        }
                        let mode = (n / 64) % 4;
                        let lfo = (mode & 1 != 0).then(|| ch.lfo((n * 7) as u8, (n * 3) as u8));
                        let actual = if mode < 2 {
                            ch.calc(clock, lfo)
                        } else {
                            ch.calc_noise(n, clock, lfo)
                        };
                        assert_eq!(
                            actual,
                            unsafe { tick(n, mode) },
                            "p={p} algo={algo} fb={fb} shape={shape} frame={n}"
                        );
                    }
                }
            }
        }
    }
}

fn compare_mix() {
    extern "C" {
        fn setup_mix();
        fn mix_block(mode: u32, out: *mut i32);
    }
    unsafe {
        setup_mix();
    }
    // Null borrowed pointers and zero counters are valid in this FM-only fixture.
    let mut audio: state::AudioState = unsafe { core::mem::zeroed() };
    let clock = Clock::new(0);
    audio.lfo_count = 0xffff0000;
    audio.lfo_step = 45123;
    for (i, ch) in audio.channels.iter_mut().enumerate() {
        ch.reset(clock);
        ch.set_algorithm(7);
        ch.set_feedback(i as u32);
        ch.set_ms(0x37);
        audio.fnum[i] = 0x2830 + i as u32 * 11;
        audio.pan[i] = ((i + 1) % 4) as u8;
        ch.set_fnum(audio.fnum[i]);
        for op in &mut ch.operators {
            op.set_ar(63);
            op.set_tl(0, false);
            op.set_multiple(4);
            op.set_amon(true);
        }
        ch.prepare(clock);
        ch.key_control(15, clock);
    }
    audio.channel3_fnum = [0x3800, 0x3833, 0x3866];
    for mode in 0..8 {
        audio.timer_control_shadow = if mode & 1 != 0 { 0xc0 } else { 0 };
        audio.reg22 = if mode & 2 != 0 { 8 } else { 0 };
        audio.reg29 = if mode & 4 != 0 { 0x80 } else { 0 };
        let mut samples = [0; 512];
        audio.fm_mix(&mut samples);
        let mut expected = [0; 512];
        unsafe {
            mix_block(mode, expected.as_mut_ptr());
        }
        for (i, value) in expected.iter().enumerate() {
            assert_eq!(
                samples[i],
                *value,
                "mix mode={mode} frame={} side={}",
                i / 2,
                i % 2
            );
        }
    }
}
