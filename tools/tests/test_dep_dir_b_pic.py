"""L12 fixed baseline traces; runtime never reads git or updates expectations.

Capture: 12b7e2fe idt.c's four PIC functions + original irq.c, linked to
this same harness/shim and irq_math.c. Timer pending's two I/O operations
are the existing ktime.c IRR read expression, exposed only for the harness.
Input reads are armed explicitly in dep_dir_b_pic_host.c; wrong/extra/missing
reads fail. Capture once with build(work, baseline=(idt_text, irq_text)).
"""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_dep_dir_b_io import trace

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/dep_dir_b_l12_baseline.json'
SOURCES = ('kernel/irq.c', 'kernel/irq_math.c', 'platform/pc98/pic.c')
HEADERS = ('kernel/platform_irq.h', 'kernel/irq.h', 'kernel/irq_math.h',
           'kernel/idt.h', 'kernel/gdt.h', 'kernel/pit_math.h')
MUTATIONS = (
    ('platform/pc98/pic.c', 'outp(PIC1_DATA, ICW3_MASTER);', 'outp(PIC1_DATA, ICW3_SLAVE);'),
    ('platform/pc98/pic.c', '    irq_restore(flags);', '    /* restore removed */'),
    ('platform/pc98/pic.c', '    outp(PIC2_CMD, OCW3_IRR);\n    return', '    /* IRR restore removed */\n    return'),
    ('kernel/irq.c', '        platform_irq_eoi_master();', '        pic_eoi(irq);'),
)


def build(work, mutation=None, baseline=None):
    # B5: identical tree and include paths for normal and mutation builds.
    for rel in (*SOURCES, *HEADERS):
        text = (ROOT / rel).read_text()
        if mutation and mutation[0] == rel:
            assert mutation[1] in text
            text = text.replace(mutation[1], mutation[2], 1)
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    if baseline is not None:
        idt, irq = baseline
        funcs = []
        for name in ('pic_init', 'irq_enable', 'irq_disable', 'pic_eoi'):
            m = re.search(r'^void ' + name + r'\([^\n]*\)\n\{.*?^\}', idt, re.M | re.S)
            assert m, name
            funcs.append(m[0])
        (work / 'platform/pc98/pic.c').write_text(
            '#include "idt.h"\n#include "io.h"\n' + '\n'.join(funcs) +
            '\nint platform_irq_timer_pending(void) {\n'
            'outp(PIC1_CMD, OCW3_IRR);\n'
            'return (inp(PIC1_CMD) & 0x01) ? 1 : 0;\n}\n')
        (work / 'kernel/irq.c').write_text(irq)
    (work / 'io.h').write_text((ROOT / 'tools/tests/dep_dir_b_io_shim.h').read_text())
    exe = work / 'pic-test'
    subprocess.run([
        'gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
        '-static', '-no-pie', '-D__cdecl=', '-I' + str(work),
        '-I' + str(work / 'kernel'), '-I' + str(ROOT / 'include'),
        str(ROOT / 'tools/tests/dep_dir_b_pic_host.c'),
        *(str(work / rel) for rel in SOURCES), '-o', str(exe)], check=True)
    return exe


def verify(mutation=None):
    expected = json.loads(FIXTURE.read_text())
    with tempfile.TemporaryDirectory(prefix='dep-dir-b-pic-') as td:
        actual = trace(build(Path(td), mutation))
    assert actual == expected['trace'], actual
    print('PASS L12 PIC init12 / all masks IF=0,1 / C irq_finish / pending / registration-quarantine', flush=True)


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
