"""Raw PCI location and GeForce cache behavior."""
CHECK = dict(name='pci-locate-host', tier=1, needs=[], budget=4,
             covers=['drivers/pci/pci_locate.c', 'drivers/pci/pci_locate.h',
                     'kdebug/recover_gf_locate.c', 'kdebug/recover.h',
                     'tools/tests/pci_locate_host.c', 'tools/tests/test_pci_locate.py'])
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('drivers/pci/pci_locate.c', 'kdebug/recover_gf_locate.c')
MUTATIONS = [
    ('M1', 1, 'return gf_loc.found ? gf_loc.dev : RECOVER_GF_DEV_FALLBACK;', 'return RECOVER_GF_DEV_FALLBACK;'),
    ('M2', 0, 'if (!out->found) { out->found = 1; out->dev = (u8)dev; }', 'if (1) { out->found = 1; out->dev = (u8)dev; }'),
    ('M3', 1, 'if (!gf_loc.found) pci_locate_fn0', 'if (1) pci_locate_fn0'),
    ('M4', 1, 'return gf_loc.found ? gf_loc.dev : RECOVER_GF_DEV_FALLBACK;', 'if (!gf_loc.found) { gf_loc.found=1; gf_loc.dev=RECOVER_GF_DEV_FALLBACK; } return gf_loc.dev;'),
    ('M5', 0, 'pci_cfg_read32(bus, dev, 0, PCI_CFG_VENDOR_ID) != id', '({ int absent=1; for (u32 fn=0; fn<8; fn++) if (pci_cfg_read32(bus, dev, fn, PCI_CFG_VENDOR_ID)==id) absent=0; absent; })'),
    ('M6', 0, 'pci_cfg_read32(bus, dev, 0, PCI_CFG_VENDOR_ID)', 'pci_cfg_read32(((void)bus, 0), dev, 0, PCI_CFG_VENDOR_ID)'),
]
def build_and_run(td, mutation=None):
    sources = []
    for i, rel in enumerate(SOURCES):
        source = (ROOT/rel).read_text()
        if mutation and mutation[1] == i:
            _, _, old, new = mutation
            assert source.count(old) == 1
            source = source.replace(old, new, 1)
        source = source.replace('#include "pci_locate.h"', '#include "'+str(ROOT/'drivers/pci/pci_locate.h')+'"').replace('#include "pci.h"', '#include "'+str(ROOT/'drivers/pci/pci.h')+'"').replace('#include "recover.h"', '#include "'+str(ROOT/'kdebug/recover.h')+'"').replace('#include "../drivers/pci/pci_locate.h"', '#include "'+str(ROOT/'drivers/pci/pci_locate.h')+'"')
        p=Path(td)/Path(rel).name; p.write_text(source); sources.append(str(p))
    exe=Path(td)/'test'
    subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'include'), '-I'+str(ROOT/'sdk/include/os32'),
                    str(ROOT/'tools/tests/pci_locate_host.c'), *sources, '-o',str(exe)],check=True)
    return [subprocess.run([str(exe),case]).returncode for case in 'abcdefg']
def main(mutate):
    with tempfile.TemporaryDirectory() as td:
        assert build_and_run(td)==[0]*7
        print('PASS a-g')
        if mutate:
            for item in MUTATIONS:
                result=build_and_run(td,item)
                assert 1 in result, (item[0], result)
                print(item[0], 'RED', result)
    return 0
def run(ctx) -> int:
    argv = ['python3', '-B', 'tools/tests/test_pci_locate.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, close_fds=False)
if __name__ == '__main__':
    sys.exit(main('--mutate' in sys.argv))
