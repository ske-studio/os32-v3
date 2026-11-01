"""Layer 1 PCI master and latency policy with a fake configuration space."""
CHECK = dict(name='gf1-master-host', tier=1,
    covers=['hwdev/gf/master.c', 'hwdev/gf/foundation.h', 'hwdev/gf/internal.h',
            'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'kdebug/recover.h',
            'tools/tests/gf1_master_host.c', 'tools/tests/test_gf1_master.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'hwdev/gf/master.c'


def main():
    with tempfile.TemporaryDirectory(prefix='gf1-master-') as td:
        exe = Path(td) / 'host'
        def run(source):
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie',
                            *include_flags(ROOT), '-I' + str(ROOT),
                            '-DMASTER_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/gf1_master_host.c'), '-o', str(exe)], check=True)
            return host32.run([str(exe)], cwd=ROOT, runner='qemu').returncode
        assert run(SOURCE) == 0
        print('PASS PCI master, latency and failures', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            old = 'pci_cfg_write16(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,\n                        PCI_CFG_COMMAND, RECOVER_COMMAND_RUN)'
            new = '(pci_cfg_write32(RECOVER_GF_BUS, RECOVER_GF_DEV, RECOVER_GF_FN,\n                         PCI_CFG_COMMAND, RECOVER_COMMAND_RUN), 0)'
            assert original.count(old) == 1
            mutant = Path(td) / 'master.c'
            # Preserve relative includes when compiling the temporary source.
            mutant.write_text(original.replace('#include "foundation.h"', '#include "' + str(ROOT / 'hwdev/gf/foundation.h') + '"').replace('#include "../../drivers/pci/pci.h"', '#include "' + str(ROOT / 'drivers/pci/pci.h') + '"').replace('#include "../../kdebug/recover.h"', '#include "' + str(ROOT / 'kdebug/recover.h') + '"').replace(old, new))
            assert run(mutant) != 0
            print('MUTATE RED Command DWORD write', flush=True)
            old = 'PCI_CFG_LATENCY_TIMER) < GF_LATENCY_MIN'
            new = 'PCI_CFG_CACHE_LINE) < GF_LATENCY_MIN'
            assert original.count(old) == 1
            mutant.write_text(original.replace('#include "foundation.h"', '#include "' + str(ROOT / 'hwdev/gf/foundation.h') + '"').replace('#include "../../drivers/pci/pci.h"', '#include "' + str(ROOT / 'drivers/pci/pci.h') + '"').replace('#include "../../kdebug/recover.h"', '#include "' + str(ROOT / 'kdebug/recover.h') + '"').replace(old, new))
            assert run(mutant) != 0
            print('MUTATE RED latency read at 0Ch', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_master.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
