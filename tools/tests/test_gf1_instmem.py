"""NV44 instance allocation and aperture access."""
CHECK = dict(name='gf1-instmem-host', tier=1,
             covers=['hwdev/gf/vram_layout.h', 'hwdev/gf/instmem.c', 'hwdev/gf/foundation.h',
                     'hwdev/gf/internal.h', 'tools/tests/gf1_instmem_host.c',
                     'tools/tests/test_gf1_instmem.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py'], needs=[], budget=30.0)
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix='gf1-instmem-') as td:
        exe = Path(td) / 'host'
        source = (ROOT / 'hwdev/gf/instmem.c').read_text()
        layout = (ROOT / 'hwdev/gf/vram_layout.h').read_text()
        def build(text, header=layout):
            header = header.replace('"../../include/', '"' + str(ROOT / 'include') + '/')
            (Path(td) / 'vram_layout.h').write_text(header)
            text = '#include "vram_layout.h"\n' + text
            path = Path(td) / 'instmem.c'
            path.write_text(text.replace('#include "foundation.h"',
                '#include "' + str(ROOT / 'hwdev/gf/foundation.h') + '"'))
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie', *include_flags(ROOT),
                            str(ROOT / 'tools/tests/gf1_instmem_host.c'), str(path), '-o', str(exe)], check=True)
        build(source)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0
        print('PASS instmem allocation and access', flush=True)
        if '--mutate' in sys.argv:
            mutations = [
                ('aperture-relative address', '*v = *(volatile u32 *)(f->bar2 + obj->off + off);', '*v = *(volatile u32 *)(f->bar2 + obj->off + off + f->imem.vram_bytes - f->imem.reserved);'),
                ('zero first word only', 'i < bytes / 4', 'i < 1'),
                ('rounding down', 'bytes = (bytes + 3) & ~3U;', 'bytes = bytes & ~3U;'),
                ('zero beyond allocation', 'i < bytes / 4', 'i <= bytes / 4'),
                ('reject exact end', 'bytes > f->imem.aperture_bytes - pos', 'bytes >= f->imem.aperture_bytes - pos'),
                ('allow allocation beyond end', 'bytes > f->imem.aperture_bytes - pos', 'bytes > f->imem.aperture_bytes - pos + 4'),
                ('reject last word', 'obj->size - off < 4', 'obj->size - off <= 4'),
                ('allow access beyond end', 'obj->size - off < 4', 'obj->size - off == 1'),
                ('wrong first BAR', 'f->io.opaque, 0, GF_VRAM_SIZE_REG, 4', 'f->io.opaque, 1, GF_VRAM_SIZE_REG, 4'),
                ('wrong second BAR', 'f->io.opaque, 0, GF_VS_REG, 4', 'f->io.opaque, 1, GF_VS_REG, 4'),
                ('wrong first width', 'f->io.opaque, 0, GF_VRAM_SIZE_REG, 4', 'f->io.opaque, 0, GF_VRAM_SIZE_REG, 2'),
                ('wrong second width', 'f->io.opaque, 0, GF_VS_REG, 4', 'f->io.opaque, 0, GF_VS_REG, 2'),
                ('wrong first register', 'GF_VRAM_SIZE_REG 0x10020cU', 'GF_VRAM_SIZE_REG 0x100208U'),
                ('wrong second register', 'GF_VS_REG 0x1540U', 'GF_VS_REG 0x1544U'),
                ('wrong register order', '0, GF_VRAM_SIZE_REG, 4, &v', '0, GF_VS_REG, 4, &v'),
                ('reservation constant', 'GF_VS_CONTEXT_BYTES 0x4980U', 'GF_VS_CONTEXT_BYTES 0x4981U'),
                ('fixed size constant', 'RAMHT_BYTES = 0x8000', 'RAMHT_BYTES = 0x8004'),
            ]
            for name, old, new in mutations:
                target = layout if old in layout else source
                assert target.count(old) == 1, name
                if target == layout:
                    build(source, layout.replace(old, new, 1))
                else:
                    build(source.replace(old, new, 1))
                assert host32.run([str(exe)], runner='qemu').returncode != 0, name
                print('MUTATE RED ' + name, flush=True)

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_instmem.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
