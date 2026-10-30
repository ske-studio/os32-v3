"""4c/1: independent H/V equations, fixed C values/order, synthetic PLL only."""
CHECK = dict(name='gfinit-mode-host', tier=1,
    covers=['tools/tests/gfinit_mode_host.c', 'kdebug/gfinit_mode.c', 'hwdev/gfinit_mode.h', 'kdebug/recover_gf_c.h', 'hwdev/generated/gf_mode_rows.inc', 'tools/gfinit_mode_tables.py', 'tools/gen_recover_tables.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'build/checks.d/check-gfinit-mode-host.mk', 'tools/check_map.d/check-gfinit-mode-host.yaml', 'kdebug/generated/recover_gf_tables.inc', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover_gf.c', 'kdebug/gfinit_pll.c', 'kdebug/gfinit_pll/*.inc', 'lib/crc32_core.inc', 'tools/tests/recover_gf_host.c', 'tools/tests/recover_cleanup_host.inc', 'hwdev/gf_load_state.c', 'hwdev/gf_load_state.h', 'hwdev/gf/display/calc.h', 'hwdev/gf/display/calc.c', 'hwdev/gf/display/timing.c', 'hwdev/gf/display/format.c', 'hwdev/gf/display/pitch.c', 'hwdev/gf/display/start.c', 'hwdev/gf/display/dac_format.c', 'hwdev/gf/display/output.c', 'hwdev/gf/display/cursor.c', 'hwdev/gf/display/fifo.c', 'hwdev/gf/display/extended.c', 'hwdev/gf/display/pll_program.c', 'tools/tests/gf_display_calc_host.inc', 'hwdev/gf/quiesce_rows.h', 'tools/tests/recover_gf_cases_host.inc', 'tools/tests/recover_gf_fixture_host.inc'],
    needs=[], budget=60.0)

import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']

ROOT = pathlib.Path(__file__).resolve().parents[2]
FULL = 0xffffffff
LIVE = 0xa5a55a5a


def command(exe):
    return ['gcc', *FLAGS, '-O2', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'include'), str(ROOT/'tools/tests/gfinit_mode_host.c'), '-o', str(exe)]


def timings():
    # Independent oracle: DESIGN §1 pixel/line inputs -> §3 intermediate
    # values -> bit positions in nouveau crtc.c:350-408/nvreg.h. No product
    # constants or generated tables are used for these expectations.
    hd, hs, he, ht, hbs, hbe = 640//8-1, 656//8+1, 752//8+1, 800//8-5, 640//8-1, 800//8-1
    vd, vs, ve, vt, vbs, vbe = 480-1, 490-1, 492-1, 525-2, 480-1, 525-1
    bit = lambda v, n: (v >> n) & 1
    cr = [0]*160
    cr[:10] = [ht & 255, hd & 255, hbs & 255, 128 | (hbe & 31), hs & 255,
               (bit(hbe, 5) << 7) | (he & 31), vt & 255,
               (bit(vs, 9)<<7) | (bit(vd, 9)<<6) | (bit(vt, 9)<<5) | 16 |
               (bit(vbs, 8)<<3) | (bit(vs, 8)<<2) | (bit(vd, 8)<<1) | bit(vt, 8),
               0, 64 | (bit(vbs, 9)<<5)]
    cr[16:25] = [vs & 255, 32 | (ve & 15), vd & 255, 640//8, 0,
                 vbs & 255, vbe & 255, 0x43, 255]
    cr[0x25] = (bit(hbe, 6)<<4) | (bit(vbs, 10)<<3) | (bit(vs, 10)<<2) | (bit(vd, 10)<<1) | bit(vt, 10)
    cr[0x2d] = (bit(hs, 8)<<3) | (bit(hbs, 8)<<2) | (bit(hd, 8)<<1) | bit(ht, 8)
    cr[0x41] = (bit(vbs, 11)<<6) | (bit(vs, 11)<<4) | (bit(vd, 11)<<2) | bit(vt, 11)
    # Independently transcribed DESIGN §3 L3b (incl. CR05=9f), then compare
    # equations with the handwritten design before comparing the implementation.
    handwritten = bytes.fromhex('5f 4f 4f 83 53 9f 0b 3e 00 40 00 00 00 00 00 00 e9 2b df 50 00 df 0c 43 ff')
    assert cr[:25] == list(handwritten)
    assert [cr[i] for i in (0x25, 0x2d, 0x41)] == [0x10, 0, 0]
    return [hd, hs, he, ht, hbs, hbe, vd, vs, ve, vt, vbs, vbe], cr


def expected_state(variant):
    _, cr = timings()
    for i, v in {0x1a:4, 0x1b:4, 0x20:0xc0, 0x28:1, 0x30:0x80,
                 0x39:255, 0x3b:0x22, 0x85:255, 0x86:1, 0x9f:1}.items():
        cr[i] = v
    if variant:
        cr[0x1c], cr[0x33], cr[0x3c], cr[0x4b], cr[0x52] = 0xdf, 0xc4, 0x5a, 0x92, 2
    else:
        cr[0x4b], cr[0x52] = 0x80, 4
    return dict(cr=cr, sr=[0,0x21,15,0,14], gr=[0,0,0,0,0,0x40,5,15,255],
                ar=list(range(16))+[1,0,15,0,0], misc=0xe3, pllsel=0x10000204,
                sel_clk=0xabc8ef01 if variant else 0, cursync=0x2000000,
                general=0x20100130, ramdac630=0, ramdac634=0, tv_setup=0,
                fp_control=0x20000022, ramdac8c0=0x100, ramdaca20=0,
                ramdaca24=0xfffff, ramdaca34=1, engine_ctrl=0x10,
                cursor_cfg=0x4011100, crtc830=477, crtc834=479,
                gpio_ext=0x12345678 if variant else 0,
                crtc850=0x87654321 if variant else 0, config=2, start=0,
                dacclk=1, test_control=0x100000)


def expected_trace(state, pll):
    """Independent sequential §3 write oracle, with abstract typed AR/DAC rows.

    Each row: kind, offset, index, width, final value, count. Live source is a
    fixed test value, not a simulated device. Delays/checks are stage-2 work.
    """
    out = []
    def w(kind, reg, v, index=FULL, count=1):
        out.append((kind, reg, index, 4 if kind == 0 else 1, v, count))
    def cr(i, v=None): w(1, 0x6013d4, state['cr'][i] if v is None else v, i)
    def sr(i, v=None): w(2, 0xc03c4, state['sr'][i] if v is None else v, i)
    def m(reg, name): w(0, reg, state[name])
    w(0,0x68052c,LIVE & ~1); m(0x680848,'fp_control')
    cr(0x1f,0x57); cr(0x44,0); sr(0,1); sr(1,0x7a)
    cr(0x17,0x5a); sr(0,3); cr(0x1a,0xda)
    cr(0x44,0); sr(0,1); sr(1,0x7a); sr(0,3)
    w(0,0x600804,1); w(0,0x680900,LIVE & ~0x10000)
    cr(0x21,0xfa); m(0x680524,'sel_clk'); cr(0x45); m(0x680634,'ramdac634')
    for i in (0x30,0x31,0x2f): cr(i)
    w(0,0x680300,LIVE)
    m(0x68052c,'dacclk'); w(0,0x68252c,LIVE|0x100); m(0x680608,'test_control')
    sr(0,1); sr(1,0x7a); w(6,0x6013c0,0); m(0x680404,'cursync')
    n, md, n2, m2, p, actual, error = pll
    for reg, v in [(0xc040,LIVE & ~0x30000), (0x680580,LIVE|0x100),
                   (0x680578,(LIVE & 0x7fff0000)|0x8000011f),
                   (0x680508,(LIVE & 0xfff80000)|(p<<16)|(n<<8)|md), (0xc040,LIVE)]:
        w(0,reg,v)
    for reg, name in [(0x68050c,'pllsel'),(0x680524,'sel_clk'),(0x680600,'general'),
                      (0x680630,'ramdac630'),(0x680634,'ramdac634'),(0x680700,'tv_setup'),
                      (0x680848,'fp_control'),(0x6808c0,'ramdac8c0'),(0x680a20,'ramdaca20'),
                      (0x680a24,'ramdaca24'),(0x680a34,'ramdaca34'),(0x600860,'engine_ctrl')]: m(reg,name)
    w(0,0x8704,1); w(0,0x8140,0); w(0,0x1588,0)
    for reg, name in [(0x600810,'cursor_cfg'),(0x600830,'crtc830'),(0x600834,'crtc834'),
                      (0x60081c,'gpio_ext'),(0x600850,'crtc850')]: m(reg,name)
    w(0,0x680900,LIVE|0x10000); m(0x600804,'config')
    for i in (0x19,0x1a,0x25,0x28,0x33,0x2d,0x1c,0x1b,0x20,0x47,0x9f,0x49,0x30,0x31,0x2f): cr(i)
    w(0,0x680300,LIVE); cr(0x39)
    for i in (0x3b,0x3c,0x41,0x45,0x4b,0x52,0x42,0x53,0x54): cr(i)
    for i in range(16): cr(0x57,i); cr(0x58,0)
    for i in (0x59,0x5b,0x85,0x86): cr(i)
    m(0x600800,'start')
    w(7,0x6813c6,255); w(8,0x6813c8,0); w(9,0x6813c9,0,count=768)
    w(5,0xc03c2,0xe3)
    for i in range(5): sr(i)
    cr(0x11,0x5a)
    for i in range(25): cr(i)
    cr(0x11,0xda)
    for i,v in enumerate(state['gr']): w(3,0xc03ce,v,i)
    for i,v in enumerate(state['ar']): w(4,0x6013c0,v,i)
    w(6,0x6013c0,32); sr(1,0x5a); sr(0,3); w(6,0x6013c0,32)
    cr(0x28); m(0x680600,'general')
    for i in (0x19,0x13,0x42): cr(i)
    m(0x600800,'start')
    for i in (0x1b,0x20,0x47): cr(i)
    sr(0,1); sr(1,1); cr(0x17,0xc3); sr(0,3); cr(0x1a,4)
    w(0,0x68052c,LIVE|1); w(10,0xfac,1)
    return out


def check_output(text, verbose=True):
    assert 'FAIL' not in text
    groups = text.split('variant=')[1:]
    assert len(groups) == 2
    for variant, section in enumerate(groups):
        lines = section.splitlines()[1:]
        fields, rows, writes, palette, sections, waits = {}, [], [], [], [], []
        for line in lines:
            name, values = line.split('=',1)
            v = list(map(int,values.split()))
            if name == 'section': sections.append(v)
            elif name == 'wait': waits.append(v)
            elif name == 'row': rows.append(v)
            elif name == 'pllwrite': writes.append(v)
            elif name == 'rgb': palette.append(v)
            elif name != 'target': fields[name] = v
        assert sections == [[i] for i in range(7)]
        assert waits == [[50, 1, 2], [10, 0, 6], [10, 0, 201]]
        timing, _ = timings()
        assert fields['timing'] == timing
        state = expected_state(variant)
        for name, value in state.items():
            assert fields[name] == (value if isinstance(value,list) else [value]), (name,fields[name],value)
        pll = fields['pll']
        n, m, n2, m2, p, actual, error = pll
        assert 1 <= n <= 255 and 1 <= m <= 27 and n2 == m2 == 1 and 0 <= p <= 6
        assert 1000 <= 27000//m <= 27000 and 100000 <= 27000*n//m <= 400000
        div = 1<<p
        assert actual == ((n*27000+div//2)//div+m//2)//m
        assert error == abs(actual-25175) and error*200 <= 25175
        # Exhaustive independent candidate search establishes nearest error.
        best = min(abs(round(27000*nn/mm/(1<<pp))-25175)
                   for pp in range(7) for mm in range(1,28) for nn in range(1,256)
                   if 100000 <= 25175*(1<<pp) <= 400000 and 1000 <= 27000//mm <= 27000)
        assert error == best
        assert writes == [[0xc040,0xc040,0xfffcffff,0], [0x680580,0x680580,0xfffffeff,256],
                          [0x680578,0x680578,0x7fff0000,0x8000011f],
                          [0x680508,0x680508,0xfff80000,(p<<16)|(n<<8)|m],
                          [0xc040,0xc040,FULL,0]]
        colors = [[0,0,0],[255,0,0],[0,255,0],[0,0,255],[255,255,255],[0,255,255],[255,0,255],[255,255,0]]
        assert palette == [[i]+(colors[i] if i<8 else [0,0,0]) for i in range(256)]
        expected = expected_trace(state,pll)[:-1]
        assert len(rows) == len(expected) == 205
        for ordinal, (row, want) in enumerate(zip(rows,expected)):
            kind,reg,index,width,expr,source,keep,bits,verify,count,flags,value = row
            got = kind,reg,index,width,value,count
            assert got == want, (ordinal,got,want)
            assert verify <= (FULL if width == 4 else 255)
            assert flags == (1 if ordinal == 1 else 0)
            if kind in (4,6,7,8,9): assert verify == 0
            if reg == 0x68050c: assert verify == 0xefffffff and value == 0x10000204
            if reg == 0x680608: assert verify == 0x100000
            if reg == 0x600804: assert verify == 3
            if reg == 0x68052c: assert verify == 0x101
            if reg == 0x680578: assert verify == 0
            if reg == 0x680508: assert verify == 0
            assert reg != 0x680510
        if verbose:
            print(f'PASS variant={variant}: H/V equations + handwritten CR00..18/25/2D/41, CR05=9f; '
                  f'205 ordered typed hardware rows; N/M/N/M/P={n}/{m}/{n2}/{m2}/{p} actual/error={actual}/{error} kHz')
            print('timing='+' '.join(f'{v:X}' for v in timing))
            print('CR00..18='+' '.join(f'{v:02X}' for v in state['cr'][:25]))
    return 0


MUTATIONS = [
    ('checker_polarity', '? GF_MODE_COLOR_BLACK : GF_MODE_COLOR_WHITE', '? GF_MODE_COLOR_WHITE : GF_MODE_COLOR_BLACK'),
    ('frame_bottom', 'y >= GF_MODE_HEIGHT - GF_MODE_FRAME_WIDTH)', 'y > GF_MODE_HEIGHT - GF_MODE_FRAME_WIDTH)'),
    ('immediate_verify', '0x0U, 0x1U, 0x0U},', '0x7ffffU, 0x1U, 0x0U},'),
    ('cr05', 'mode_bit(t->hbe, 5, 7)', 'mode_bit(t->hbe, 5, 6)', 'hwdev/gf/display/timing.c'),
    ('50c_bit28', 'o->pllsel = GF_MODE_PLL_SELECT;', 'o->pllsel = GF_MODE_PLL_SELECT & ~GF_MODE_PLLSEL_VPLL_A;', 'hwdev/gf/display/pll_program.c'),
    ('single_stage', 'if (o->single) o->limits.vco2 = (struct gfinit_pll_vco){0};', '(void)o->single;', 'hwdev/gf/display/pll_program.c'),
]


def mutation(item):
    name, old, new = item[:3]
    # The calc tail is split into .inc files included in place (kdebug/gfinit_mode/); the 4th item names it.
    file = 'hwdev/generated/gf_mode_rows.inc' if name == 'immediate_verify' else (item[3] if len(item) == 4 else 'kdebug/gfinit_mode.c')
    source = (ROOT/file).read_text()
    if name == 'immediate_verify':
        old = next(line for line in source.splitlines() if '0x680508U' in line and line.endswith(old))
        new = old.rsplit('0x0U, 0x1U, 0x0U},', 1)[0] + new
    assert not old or source.count(old) == 1
    with tempfile.TemporaryDirectory(prefix='gfmode-mut-') as td:
        exe = pathlib.Path(td)/'host'
        tree = mutpar.build_in_tree(ROOT,td,{file:source.replace(old,new,1) if old else source},
                                   [command(exe)],capture_output=True)
        # gcc -MM drives copies, including all new headers; assert they resolve
        # inside the mutant tree so a header-copy failure cannot look like RED.
        for path in ('hwdev/gfinit_mode.h','hwdev/gf_load_state.c','hwdev/gf_load_state.h','hwdev/generated/gf_mode_rows.inc'):
            assert (tree/path).is_file() and not (tree/path).is_symlink(), path
        run = host32.run([str(exe)],cwd=tree,timeout=60,capture_output=True)
        rc = run.returncode
        if not rc:
            try:
                check_output(run.stdout.decode(),verbose=False)
            except AssertionError:
                rc = 1
        return f'{name}: {"RED" if rc else "GREEN"} rc={rc}', int(rc == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='gfmode-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(command(exe),cwd=ROOT,check=True)
        result = host32.run([str(exe)],cwd=ROOT,timeout=60,capture_output=True)
        if result.returncode:
            print("\n".join(line for line in result.stdout.decode().splitlines() if line.startswith("FAIL")))
            return result.returncode
        check_output(result.stdout.decode())
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,MUTATIONS,('control','',''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gfinit_mode.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
