#!/usr/bin/env python3
"""G8 GART bounds and surface DMA selection."""
CHECK = dict(name='gf-g8-host', tier=1, needs=[], budget=5,
    covers=['hwdev/gf/accel_surface.c', 'hwdev/gf/accel.h',
            'tools/tests/gf_g8_host.c', 'tools/tests/test_gf_g8.py',
            'tools/tests/gf_accel_host.h', 'tools/tests/gf_command_runtime_host.h',
            'tools/tests/recover_gf_dev_stub.c', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    product = work / 'accel_surface.c'
    product.write_text(source)
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
        '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-nostdlib', '-static', '-no-pie', *include_flags(ROOT), '-I' + str(ROOT),
        '-iquote', str(ROOT / 'hwdev/gf'), str(ROOT / 'tools/tests/gf_g8_host.c'),
        str(product), str(ROOT / 'tools/tests/recover_gf_dev_stub.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'hwdev/gf/accel_surface.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, source) == 0
        if args.mutate:
            for old, new in [
                ('if (s->dma == GF3_HANDLE_GART_DMA)', 'if (0)'),
                ('src->dma ? src->dma : GF3_HANDLE_VRAM_DMA', 'GF3_HANDLE_VRAM_DMA'),
                ('dst->dma ? dst->dma : GF3_HANDLE_VRAM_DMA', 'GF3_HANDLE_VRAM_DMA'),
            ]:
                assert source.count(old) == 1
                rc = observe(work, source.replace(old, new))
                assert rc != 0
                print(f'RED runtime rc={rc}: {old}')
    print('PASS G8')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
