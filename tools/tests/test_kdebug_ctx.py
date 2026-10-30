#!/usr/bin/env python3
"""S3: compiled exception call graph, context marks, and slot 239 OFF/ON.

The graph is compiled without inlining so every transitive edge stays visible.
Indirect calls in the recorder's two I/O dispatchers conservatively reach every
function in static I/O initializers (hardware and dry ops). Recorder table
assignments are checked too, so a new dynamic callback cannot evade the graph.
Other indirect call sites are rejected and require an explicit model update.
--mutate checks direct, transitive, callback, missing-mark and missing-slot REDs.
"""
import copy
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_kdebug_imports as gen
import mkmod

FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']
INCLUDES = ['-I' + str(ROOT / d) for d in
            ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
             'sdk/include', 'sdk/include/os32', 'exec', 'gfx', 'drivers', 'kapi', 'kdebug')]


def graph(folder, sources):
    # Only these two fixed tables can reach the indirect I/O dispatchers.
    assignments = re.findall(r'\bgcap_ops\s*=\s*([^;]+);', sources['v86_gcap.c'])
    allowed = {'0', '(mode == V86G_MODE_SELFTEST) ? &gcap_dry_ops : &gcap_hw_ops'}
    if not assignments or set(assignments) - allowed:
        raise ValueError('unmodeled recorder callback assignment')
    functions, addresses = {}, set()
    cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
    for name, source in sources.items():
        path = folder / name
        path.write_text(source)
        asm = path.with_suffix('.s')
        subprocess.run([str(cross), *FLAGS, '-O0', '-fno-inline', *INCLUDES,
                        '-S', str(path), '-o', str(asm)], check=True)
        text = asm.read_text()
        for match in re.finditer(r'^\s*\.type\s+(\w+),\s*@function\n(.*?)^\s*\.size\s+\1,', text, re.M | re.S):
            symbol, body = match.groups()
            calls = re.findall(r'^\s*call\s+([^\n]+)', body, re.M)
            functions[symbol] = calls
        addresses.update(re.findall(r'^\s*\.long\s+(\w+)\s*$', text, re.M))
    return functions, addresses


def audit(functions, addresses, entries, main):
    imports = {e['name']: e['context_safe'] for e in entries}
    roots = []
    for slot in mkmod.OPS_NAMES[6:15]:
        match = re.search(r'\.' + slot + r'\s*=\s*(\w+)', main)
        if not match or match[1] not in functions:
            raise ValueError('missing exception slot: ' + slot)
        roots.append(match[1])
    callbacks = addresses & (functions.keys() | imports.keys())
    seen, pending = set(), roots[:]
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        if name in imports:
            if not imports[name]:
                raise ValueError('unsafe import: ' + name)
            continue
        if name not in functions:
            raise ValueError('unresolved call: ' + name)
        for target in functions[name]:
            if target.startswith('*'):
                if name not in ('v86g_pass_in', 'v86g_pass_out') or not callbacks:
                    raise ValueError('unmodeled indirect call: ' + name)
                pending.extend(callbacks)
            else:
                pending.append(target.strip())
    return seen


def slot239(folder):
    source = folder / 'slot239.c'
    source.write_text('''#include "kernel/module/module.h"
#include "kernel/v86/v86_gcap.h"
struct kdebug kdebug;
static int calls;
static int capture(int mode, V86Gcap *out) {
    calls++;
    return mode == V86G_MODE_SELFTEST && !out ? 123 : -1;
}
void _start(void) {
    int rc = v86_gdc_capture(V86G_MODE_SELFTEST, 0) != OS32_ERR_NOSYS;
    kdebug.ops.gcap_capture = capture;
    rc |= v86_gdc_capture(V86G_MODE_SELFTEST, 0) != 123 || calls != 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
''')
    exe = folder / 'slot239'
    subprocess.run(['gcc', *FLAGS, *INCLUDES, '-nostdlib', '-static', '-no-pie',
                    str(source), str(ROOT / 'kernel/v86/v86_gcap.c'), '-o', str(exe)], check=True)
    for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
        assert host32.run([str(exe)], runner=runner).returncode == 0
    print('PASS slot 239: OFF NOSYS, ON dispatch with unchanged arguments')


def main():
    entries = gen.load_def()
    sources = {n: (ROOT / 'kdebug' / n).read_text() for n in ('v86_gcap.c', 'v86_gcap_math.c')}
    initializer = (ROOT / 'kdebug/kdebug_main.c').read_text()
    with tempfile.TemporaryDirectory(prefix='kdebug-ctx-') as td:
        folder = Path(td)
        functions, addresses = graph(folder, sources)
        seen = audit(functions, addresses, entries, initializer)
        print('PASS exception slots 7..15: transitive calls and I/O callbacks; imports=' +
              ','.join(sorted(seen & {e['name'] for e in entries})))
        slot239(folder)
        if '--mutate' in sys.argv:
            cases = [
                ('direct', 'v86_gcap.c', 'return v86_gcap_rec != 0;',
                 'kfree(0); return v86_gcap_rec != 0;'),
                ('transitive', 'v86_gcap_math.c', 'int v86g_port_passed(unsigned int port)\n{',
                 'extern void kfree(void *);\nint v86g_port_passed(unsigned int port)\n{\n    kfree(0);'),
                ('callback', 'v86_gcap.c', 'return inp(port) & 0xFFU;',
                 'kfree(0); return inp(port) & 0xFFU;'),
            ]
            for label, name, old, new in cases:
                mutant = dict(sources)
                assert old in mutant[name]
                mutant[name] = mutant[name].replace(old, new)
                f, a = graph(folder, mutant)
                try:
                    audit(f, a, entries, initializer)
                except ValueError as exc:
                    assert str(exc) == 'unsafe import: kfree', exc
                    print('MUTATE', label, 'RED:', exc)
                else:
                    raise AssertionError(label)
            changed = copy.deepcopy(entries)
            for e in changed:
                if e['name'] == 'kdi_get_v86_gp_cs':
                    e['context_safe'] = False
            for label, rows, init in (
                ('context-mark', changed, initializer),
                ('slot', entries, initializer.replace('.gcap_detach = v86_gcap_detach', '.gcap_detach = 0')),
            ):
                try:
                    audit(functions, addresses, rows, init)
                except ValueError as exc:
                    print('MUTATE', label, 'RED:', exc)
                else:
                    raise AssertionError(label)
    return 0


if __name__ == '__main__':
    sys.exit(main())
