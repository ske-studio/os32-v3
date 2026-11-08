"""C0 lifecycle order, CLI boundaries, rollback and ownership on failure.

B components are contract fakes here; their own host tests verify hardware
operations. The production C0 translation unit is also cross-compiled unchanged.
"""
CHECK = dict(name='gf1-foundation-host', tier=1,
    covers=['hwdev/gf/foundation.c', 'hwdev/gf/*.h', 'hwdev/gf/display/*.h',
            'hwdev/gf/master.c', 'hwdev/gf/mc.c', 'hwdev/gf/instmem.c',
            'hwdev/gf/instmem_map.c', 'hwdev/gf/irq.c', 'hwdev/gf/engine_stop.c',
            'hwdev/gf_load_state.h', 'kdebug/recover.h', 'kdebug/gfinit_decode.h',
            'include/types.h', 'include/io.h', 'include/video_service.h',
            'include/pci_dev_contract.h', 'arch/x86/arch_io.h',
            'drivers/video/geforce/geforce_tick.h',
            'drivers/video/geforce/geforce_tick.c',
            'tools/tests/gf1_foundation_host.c', 'tools/tests/test_gf1_foundation.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.3)

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'hwdev/gf/foundation.c'


def main():
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', *include_flags(ROOT), '-I' + str(ROOT),
             '-I' + str(SOURCE.parent)]
    with tempfile.TemporaryDirectory(prefix='gf1-foundation-') as td:
        td = Path(td)
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *flags, '-D__KERNEL_BUILD__', '-c',
                        str(SOURCE), '-o', str(td / 'foundation.o')], check=True)
        exe = td / 'host'

        def build(source):
            subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie',
                            '-DGF1_FOUNDATION_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/gf1_foundation_host.c'),
                            '-o', str(exe)], check=True)

        build(SOURCE)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner)
            assert result.returncode == 0, (runner, result.returncode)
            print('PASS C0 start/stop/restart/CLI/rollback/ownership (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            mutants = [
                ('unsigned flags = irq_save();\n    f->revoked = true;',
                 'unsigned flags = 0;\n    f->revoked = true;', 'revoke CLI omitted'),
                ('if (!rc && !f->revoked) rc = mask_sources(f);',
                 'if (!rc) rc = mask_sources(f);', 'revoked MMIO guard omitted'),
                ('int unmap_rc = f->mapped ? gf_instmem_unmap(f) : GF1_OK;',
                 'int unmap_rc = GF1_OK;', 'revoked BAR2 unmap omitted'),
                ('if (f->revoked) return f->last_rc = GF1_EPCI;',
                 '/* revoked restart permitted */', 'revoked restart permitted'),
                ('f->revoked = true;', '/* revoke omitted */', 'revoke sticky state omitted'),
                ('f->running = false; /* revoke publication */', '/* still running */', 'revoke running retained'),
                ('f->expected_pmc = stopped_pmc;', '/* expected PMC retained */',
                 'stopped PMC publication omitted'),
                ('f->expected_pmc = stopped_pmc;', 'f->expected_pmc = 0xffffffffU;',
                 'stopped PMC forced all-on before MC'),
                ('return f->last_rc = GF1_EIO;', 'stopped_pmc = f->expected_pmc;',
                 'stopped PMC read failure ignored'),
                ('int rc = gf_master_enable(f);', 'int rc = 0;', 'restart BM enable removed'),
                ('rc = gf_engine_stop(f);', 'rc = gf_master_disable(f);', 'BM clear before engine stop'),
                ('rc = geforce_tick_bind(0, 0) ? GF1_EIRQ : GF1_OK;',
                 'rc = GF1_OK;', 'tick reference retained'),
                ('f->running = false;\n    rc =', '/* running retained */\n    rc =', 'running retained at stop'),
                ('f->running = true;', '/* running not published */', 'running missing at enable'),
                ('unsigned flags = irq_save();\n        f->running = true;',
                 'unsigned flags = 0;\n        f->running = true;', 'IRQ enable outside CLI'),
                ('gf_irq_enable(f, true, true, GF1_HEAD_A)',
                 'gf_irq_enable(f, true, true, 0)', 'head A disabled'),
                ('rc = gf_irq_detach(f);', 'rc = gf_instmem_unmap(f);', 'unmap before IRQ detach'),
                ('rc = gf_engine_stop(f);\n    if (rc) return f->last_rc = rc;',
                 'rc = gf_engine_stop(f);\n    (void)rc;', 'engine failure ignored'),
                ('gf_inst_free(f, obj);', '(void)obj;', 'variable handles retained'),
                ('f->imem.ramfc = (struct gf_inst){0};',
                 '/* fixed RAMFC retained */', 'fixed handle retained'),
                ('if (!rc) rc = gf_mc_init(f);\n    if (!rc) rc = gf_instmem_init(f);',
                 'if (!rc) rc = gf_instmem_init(f);\n    if (!rc) rc = gf_mc_init(f);',
                 'instmem before MC'),
                ('if (!rc) rc = gf_irq_attach(f);', '/* IRQ attach omitted */', 'IRQ attach omitted'),
                ('int stop_rc = gf_foundation_stop(f);', 'int stop_rc = 0;', 'start rollback omitted'),
                ('{0x40013cU, 0x400100U, GF1_IRQ_ALL}',
                 '{0x400140U, 0x400100U, GF1_IRQ_ALL}', 'wrong GR mask register'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1, label
                mutant = td / 'mutant.c'
                # Absolute includes preserve the production header dependencies.
                text = original.replace(old, new, 1)
                text = text.replace('#include "foundation.h"',
                                    '#include "' + str(SOURCE.parent / 'foundation.h') + '"')
                for header in ('include/io.h', 'drivers/video/geforce/geforce_tick.h'):
                    text = text.replace('#include "../../' + header + '"',
                                        '#include "' + str(ROOT / header) + '"')
                mutant.write_text(text)
                build(mutant)  # Compilation failure never counts as RED.
                result = host32.run([str(exe)], runner='qemu')
                assert result.returncode != 0, label
                print('MUTATE runtime RED: ' + label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gf1_foundation.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
