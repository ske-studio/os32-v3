"""Parse deployed HW-BASIC examples with the real parser, and check FAC order."""
CHECK = dict(name='hwb-scripts-host', tier=1, budget=4.0, needs=[], covers=[
    'userland/tests/hw/trident.hwb', 'userland/tests/hw/np21w_gdc.hwb',
    'userland/tests/hw/np21w_tvram.hwb', 'userland/tests/hw/np21w_nopci.hwb',
    'userland/tests/hw/np21w_loop.hwb', 'userland/tests/hw/np21w_poll.hwb',
    'userland/tests/hw/np21w_wait.hwb', 'userland/shell/hwb_log.c', 'userland/shell/hwb_probe.c', 'userland/shell/hwb_parse.c',
    'userland/shell/hwb.h', 'kdebug/probe_wire.h', 'kdebug/hwb_wire.h', 'include/types.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'userland/tests/hw/trident_mode.hwb', 'userland/shell/hwb_exec.c',
    'userland/shell/hwb_log.c', 'tools/tests/hwb_scripts_host.c',
    'userland/tests/hw/gf_regs.hwb', 'userland/tests/hw/hpt370_probe.hwb', 'userland/tests/hw/gf_palette.hwb', 'userland/tests/hw/tri_w32.hwb', 'userland/tests/hw/tri_burst.hwb', 'userland/tests/hw/tri_burst2.hwb', 'userland/tests/hw/tri_burst3.hwb', 'userland/tests/hw/tri_burst4.hwb', 'userland/tests/hw/tri_burst5.hwb', 'userland/tests/hw/tri_burst6.hwb', 'userland/tests/hw/pegc_burst.hwb'])
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import hwb_host

ROOT = hwb_host.ROOT
FILES = [
    'userland/tests/hw/trident.hwb', 'userland/tests/hw/np21w_gdc.hwb',
    'userland/tests/hw/np21w_tvram.hwb', 'userland/tests/hw/np21w_nopci.hwb',
    'userland/tests/hw/np21w_loop.hwb', 'userland/tests/hw/np21w_poll.hwb',
    'userland/tests/hw/np21w_wait.hwb', 'userland/tests/hw/trident_mode.hwb',
    'userland/tests/hw/gf_regs.hwb', 'userland/tests/hw/hpt370_probe.hwb', 'userland/tests/hw/gf_palette.hwb', 'userland/tests/hw/tri_w32.hwb', 'userland/tests/hw/tri_burst.hwb', 'userland/tests/hw/tri_burst2.hwb', 'userland/tests/hw/tri_burst3.hwb', 'userland/tests/hw/tri_burst4.hwb', 'userland/tests/hw/tri_burst5.hwb', 'userland/tests/hw/tri_burst6.hwb', 'userland/tests/hw/pegc_burst.hwb']
MUTATIONS = [('enable-fac', FILES[0], 'iowrite8 port 0xFAC 2',
              'iowrite8 port 0xFAC 0', 'enable-fac'),
             ('gdc-restore', FILES[1], 'iowrite8 port 0x68 0x0F',
              'iowrite8 port 0x68 0x0E', 'gdc-restore')]


def trial(mutation=None):
    sources = [(ROOT / p).read_text() for p in FILES]
    if mutation:
        _, path, old, new, _ = mutation
        index = FILES.index(path)
        if sources[index].count(old) != 1:
            raise RuntimeError('mutation anchor is not unique')
        sources[index] = sources[index].replace(old, new)
    arrays = ',\n'.join(json.dumps(s, ensure_ascii=True) for s in sources)
    code = r'''#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_log.c"
static struct hwb_program p;
static struct hwb_state state;
static const char *scripts[] = {''' + arrays + r'''};
void _start(void) {
    u32 n, line;
    HWB_TEST("syntax");
    for (n = 0; n < sizeof(scripts)/sizeof(scripts[0]); n++) {
        u32 len = 0; while (scripts[n][len]) len++;
        CHECK(hwb_parse(&p, scripts[n], len, &line) == HWB_OK);
        CHECK(line == 0);
    }
    n = 0; while (scripts[0][n]) n++;
    CHECK(hwb_parse(&p, scripts[0], n, &line) == HWB_OK);
    state.program = &p;
    u32 enable = HWB_NO_PC, disable = HWB_NO_PC;
    for (n = 0; n < p.label_count; n++) {
        if (hwbf_equal(p.labels[n].name, "enable")) enable = p.labels[n].pc;
        if (hwbf_equal(p.labels[n].name, "disable")) disable = p.labels[n].pc;
    }
    HWB_TEST("enable-fac");
    CHECK(enable != HWB_NO_PC && disable != HWB_NO_PC);
    struct hwb_instruction ins, last = {0}; u32 found = 0;
    for (u32 pc = enable; pc != HWB_NO_PC && pc < disable; pc = ins.next_pc) {
        CHECK(hwb_parse_instruction(&p, pc, &ins) == HWB_OK);
        if (ins.op == HWB_WRITE && ins.space == HW_IO) { last = ins; found = 1; }
        if (ins.op == HWB_END) break;
    }
    CHECK(found && last.width == 1 && last.address.kind == HWB_ADDR_PORT);
    u32 addr, value;
    CHECK(hwb_eval(&state, last.address.offset, &addr) == HWB_OK);
    CHECK(hwb_eval(&state, last.value, &value) == HWB_OK);
    /* trident_driver-1.4.0.c:727: outb(0xFAC, 0x02). */
    CHECK(addr == 0xFAC && value == 2);
    n = 0; while (scripts[1][n]) n++;
    CHECK(hwb_parse(&p, scripts[1], n, &line) == HWB_OK);
    HWB_TEST("gdc-restore");
    found = 0;
    for (u32 pc = 0; pc != HWB_NO_PC; pc = ins.next_pc) {
        CHECK(hwb_parse_instruction(&p, pc, &ins) == HWB_OK);
        if (ins.op == HWB_WRITE && ins.space == HW_IO) { last = ins; found = 1; }
        if (ins.op == HWB_END) break;
    }
    CHECK(found && last.width == 1 && last.address.kind == HWB_ADDR_PORT);
    CHECK(hwb_eval(&state, last.address.offset, &addr) == HWB_OK);
    CHECK(hwb_eval(&state, last.value, &value) == HWB_OK);
    /* HW_BASIC_DESIGN.md section 7: restore GDC display. */
    CHECK(addr == 0x68 && value == 0x0F);
    hwb_test_write("PASS scripts parse, enable FAC and restore GDC\n"); hwb_test_exit(0);
}'''
    with tempfile.TemporaryDirectory(prefix='hwb-scripts-') as d:
        source = Path(d) / 'fixture.c'
        source.write_text(code)
        exe = Path(d) / 'host'
        hwb_host.build(ROOT, source, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or f'FAIL:{mutation[4]}:' not in result.stdout:
                raise RuntimeError('mutation did not produce expected runtime RED')
            print(f'{mutation[0]}: RED runtime', flush=True)
        elif result.returncode:
            raise RuntimeError('normal control failed')
        else:
            hwb_host.build(ROOT, source, Path(d) / 'target.o', cross=True)
            print('GREEN host ILP32; i386-elf compile PASS', flush=True)



MODE = 'userland/tests/hw/trident_mode.hwb'
EXEC = 'userland/shell/hwb_exec.c'
EXEC_MUTATIONS = [
    ('state-save-order', MODE, ' call state_save\n # PC-98 PCI', ' # PC-98 PCI', 'state-save-restore'),
    ('palette-fallback', MODE, ' let save[767] = 0', ' let save[767] = 1', 'palette-default'),

    ('state-restore-omit', MODE, ' call state_restore\n', ' log "restore omitted"\n', 'state-save-restore'),
    ('state-restore-order', MODE, ' mmiowrite8 v + 0x43C8 save[847]\n mmiowrite8 v + 0x43C9 save[848]',
     ' mmiowrite8 v + 0x43C9 save[848]\n mmiowrite8 v + 0x43C8 save[847]', 'state-save-restore'),
    ('state-save-omit', MODE, ' call state_save\n', ' log "save omitted"\n', 'mode'),

    ('array-read-store', EXEC, 'if (element) { *element = a; return HWB_OK; }\n        return he_assign(s, i->name, HWB_SCALAR, a, 0, 0);\n    case HWB_PRINT:',
     'if (element) { *element = 0; return HWB_OK; }\n        return he_assign(s, i->name, HWB_SCALAR, a, 0, 0);\n    case HWB_PRINT:', 'array-save-restore'),
    ('fill-address', EXEC, 'r->addr + n * i->width, &a);', 'r->addr, &a);', 'fill-values'),
    ('fill-count', EXEC, 'n < r->count; n++', 'n + 1U < r->count; n++', 'fill-values'),
    ('fill-point', EXEC, 'rc = he_point(s, 0); if (rc) return rc;\n            a = r->value;', 'rc = HWB_OK; if (rc) return rc;\n            a = r->value;', 'fill-abort'),
    ('fill-precheck', EXEC, 'r.count - 1U >', 'r.count - r.count >', 'fill-invalid'),
    ('fill-log', 'userland/shell/hwb_log.c', 'if (r->op == HWB_FILL) { str(&b, " count="); dec(&b, r->count); str(&b, " done=");', 'if (r->op == HWB_FILL) { str(&b, " count="); dec(&b, r->count); str(&b, " lost=");', 'fill-values'),
    ('mode-cap-four', MODE, ' if t == 15 goto cap_four\n', '', 'capacity'),
    ('mode-fac', MODE, 'iowrite8 port 0xFAC 0', 'iowrite8 port 0xFAC 2', 'mode'),
    ('mode-cr03', MODE, '0x03 | ((0x80) << 8)', '0x03 | ((0x82) << 8)', 'mode'),
    ('mode-cr05', MODE, '0x05 | ((0x1E) << 8)', '0x05 | ((0x9E) << 8)', 'mode'),
    ('mode-cr3a', MODE, 'mmiowrite16 v + 0x3D4 0x3A | ((0) << 8)', 'log "CR3A omitted"', 'mode'),
    ('mode-pitch', MODE, '0x13 | ((0x50) << 8)\n # Column 6', '0x13 | ((0x51) << 8)\n # Column 6', 'mode'),
    ('mode-pll', MODE, 'mmiowrite8 v + 0x43C9 b', 'mmiowrite8 v + 0x43C9 a', 'mode'),
    ('mode-ar', MODE, 'mmiowrite8 v + 0x3C0 0x41\n mmioread8 v + 0x3DA', 'mmiowrite8 v + 0x3C0 0x40\n mmioread8 v + 0x3DA', 'mode'),
    ('pc98-skip', MODE, ' call pc98_init\n', ' log "skip init"\n', 'pc98-init'),
    ('pc98-mclk', MODE, 'mmiowrite8 v + 0x43C6 0xAF', 'mmiowrite8 v + 0x43C6 0xAE', 'pc98-init'),
    ('mode-enable-order', MODE, ' call enable\n # mode=4:', ' # mode=4:', 'mode'),
    ('mode-cr39-burst', MODE, '((t & 0xF9) | 6)', '(t & 0xF9)', 'mode'),
    ('mode-unprotect', MODE, 'call protect_off\n call enable\n # mode=4:', 'log "unprotect postponed"\n call enable\n # mode=4:', 'mode'),
]


def execution_trial(mutation=None):
    fixture = (ROOT / 'tools/tests/hwb_scripts_host.c').read_text()
    scripts = [(ROOT / p).read_text() for p in FILES]
    products = {p: (ROOT / p).read_text() for p in [
        'userland/shell/hwb_parse.c', EXEC, 'userland/shell/hwb_log.c']}
    if mutation:
        _, path, before, after, _ = mutation
        original = (ROOT / path).read_text()
        if original.count(before) != 1:
            raise RuntimeError('mutation anchor is not unique: ' + mutation[0])
        if path in FILES:
            scripts[FILES.index(path)] = original.replace(before, after)
            if mutation[0] == 'state-save-order':
                scripts[FILES.index(path)] = scripts[FILES.index(path)].replace(
                    ' call pc98_init\n', ' call pc98_init\n call state_save\n', 1)
            if mutation[0] == 'mode-enable-order':
                scripts[FILES.index(path)] = scripts[FILES.index(path)].replace(
                    ' wait us 5000000', ' call enable\n wait us 5000000', 1)
        else:
            products[path] = original.replace(before, after)
    # Inline product TUs so mutations stay isolated and execute the real code.
    for path, content in products.items():
        fixture = fixture.replace('#include "' + path + '"', content.replace(
            '#include "hwb.h"', '#include "userland/shell/hwb.h"'))
    arrays = ',\n'.join(json.dumps(s, ensure_ascii=True) for s in scripts)
    fixture = fixture.replace('/* SCRIPTS_DATA */',
                              'static const char *scripts[] = {' + arrays + '};')
    with tempfile.TemporaryDirectory(prefix='hwb-mode-') as d:
        source = Path(d) / 'fixture.c'
        source.write_text(fixture)
        exe = Path(d) / 'host'
        hwb_host.build(ROOT, source, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:' + mutation[4] + ':' not in result.stdout:
                raise RuntimeError('mutation did not produce expected runtime RED: ' + mutation[0])
            print(mutation[0] + ': RED runtime', flush=True)
        elif result.returncode:
            raise RuntimeError('execution control failed')
        else:
            hwb_host.build(ROOT, source, Path(d) / 'target.o', cross=True)
            print('GREEN real executor mode/fill ILP32; i386-elf compile PASS', flush=True)


def main():
    trial()
    execution_trial()
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS:
            trial(mutation)
        for mutation in EXEC_MUTATIONS:
            execution_trial(mutation)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_scripts.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
