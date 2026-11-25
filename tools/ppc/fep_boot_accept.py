"""Boot the production FEP from a disposable APM/ext2 root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

from ata_accept import guest
from run import QMP, ROM
from usb_run import keyboard_actions
from hdd_root_accept import disk
from kboot_accept import ROOT, build_media
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


def type_reading(proc, serial, deadline):
    q = QMP(proc, deadline)

    def read():
        return serial.read_text(errors='replace') if serial.exists() else ''

    def wait(predicate, label):
        while proc.poll() is None and time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.03)
        raise RuntimeError(f'{label}: {serial}')

    key, type_keys = keyboard_actions(q, read, wait, serial.parent)
    wait(lambda: 'protocol=01' in read() and 'FEP-TEST remote' in read(), 'resident shell')
    key('esc')
    wait(lambda: 'FEP-TEST local' in read(), 'local prompt')
    type_keys('kana')
    key('ret')  # Commit the reading. In raw mode this already submits the line.
    key('ret')  # Submit the committed UTF-8 line.


def resident(work, raw):
    path = 'userland/shell/ui.c'
    source = (ROOT / path).read_text()
    edits = [
        ('if (remote) (void)execute_command("rshell");',
         'if (remote) { g_api->serial_puts("\\nFEP-TEST remote\\n"); (void)execute_command("rshell"); }'),
        ('cmd_dropped = 0;\n        sh_mark_drawn',
         'cmd_dropped = 0;\n        g_api->serial_puts("\\nFEP-TEST local\\n");\n        sh_mark_drawn'),
        ('(void)execute_command(cmd_buf);',
         'g_api->serial_puts("\\nFEP-TEST line="); g_api->serial_puts(cmd_buf); '
         'g_api->serial_puts("\\nFEP-TEST submitted\\n");\n        (void)execute_command(cmd_buf);'),
    ]
    if raw:
        edits.append(('(g_api->ime_getkey())', '(g_api->kbd_getkey())'))
    for old, new in edits:
        assert source.count(old) == 1, old
        source = source.replace(old, new)
    work.mkdir()
    file = work / 'ui.c'
    file.write_text(source)
    return ['KB_SH_OUT=' + str(work), 'PPC_RES_MUT_SRC=' + path, 'PPC_RES_MUT_FILE=' + str(file)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file() or not all(
            shutil.which(tool) for tool in ('isoinfo', 'genisoimage', 'mke2fs', 'debugfs')) or (
            not args.build_only and (not ROM.is_file() or not shutil.which('qemu-system-ppc'))):
        print('SKIP: PowerPC toolchain or HDD acceptance tools unavailable')
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-fep-boot-'))
    kernel = 'sys/platform/powermac/ppc_kernel.c'
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
    manifest = json.loads((ROOT / 'build/ppc-ram-root.json').read_text())
    database = work / 'fep.db'
    create_db([('かな', '仮名', 1, 100)], database, 0, variant='M')
    manifest['/db/fep.db'] = str(database)
    template = work / 'template.json'
    template.write_text(json.dumps(manifest))
    # This diagnostic selects a fixture HDD to exercise dictionary loading.
    tree = 'sys/platform/powermac/ide_tree.c'
    diagnostic_tree = work / 'ide_tree.c'
    diagnostic_tree.write_text(f'#undef PM_INSTALL_MEDIA\n#include "{tree}"\n')
    shell = resident(work / 'user', False)
    cases = [('normal', None, None)]
    if args.mutate:
        cases.extend([('allow-ram', '&& hdd_root_ready &&', '&&'), ('raw-shell', None, None)])
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
        boot = build_media(out, '-j6', 'IMAGE=powermac', 'KB_MUT_SRC=' + kernel,
                    'KB_MUT_FILE=' + str(source), 'KBOOT_USB_KEY_TRACE=1',
                    'KB_MUT_EXTRA_SRC=' + tree, 'KB_MUT_EXTRA_FILE=' + str(diagnostic_tree),
                    'ROOT_MANIFEST=' + str(template),
                    *(resident(work / 'raw-user', True) if name == 'raw-shell' else shell), 'kernel')
        digest = hashlib.sha256(boot.elf.read_bytes()).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        trials = {'normal': ('dictionary', 'missing', 'corrupt', 'wrong-pair', 'ram'),
                  'allow-ram': ('ram',)}.get(name, ('dictionary',))
        for dictionary in trials:
            trial = out / dictionary
            trial.mkdir()
            ram = dictionary == 'ram'
            media = None if ram else disk(
                trial, 'root', lambda d: install(d, out / 'root.iso', dictionary))
            if not ram:
                module = (trial / 'root/boot/fep.mod').read_bytes()
                hole = next(i for i in range(0, len(module) - 1023, 1024)
                            if not any(module[i:i + 1024]))
                fs = trial / 'root.ext2'
                assert int(subprocess.check_output(
                    ['debugfs', '-R', f'bmap /boot/fep.mod {hole // 1024}', fs])) == 0
                assert subprocess.check_output(['debugfs', '-R', 'cat /boot/fep.mod', fs]) == module
            if args.build_only:
                continue
            keys = dictionary in ('dictionary', 'missing') and name != 'close-failure'
            lines = guest(boot, '7450', media, absent=ram, keyboard=type_reading if keys else None,
                          done='FEP-TEST submitted' if keys else 'FEP-TEST done',
                          directory=trial, bootargs=None if ram else 'os32-root=/pci@f2000000/mac-io@c/ata-3@20000/disk@0:3')
            report, = [line for line in lines if line.startswith('FEP-TEST ready=')]
            ready = 'ready=1 sql=1 public=1 mode=1' in report
            if ram:
                assert any(line.startswith('KBOOT ROOT rc=0 registered=1') for line in lines)
                attempted = any(line.startswith('[fep] static rc=') for line in lines)
                assert attempted == (name == 'allow-ram'), lines
            if name != 'allow-ram':
                assert ready == (dictionary == 'dictionary' and name != 'close-failure'), (
                    name, dictionary, [line for line in lines if line.startswith(
                        ('HDD root', 'KBOOT ROOT', '[unicode]', '[services]', '[fep]', '[IME]', 'FEP-TEST'))])
            if keys:
                observed = next(line for line in lines if line.startswith('FEP-TEST line='))
                expected = 'かな' if ready and name != 'raw-shell' else 'kana'
                assert observed == 'FEP-TEST line=' + expected, observed
                if name == 'raw-shell':
                    print(name, 'RED', observed, flush=True)
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
