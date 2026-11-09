#!/usr/bin/env python3
"""H2/H3: actual public records and LE codec; --ppc is manual/PM only."""
CHECK = dict(name='record-layout', tier=1, needs=[], budget=15,
    covers=['tools/abi/record_layout_contract.c', 'tools/tests/test_record_layout.py',
            'tools/tests/host32.py', 'include/types.h', 'include/callconv.h',
            'drivers/pci/pci.c', 'include/pci_dev_contract.h',
            'userland/shell/cmd_pci.c', 'sdk/include/os32/**', 'lib/le_internal.h'])
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'tools/abi/record_layout_contract.c'
FIELDS = [('bus', 'bus'), ('dev', 'dev'), ('fn', 'fn'), ('vendor', 'vendor'),
          ('device', 'device'), ('class', 'cls'), ('subclass', 'subclass'),
          ('progif', 'progif'), ('header', 'header'), ('bar', 'bar'),
          ('irq_line', 'irq_line'), ('irq_pin', 'irq_pin'), ('command', 'command')]
OFFSETS = [0, 1, 2, 4, 6, 8, 9, 10, 11, 12, 36, 37, 38]
LE_BODY = '''
static int wire_ok(void) {
    const u8 vector[4] = {0x12, 0x34, 0x56, 0x78};
    u8 out[4];
    le_write32(out, 0x78563412UL);
    return le_read32(vector) == 0x78563412UL && le_read16(vector) == 0x3412 &&
        out[0] == 0x12 && out[1] == 0x34 && out[2] == 0x56 && out[3] == 0x78;
}
'''


def invoke(argv):
    return subprocess.run(argv, cwd=ROOT, check=True, capture_output=True)


def prepare(directory):
    text = (ROOT / 'userland/shell/cmd_pci.c').read_text()
    matches = re.findall(r'typedef struct \{[^{}]*\} PciDev;', text)
    assert len(matches) == 1, 'shell PciDev extraction must be unique'
    assertions = []
    for owner, name in [('drivers/pci/pci.c', 'pci_dev_is_40'),
                        ('userland/shell/cmd_pci.c', 'shell_pci_dev_is_40')]:
        found = re.findall(r'STATIC_ASSERT\([^;]*, ' + name + r'\);',
                           (ROOT / owner).read_text())
        assert len(found) == 1, (owner, name)
        assertions += found
    (directory / 'shell_pci_record.h').write_text(matches[0] +
        '\n#ifndef RECORD_LAYOUT_RUNTIME\n' + '\n'.join(assertions) + '\n#endif\n')
    # Keep the real relative include in le_internal.h; mutations use a mirror.
    (directory / 'lib').mkdir()
    (directory / 'include').mkdir()
    (directory / 'include/types.h').write_text((ROOT / 'include/types.h').read_text())
    (directory / 'lib/le_internal.h').write_text((ROOT / 'lib/le_internal.h').read_text())
    return ['-I' + str(directory), '-I' + str(ROOT / 'include'),
            '-I' + str(ROOT / 'sdk/include/os32')]


def layout_expressions():
    exprs = ['sizeof(GuiEvtWidget)', '__builtin_offsetof(GuiEvtWidget,value)',
             'sizeof(GuiPayload)', 'sizeof(GuiEvent)']
    exprs += ['__builtin_offsetof(GuiEvent,' + f + ')' for f in
              ('kind', 'sub', 'serial', 'window', 'payload')]
    exprs += ['sizeof(struct pci_dev)', 'sizeof(PciDev)']
    exprs += ['__builtin_offsetof(' + typ + ',' + pair[index] + ')'
              for typ, index in [('struct pci_dev', 0), ('PciDev', 1)] for pair in FIELDS]
    return exprs


def runtime(directory, includes):
    source = directory / 'runtime.c'
    source.write_text('#define RECORD_LAYOUT_RUNTIME\n#include "' + str(PROBE) +
        '"\n#include "lib/le_internal.h"\n' + LE_BODY +
        'const unsigned int actual_layout[] = {' + ','.join(layout_expressions()) + '};\n' + '''
void _start(void) {
    int code = (!RECORD_LAYOUT_MATCHES ? 1 : 0) | (!wire_ok() ? 2 : 0);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(actual_layout), "d"(sizeof(actual_layout)) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}
''')
    exe = directory / 'runtime'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-ffreestanding',
                  '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib', '-static',
                  *includes, '-Iarch/x86', str(source), '-o', str(exe)],
                 cwd=ROOT, check=True, capture_output=True)
    return host32.run([str(exe)], capture_output=True)


def measure(directory, includes, ppc):
    """Manual target object constants, never part of the syntax-only probe."""
    prefix = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'
    target = 'powerpc-elf' if ppc else 'i386-elf'
    cc = str(prefix / (target + '-gcc'))
    flags = ['-std=gnu11', '-ffreestanding', '-fsigned-char', '-fno-short-enums',
             '-Iarch/' + ('ppc' if ppc else 'x86'), *includes]
    if ppc:
        flags += ['-mcpu=7450', '-m32', '-mbig-endian', '-msoft-float',
                  '-mno-altivec', '-msdata=none']
    else:
        flags += ['-m32', '-march=i386']
    invoke([cc, *flags, '-fsyntax-only', str(PROBE)])
    # Both targets evaluate the real probe without emitting an object.
    wire = directory / 'wire.c'
    wire.write_text('#include "lib/le_internal.h"\n' + LE_BODY)
    invoke([cc, *flags, '-fsyntax-only', str(wire)])
    if not ppc:
        return
    # Object constants are exclusively the manual/PM PPC observation.
    source = directory / 'constants.c'
    exprs = layout_expressions()
    source.write_text('#include "' + str(PROBE) +
        '"\nconst unsigned int values[] __attribute__((section(".layout"))) = {' +
        ','.join(exprs) + '};\n')
    obj, raw = directory / 'constants.o', directory / 'constants.bin'
    invoke([cc, *flags, '-c', str(source), '-o', str(obj)])
    header = obj.read_bytes()[:20]
    assert header[:5] == b'\x7fELF\x01' and header[5] == (2 if ppc else 1)
    order = '>' if ppc else '<'
    assert struct.unpack(order + 'H', header[18:20])[0] == (20 if ppc else 3)
    invoke([str(prefix / (target + '-objcopy')), '-O', 'binary',
            '--only-section=.layout', str(obj), str(raw)])
    data = raw.read_bytes()
    values = list(struct.unpack(order + 'I' * len(exprs), data))
    expected = [6, 2, 8, 16, 0, 1, 2, 4, 8, 40, 40] + OFFSETS * 2
    assert values == expected, (values, expected)
    print(('PPC BE' if ppc else 'x86 LE') + ' measured GUI Widget=6/value@2, Payload=8; '
          'Event=16 fields@0,1,2,4,8; PCI kernel/shell=40 fields@' + str(OFFSETS))
    if ppc:
        wrong = expected.copy()
        wrong[1] = 4
        assert values != wrong, 'manual GUI value@4 expectation must be rejected'
        print('RED manual PPC GUI value@4 expectation; LE codec syntax PASS (no BE runtime)')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--ppc', action='store_true', help='manual/PM only, outside CHECK')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='record-layout-') as td:
        directory = Path(td)
        includes = prepare(directory)
        if args.ppc:
            measure(directory, includes, True)
            return
        measure(directory, includes, False)
        control = runtime(directory, includes)
        assert control.returncode == 0
        values = struct.unpack('<' + 'I' * len(layout_expressions()), control.stdout)
        print('x86 LE measured GUI Widget=%d/value@%d, Payload=%d; Event=%d fields@%s; '
              'PCI kernel/shell=%d/%d fields@%s/%s' %
              (*values[:4], values[4:9], *values[9:11], values[11:24], values[24:]))
        print('GREEN actual GUI asserts, PCI all fields, LE 12 34 56 78 -> 0x78563412')
        if args.mutate:
            shell = directory / 'shell_pci_record.h'
            original = shell.read_text()
            anchor = 'u32 bar[PCI_CFG_BAR_COUNT];'
            assert original.count(anchor) == 1
            shell.write_text(original.replace(anchor, 'u32 extra; ' + anchor))
            assert runtime(directory, includes).returncode == 1
            print('RED shell bar@12 -> 16 (compiled, runtime PCI oracle)')
            shell.write_text(original)
            codec = directory / 'lib/le_internal.h'
            original = codec.read_text()
            anchor = '(u32)p[0] | ((u32)p[1] << 8)'
            assert original.count(anchor) == 1
            codec.write_text(original.replace(anchor, '(u32)p[1] | ((u32)p[0] << 8)'))
            assert runtime(directory, includes).returncode == 2
            print('RED real le_read32 byte order (compiled, runtime LE vector)')
            codec.write_text(original)
            assert runtime(directory, includes).returncode == 0


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
