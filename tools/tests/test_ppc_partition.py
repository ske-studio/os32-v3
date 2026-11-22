"""APM full-table validation and partition-relative I/O on a disposable disk."""
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

root = Path(__file__).resolve().parents[2]
view = root / 'platform/powermac/partition_view.c'
apm = root / 'platform/powermac/apm.c'
cases = [('normal', view, None, None, 0)]
if '--mutate' in sys.argv:
    cases += [('start', view, 'first + offset', 'offset', 5),
              ('last', view, 'count > dev->total_sects - offset', 'count >= dev->total_sects - offset', 6),
              ('early-write', view, '!writable || dev != &view', 'dev != &view', 12),
              ('type', apm, 'i + 1 == number && kstrncmp', '0 && kstrncmp', 11),
              ('magic', apm, 'return EXT2_ERR_MAGIC;', 'block[0] = 0;', 10)]
with tempfile.TemporaryDirectory() as tmp:
    for name, target, old, new, expected in cases:
        source = target.read_text()
        if old:
            assert source.count(old) == 1
            source = source.replace(old, new)
        path = Path(tmp) / target.name
        path.write_text(source)
        exe = Path(tmp) / 'host'
        subprocess.run(['cc', '-m32', '-nostdinc', '-nostdlib', '-static', '-ffreestanding', '-fno-pie', '-D__cdecl=', '-I.', '-Iinclude', '-Iplatform/powermac',
                        '-Ikernel/dev', '-Ilib', '-Isdk/include/os32', '-Iarch/ppc',
                        'tools/tests/ppc_partition_host.c', apm if target == view else view,
                        'lib/kstring_c.c', path, '-o', exe], cwd=root, check=True)
        rc = host32.run([exe]).returncode
        assert rc == expected, (name, rc, expected)
        print(name, 'RED' if rc else 'OK', rc)
