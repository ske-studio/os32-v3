"""APM full-table validation and partition-relative I/O on a disposable disk."""
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

root = Path(__file__).resolve().parents[2]
view = root / 'platform/powermac/partition_view.c'
cases = [('normal', None, None, 0)]
if '--mutate' in sys.argv:
    cases += [('start', 'first + offset', 'offset', 5),
              ('last', 'count > dev->total_sects - offset', 'count >= dev->total_sects - offset', 6)]
with tempfile.TemporaryDirectory() as tmp:
    for name, old, new, expected in cases:
        source = view.read_text()
        if old:
            assert source.count(old) == 1
            source = source.replace(old, new)
        path = Path(tmp) / 'view.c'
        path.write_text(source)
        exe = Path(tmp) / 'host'
        subprocess.run(['cc', '-m32', '-nostdinc', '-nostdlib', '-static', '-ffreestanding', '-fno-pie', '-D__cdecl=', '-I.', '-Iinclude', '-Iplatform/powermac',
                        '-Ikernel/dev', '-Ilib', '-Isdk/include/os32', '-Iarch/ppc',
                        'tools/tests/ppc_partition_host.c', 'platform/powermac/apm.c',
                        'lib/kstring_c.c', path, '-o', exe], cwd=root, check=True)
        rc = host32.run([exe]).returncode
        assert rc == expected, (name, rc, expected)
        print(name, 'RED' if rc else 'OK', rc)
