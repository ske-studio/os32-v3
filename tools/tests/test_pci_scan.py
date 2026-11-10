"""Run the real boot PCI enumeration and resource collector over fake ports."""
CHECK = dict(name='pci-scan-host', tier=4,
    covers=['drivers/pci/pci.c', 'drivers/pci/pci_barsize.c', 'drivers/pci/pci_barsize.h',
            'drivers/pci/pci.h', 'drivers/pci/pci_decode.c', 'drivers/pci/pci_decode.h',
            'tools/tests/pci_scan_host.c', 'tools/tests/test_pci_scan.py',
            'kdebug/gfinit_resources.c', 'kdebug/gfinit_resources.h', 'kdebug/gfinit_resources/*.inc',
            'tools/tests/host32.py', 'tools/tests/mutpar.py', 'include/types.h', 'include/pci_dev_contract.h', 'include/pc98.h', 'include/kprintf.h', 'kdebug/recover.h',
            'sdk/include/os32/os32_kapi_shared.h'], needs=[], budget=5)
import pathlib
import subprocess
import sys
import tempfile
import mutpar
import host32
ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('drivers/pci/pci.c', 'record_missing', '    pci_barsize_record(bus, dev, fn, pci_cfg_read8(bus, dev, fn, PCI_CFG_HEADER_TYPE));', ''),
    ('drivers/pci/pci.c', 'reset_missing', '    pci_barsize_reset();', ''),
    ('drivers/pci/pci.c', 'record_after_full', '    pci_barsize_record(bus, dev, fn, pci_cfg_read8(bus, dev, fn, PCI_CFG_HEADER_TYPE));\n    g_pci_seen++;\n    if (g_pci_count >= PCI_MAX_DEVS) return;',
     '    g_pci_seen++;\n    if (g_pci_count >= PCI_MAX_DEVS) return;\n    pci_barsize_record(bus, dev, fn, pci_cfg_read8(bus, dev, fn, PCI_CFG_HEADER_TYPE));'),
    ('drivers/pci/pci.c', 'scan_single', 'nfn = pci_is_multifunction(hdr0) ? PCI_FN_COUNT : 1;', 'nfn = 1; (void)hdr0;'),
    ('kdebug/gfinit_resources/bounds.inc', 'collect_single', 'return pci_is_multifunction(hdr0) ? PCI_FN_COUNT : 1;', '(void)hdr0; return 1;'),
    ('drivers/pci/pci_barsize.c', 'disabled_rom', 'd->rom_reg && (pci_cfg_read32(bus, dev, fn, d->rom_reg) & 1U)', 'd->rom_reg'),
    ('drivers/pci/pci_barsize.c', 'ide_compat', 'if (ide && i < 4 && !(progif & (i < 2 ? 1U : 4U))) continue;', '(void)ide; (void)progif;'),
    ('drivers/pci/pci_barsize.c', 'measure_atomic', 'unsigned int flags = irq_save();', 'unsigned int flags = irq_state;'),
]
def command(exe):
    return ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            *['-I'+str(ROOT/p) for p in ('include','drivers','platform/pc98','sdk/include/os32')],
            str(ROOT/'tools/tests/pci_scan_host.c'), '-o', str(exe)]
def mutation(item):
    file,name,old,new=item
    source=(ROOT/file).read_text()
    if old and source.count(old)!=1:return name+': ambiguous anchor',1
    with tempfile.TemporaryDirectory(prefix='pci-scan-mut-') as td:
        exe=pathlib.Path(td)/'host'
        try:
            tree=mutpar.build_in_tree(ROOT,td,{file:source.replace(old,new,1) if old else source},[command(exe)],capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name+': compile failed '+str(exc.stderr),1
        result=host32.run([str(exe)],cwd=tree,capture_output=True,timeout=15)
        return f'{name}: {"RED" if result.returncode else "GREEN"} rc={result.returncode}',int(result.returncode==0)
def main():
    with tempfile.TemporaryDirectory(prefix='pci-scan-') as td:
        exe=pathlib.Path(td)/'host'
        subprocess.run(command(exe),check=True)
        if host32.run([str(exe)]).returncode:return 1
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,MUTATIONS,('drivers/pci/pci.c','control','',''))))
    return 0
if __name__=='__main__':sys.exit(main())
def run(ctx):
    return subprocess.call(['python3','-B','tools/tests/test_pci_scan.py',*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)
