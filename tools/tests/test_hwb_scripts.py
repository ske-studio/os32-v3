"""Parse deployed HW-BASIC examples with the real parser, and check FAC order."""
CHECK = dict(name='hwb-scripts-host', tier=1, budget=4.0, needs=[], covers=[
    'userland/tests/hw/trident.hwb', 'userland/tests/hw/np21w_gdc.hwb',
    'userland/tests/hw/np21w_tvram.hwb', 'userland/tests/hw/np21w_nopci.hwb',
    'userland/tests/hw/np21w_loop.hwb', 'userland/tests/hw/np21w_poll.hwb',
    'userland/tests/hw/np21w_wait.hwb', 'userland/shell/hwb_parse.c',
    'userland/shell/hwb.h', 'kdebug/hwb_wire.h', 'include/types.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py'])
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import hwb_host

ROOT = hwb_host.ROOT
FILES = CHECK['covers'][:7]
MUTATIONS = [('enable-fac', FILES[0], 'iowrite8 port 0xFAC 2',
              'iowrite8 port 0xFAC 0', 'enable-fac')]


def trial(mutate=False):
    sources = [(ROOT / p).read_text() for p in FILES]
    if mutate:
        _, _, old, new, _ = MUTATIONS[0]
        if sources[0].count(old) != 1:
            raise RuntimeError('mutation anchor is not unique')
        sources[0] = sources[0].replace(old, new)
    arrays = ',\n'.join(json.dumps(s, ensure_ascii=True) for s in sources)
    code = r'''#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
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
    hwb_test_write("PASS scripts parse and enable FAC\n"); hwb_test_exit(0);
}'''
    with tempfile.TemporaryDirectory(prefix='hwb-scripts-') as d:
        source = Path(d) / 'fixture.c'
        source.write_text(code)
        exe = Path(d) / 'host'
        hwb_host.build(ROOT, source, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutate:
            if result.returncode != 1 or 'FAIL:enable-fac:' not in result.stdout:
                raise RuntimeError('mutation did not produce expected runtime RED')
            print('enable-fac: RED runtime', flush=True)
        elif result.returncode:
            raise RuntimeError('normal control failed')
        else:
            hwb_host.build(ROOT, source, Path(d) / 'target.o', cross=True)
            print('GREEN host ILP32; i386-elf compile PASS', flush=True)


def main():
    trial()
    if '--mutate' in sys.argv:
        trial(True)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_scripts.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
