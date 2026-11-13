#!/usr/bin/env python3
"""RH capture/translation/raster guards using the real product, no host libc."""
CHECK = dict(name='ppc-rh-observe-host', tier=2, budget=30, needs=[], covers=[
    'platform/powermac/rh_observe.[ch]', 'platform/powermac/ofw.[ch]',
    'tools/ppc/rh_observe_host.c', 'tools/tests/test_ppc_rh_observe.py',
    'arch/ppc/ppc_boot.h', 'platform/powermac/ppc_layout.h',
    'lib/kstring_c.c', 'lib/kstring.h', 'include/types.h', 'tools/tests/host32.py'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/tests'))
import host32


def main():
    source = (ROOT/'platform/powermac/rh_observe.c').read_text()
    ofw = (ROOT/'platform/powermac/ofw.c').read_text()
    with tempfile.TemporaryDirectory(prefix='rh-observe-') as td:
        work = Path(td)
        def evaluate(text, wrapper=ofw):
            src = work/'rh.c'; src.write_text(text)
            ofsrc = work/'ofw.c'; ofsrc.write_text(wrapper)
            exe = work/'host'
            subprocess.run(['gcc','-m32','-std=gnu11','-O2','-ffreestanding',
                '-fno-builtin','-fno-pie','-no-pie','-nostdlib','-static',
                '-D__cdecl=','-Iinclude','-Ilib','-Iarch/ppc','-Iplatform/powermac',
                'tools/ppc/rh_observe_host.c',str(ofsrc),
                'lib/kstring_c.c',str(src),'-o',str(exe)],cwd=ROOT,check=True)
            return host32.run([str(exe)], runner='qemu').returncode
        assert evaluate(source)==0
        print('GREEN absent/16/32bpp/overflow/stride/translate/continuity/guards/history/OF-off',flush=True)
        if '--mutate' in sys.argv:
            for name, old, new in [
                ('catch-untouched', 'if (no) args[3+ni] = initial;', '(void)initial;'),
                ('ci-status', 'call("call-method",ni+2,no+1,a,(cell)-1) != 0',
                 'call("call-method",ni+2,no+1,a,(cell)-1) == -1'),
            ]:
                assert ofw.count(old) == 1, name
                assert evaluate(source, ofw.replace(old, new)) == 1, name
                print('RED runtime exit=1 '+name, flush=True)
            for name, old, new in [
                ('color-write-order', '.foreground = 255, .background = 0', '.foreground = 0, .background = 255'),
                ('color-write-omitted', 'if (ofw_call_method("color!", output, 4, 0, black) ||\n            ofw_call_method("color!", output, 4, 0, white)) return;', 'if (0) return;'),
                ('color-order', 'a > b ? 0 : 255', 'a > b ? 255 : 0'),
                ('color-equal', 'if (a == b)', 'if (0)'),
                ('color-failed', '.rc = PM_RH_COLOR_FAILED', '.rc = PM_RH_COLOR_OK'),
                ('full-va-end', 'fb_va - offset > ~0U - rounded', '0'),
                ('full-pa-end', 'base > ~0U - rounded) return PM_RH_FULL_SIZE;', '0) return PM_RH_FULL_SIZE;'),
                ('full-bytes', 'full_fb.bytes = bytes;', 'full_fb.bytes = fb.bytes;'),
                ('full-pages', 'off < rounded; off += PM_RH_FB_PAGE) {\n        u32 next;\n        if (!translate(fb_mmu', 'off < PM_RH_FB_PAGE; off += PM_RH_FB_PAGE) {\n        u32 next;\n        if (!translate(fb_mmu'),
                ('paint', '    if (!pixels) return;\n    u32 cell', '    return;\n    u32 cell'),
                ('contiguous','!translate(mmu, va - offset + off, &next) || next != base + off','0'),
                ('history','reached |= 1U << stage;', 'reached = 1U << stage;'),
                ('stride','fb.stride < fb.width * (fb.depth / 8)', '0'),
                ('unknown-depth','(fb.depth != 8 && fb.depth != 16 && fb.depth != 32)', '0'),
            ]:
                assert source.count(old)==1, name
                assert evaluate(source.replace(old,new))==1, name
                print('RED runtime exit=1 '+name,flush=True)
    return 0


if __name__=='__main__':
    sys.exit(main())


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_ppc_rh_observe.py'] +
                           (['--mutate'] if ctx.mutate else []),
                           cwd=ctx.root, env=dict(os.environ))
