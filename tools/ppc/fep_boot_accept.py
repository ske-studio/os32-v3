"""Boot the production FEP from a disposable APM/ext2 root."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from ata_accept import guest
from run import ROM
from hdd_root_accept import disk
from kboot_accept import ROOT, build
from root_pair import HEADER_FIELDS, bind_module

sys.path.insert(0, str(ROOT / 'tools'))
from fep_to_sqlite import create_db


def install(directory, image, dictionary):
    for subdir in ('db', 'etc', 'boot', 'sys', 'fonts'):
        (directory / subdir).mkdir()
    for name in ('/boot/fep.mod', '/sys/unicode.bin', '/fonts/shino16.kcg', '/sys/shell.bin'):
        contents = subprocess.check_output(['isoinfo', '-i', image, '-x', name.upper() + ';1'])
        assert contents, name
        (directory / name[1:]).write_bytes(contents)
    if dictionary == 'wrong-pair':
        module = directory / 'boot/fep.mod'
        data = module.read_bytes()
        pair, = struct.unpack_from('<I', data, 4 * HEADER_FIELDS.index('PAIR_CRC'))
        module.write_bytes(bind_module(data, pair ^ 1))
    if dictionary == 'corrupt':
        (directory / 'db/fep.db').write_bytes(b'invalid SQLite database')
    elif dictionary != 'missing':
        create_db([('かな', '仮名', 1, 100)], directory / 'db/fep.db', 0, variant='M')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file() or not all(
            shutil.which(tool) for tool in ('isoinfo', 'genisoimage', 'mke2fs')) or (
            not args.build_only and (not ROM.is_file() or not shutil.which('qemu-system-ppc'))):
        print('SKIP: PowerPC toolchain or HDD acceptance tools unavailable')
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-fep-boot-'))
    kernel = 'platform/powermac/ppc_kernel.c'
    product = (ROOT / kernel).read_text()
    call = '        pm_exec_boot();'
    assert product.count(call) == 1
    # Read through the same generated wrapper the shell's clients call.
    probe = '''
        char line[18];
        os32_sqlite_snprintf(sizeof(line), line, "%s,%d,%u", "FEP", -2147483647 - 1, 42u);
        kprintf(7, "FEP-TEST format=%s\\n", line);
        wrap_ime_set_mode(IME_MODE_HIRAGANA);
        kprintf(7, "FEP-TEST ready=%d sql=%d public=%d mode=%d pages=%u\\n",
                services_stage == SERVICES_READY, sqlite_module_ready(), fep_services_public(),
                wrap_ime_get_mode(), fep_module_pages());
        kprintf(7, "FEP-TEST done\\n");
'''
    product = ('#include "os32_kapi_shared.h"\n'
               '#include "fep_module.h"\n'
               'extern char *os32_sqlite_snprintf(int, char *, const char *, ...);\n'
               'extern void wrap_ime_set_mode(int);\n'
               'extern int wrap_ime_get_mode(void);\n'
               'extern int sqlite_module_ready(void);\n' + product.replace(call, probe + call))
    cases = [('normal', None, None)]
    if args.mutate:
        cases.append(('skip-hdd', '&& hdd_root_ready &&', '&& !hdd_root_ready &&'))
        cases.append(('close-failure', 'int sql_rc = sqlite_static_boot();',
                      'int sql_rc = sqlite_static_boot(); services_boot_fault = SERVICES_FAULT_FEP_CLOSE;'))
    hashes = set()
    for name, old, new in cases:
        out = work / name
        out.mkdir()
        source = out / 'kernel.c'
        if old:
            assert product.count(old) == 1
        source.write_text(product.replace(old, new) if old else product)
        elf = build(out, '-j6', 'IMAGE=powermac', 'KB_MUT_SRC=' + kernel,
                    'KB_MUT_FILE=' + str(source), 'KB_SH_OUT=' + str(work / 'user'), 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        for dictionary in (('dictionary', 'missing', 'corrupt', 'wrong-pair') if name == 'normal' else ('dictionary',)):
            trial = out / dictionary
            trial.mkdir()
            media = disk(trial, 'root', lambda d: install(d, out / 'root.iso', dictionary))
            if args.build_only:
                continue
            image = trial / elf.name
            image.hardlink_to(elf)
            lines = guest(image, '7450', media, done='FEP-TEST done',
                          bootargs='os32-root=/pci@f2000000/mac-io@c/ata-3@20000/disk@0:3')
            report, = [line for line in lines if line.startswith('FEP-TEST ready=')]
            ready = 'ready=1 sql=1 public=1 mode=1' in report
            assert ready == (dictionary == 'dictionary' and name == 'normal'), report
            if not ready:
                assert 'ready=0 sql=1 public=0 mode=0' in report, report
            pages = int(report.split('pages=')[1])
            assert bool(pages) == (ready or (name == 'close-failure')), report
            assert '[sqlite] static rc=0' in lines
            assert 'FEP-TEST format=FEP,-2147483648,4' in lines
            if old:
                print(name, 'RED', report, flush=True)
        print(name, digest, flush=True)
    print(f'FEP HDD {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    raise SystemExit(main())
