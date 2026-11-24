"""Compare integer stereo samples from identical register and render sequences."""
CHECK = dict(name='opna-psg', tier=1, needs=[], budget=5,
             covers=['lib/os32_opna/src/psg.rs', 'lib/os32_opna/src/state.rs',
                     'lib/os32_opna/src/tables.rs', 'lib/os32_opna/upstream/fmgen_psg.*',
                     'lib/os32_opna/upstream/fmgen_headers.h', 'lib/os32_opna/upstream/fmgen_misc.h',
                     'lib/os32_opna/upstream/fmgen_types.h'],
             basis=['docs/decisions/sound/SYNTH_OPNA_DESIGN.md §9'])


def sequence():
    for p in range(3):
        yield 0, p, 0
        yield 1, 0, 0
        yield 4, 7, 0
        for reg, value in enumerate([13, 240, 39, 0, 1, 1, 31, 0, 15, 9, 6, 0, 0]):
            yield 2, reg, value
        for mixer in range(64):
            yield 2, 7, mixer
            yield 4, 79, 0
        for shape in range(16):
            yield 2, 13, shape
            yield 2, 7, shape
            yield 2, 11, shape % 4
            for mask in range(8):
                yield 3, mask, 0
                yield 2, 8 + mask % 3, 16 | shape
                yield 4, 255, 0
                yield 4, 0, 0
        yield 3, 0, 0
        yield 2, 7, 7
        yield 2, 6, 0
        yield 0, p, 0
        yield 4, 256, 0


CPP = r"""
#include "fmgen_psg.h"
#include <cstdio>
int main() {
    static PSG chip;
    unsigned op, a, b;
    while (scanf("%u%u%u", &op, &a, &b) == 3) {
        switch (op) {
        case 0: chip.SetClock((7987200 / 2) / (4 >> a), 44100); break;
        case 1: chip.Reset(); break;
        case 2: chip.SetReg(a, b); break;
        case 3: chip.SetChannelMask(a); break;
        case 4:
            int buffer[512];
            for (unsigned i = 0; i < 2*a; ++i) buffer[i] = int(i) - 137;
            chip.Mix(buffer, a);
            fwrite(buffer, sizeof(int), 2*a, stdout);
        }
    }
}
"""

RUST = """
mod state;
mod psg;
mod tables;
use std::io::{self, Read, Write};
fn main() {
    let mut chip: state::Psg = unsafe { core::mem::zeroed() };
    let mut p = 0;
    chip.init(p);
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    let mut out = io::BufWriter::new(io::stdout().lock());
    for line in input.lines() {
        let v: Vec<usize> = line.split_whitespace().map(|s| s.parse().unwrap()).collect();
        match v[0] {
            0 => { p = v[1] as u8; chip.set_clock(p); }
            1 => chip.reset(p),
            2 => chip.set_reg(v[1], v[2] as u8, p),
            3 => chip.set_channel_mask(v[1] as i32),
            4 => {
                let mut buf: Vec<i32> = (0..2*v[1]).map(|i| i as i32 - 137).collect();
                chip.mix(&mut buf);
                for n in buf { out.write_all(&n.to_ne_bytes()).unwrap(); }
            }
            _ => unreachable!()
        }
    }
}
"""


def run(ctx):
    import subprocess
    import tempfile
    from pathlib import Path
    core = ctx.root / 'lib/os32_opna'
    upstream = core / 'upstream'
    commands = ''.join(f'{op} {a} {b}\n' for op, a, b in sequence()).encode()
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        (work / 'compiler.h').touch()
        (work / 'oracle.cpp').write_text(CPP)
        subprocess.run(['g++', '-std=c++17', '-DSUPPORT_FMGEN', '-I', work, '-I', upstream,
                        work / 'oracle.cpp', upstream / 'fmgen_psg.cpp', '-o', work / 'oracle'],
                       check=True)
        expected = subprocess.check_output([work / 'oracle'], input=commands)
        for module in ['state', 'tables']:
            (work / f'{module}.rs').symlink_to(core / 'src' / f'{module}.rs')
        source = (core / 'src/psg.rs').read_text()
        (work / 'main.rs').write_text(RUST)

        def compare(code):
            (work / 'psg.rs').write_text(code)
            subprocess.run(['rustc', '--edition=2021', '-Adead_code', work / 'main.rs',
                            '-o', work / 'render'], check=True)
            return subprocess.check_output([work / 'render'], input=commands) == expected

        if not compare(source):
            raise RuntimeError('PSG stereo samples differ from fmgen')
        print(f'{len(expected) // 8} stereo frames match fmgen')
        if ctx.mutate:
            for anchor, replacement in [('tone_high || noise_high', 'tone_high && noise_high'),
                                        ('self.noise_period /= 2', 'self.noise_period /= 3'),
                                        ('phase |= limit / 2', 'phase |= limit / 4')]:
                if source.count(anchor) != 1 or compare(source.replace(anchor, replacement)):
                    raise RuntimeError(f'PSG mutation failed: {anchor}')
                print(f'RED: {anchor}')
    return 0
