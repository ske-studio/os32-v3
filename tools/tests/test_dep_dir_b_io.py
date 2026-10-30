"""L11 fixed baseline I/O trace; no git or fixture generation at test time.

Fixture capture (once, before changing sys.c): obtain kernel/sys.c with
`git show 12b7e2fe:kernel/sys.c`, pass that text as build(..., baseline=text),
then trace() for TEST_IF=0 and 1. Store those rows, SHA and empty read input
list in dep_dir_b_l11_baseline.json. The same C harness/shim records both
versions. 12b7e2fe and step base 6e880821 have identical kernel/sys.c.
The first reboot halt returns; the second terminates the host process.
"""
from kernel_includes import include_flags
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('kernel/sys.c', 'platform/pc98/system.c')
HEADERS = ('include/platform_sys.h',)
FIXTURE = ROOT / 'tools/tests/dep_dir_b_l11_baseline.json'
MUTATIONS = (
    ('platform/pc98/system.c',
     '    outp(SYSPORT_C_BSR, BSR_SHUT0_SET);\n    outp(SYSPORT_C_BSR, BSR_SHUT1_SET);',
     '    outp(SYSPORT_C_BSR, BSR_SHUT1_SET);\n    outp(SYSPORT_C_BSR, BSR_SHUT0_SET);'),
    ('platform/pc98/system.c', 'outp(SYSPORT_C_BSR, BSR_BUZ_ON);',
     'outp(SYSPORT_C_BSR, BSR_BUZ_OFF);'),
)


def control_functions(text):
    result = []
    for name in ('sys_reboot', 'sys_halt', 'buz_on', 'buz_off'):
        match = re.search(r'^void ' + name + r'\(void\)\n\{.*?^\}', text, re.M | re.S)
        assert match, name
        result.append(match[0])
    return '\n'.join(result) + '\n'


def build(work, enabled, mutation=None, baseline=None):
    # Normal and mutation builds share copies, headers, and include paths (B5).
    for rel in (() if baseline is not None else (*SOURCES, *HEADERS)):
        text = (ROOT / rel).read_text()
        if mutation and mutation[0] == rel:
            assert text.count(mutation[1]) == 1
            text = text.replace(mutation[1], mutation[2], 1)
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    (work / 'io.h').write_text((ROOT / 'tools/tests/dep_dir_b_io_shim.h').read_text())
    source = baseline if baseline is not None else (work / 'kernel/sys.c').read_text()
    includes = '' if baseline is not None else '#include "platform_sys.h"\n'
    (work / 'sys_control.inc').write_text(includes + control_functions(source))
    exe = work / 'io-test'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
           '-static', '-no-pie', f'-DTEST_IF={enabled}', '-include', 'pc98.h',
           '-I' + str(work), *include_flags(work), '-I' + str(ROOT / 'include'),
           str(ROOT / 'tools/tests/dep_dir_b_io_host.c')]
    if baseline is None:
        cmd.append(str(work / 'platform/pc98/system.c'))
    subprocess.run([*cmd, '-o', str(exe)], check=True)
    return exe


def trace(exe):
    out = host32.run([str(exe)], runner='qemu', capture_output=True, timeout=10)
    assert out.returncode == 0, (out.returncode, out.stderr)
    assert len(out.stdout) % 16 == 0
    return [[chr(op), port, value, flags]
            for op, port, value, flags in struct.iter_unpack('<4I', out.stdout)]


def verify(mutation=None):
    expected = json.loads(FIXTURE.read_text())
    for enabled in (0, 1):
        with tempfile.TemporaryDirectory(prefix='dep-dir-b-io-') as td:
            actual = trace(build(Path(td), enabled, mutation))
        assert actual == expected['traces'][str(enabled)], (enabled, actual)
    print('PASS L11 reset/beep/halt order, IF=0/1, no reads or waits', flush=True)


if __name__ == '__main__':
    verify()
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS:
            try:
                verify(mutation)
            except AssertionError:
                print('RED', mutation[1], flush=True)
            else:
                raise AssertionError('mutation escaped: ' + mutation[1])
