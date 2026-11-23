"""Cold boot a disk installed by cdinst, including Open Firmware failure controls."""
import argparse
from dataclasses import replace
import hashlib
import re
from pathlib import Path
import struct
import tempfile

from deploy_accept import extract, verify_disk
from hdd_root_accept import disk as other_disk
from kboot_accept import build_media, mutant_settings
from link_sh_run import ROOT, Session
from product_accept import catalog

DEVICE = '/pci@f2000000/mac-io@c/ata-3@20000/disk@'


class DiskSession(Session):
    def boot_args(self):
        args = ['-boot', 'c', '-prom-env', 'boot-device=' + self.bootpath]
        if self.serial_console:
            args.append('-nographic')
        return args


def attach(session, index, path):
    session.usb_devices += ('-drive', f'file={path},format=raw,if=ide,index={index},cache=writethrough')


def install(media, out):
    raw = out/'installed.raw'
    with raw.open('xb') as f:
        f.truncate(32 * 1024 * 1024)
    s = Session(media, '7450', out/'install', 450)
    attach(s, 0, raw)
    try:
        s.start()
        s.wait(s.ready, 'installer shell', 90)
        s.close_rshell()
        s.keys('/sbin/cdinst.bin\n')
        s.wait(lambda: 'Select [0-3]' in s.screen('menu'), 'package menu', 60)
        s.key('1')
        s.wait(lambda: '[y/N]' in s.screen('confirm'), 'target confirmation', 90)
        s.key('y')
        s.wait(lambda: s.word('exec_nest_level') == 1 and 'Installation Complete' in
               s.screen('installed'), 'committed installation', 240)
    finally:
        s.close()
    verify_disk(raw, out, media)
    return raw


def stale_bootargs(media, out, raw, mutant):
    before = raw.read_bytes()
    s = Session(media, '7450', out/'cd-stale', 150)
    s.usb_devices += ('-prom-env', r'boot-command=boot cd:,\PPCKERN.ELF os32-root=' + DEVICE + '0:3')
    attach(s, 0, raw)
    try:
        s.start()
        if mutant:
            s.wait(lambda: s.word('of_hd0_registrations') == 1, 'stale root selected', 90)
        else:
            s.wait(s.ready, 'installer shell on media root', 90)
            assert s.word('pm_root_unit') == 0xffffffff
            assert s.word('of_hd0_registrations') == 0
    finally:
        s.close()
    if not mutant:
        assert raw.read_bytes() == before


def control(raw, media, case):
    data = bytearray(raw.read_bytes())
    hfs, = struct.unpack_from('>I', data, 2*512+8)
    root, = struct.unpack_from('>I', data, 3*512+8)
    if case == 'partition':
        descriptor = extract(media.iso, 'PLAN.BIN')
        loader, = struct.unpack_from('>I', descriptor, 12)
        offset = hfs*512 + loader + 4
        assert struct.unpack_from('>I', data, offset) == (3,)
        struct.pack_into('>I', data, offset, 2)
    elif case == 'missing':
        name, _, _ = catalog(data[hfs*512:root*512])['PRODUCT.ELF']
        data[hfs*512+name] = ord('X')
    elif case == 'corrupt':
        offset = root*512 + 1080
        assert data[offset:offset+2] == b'\x53\xef'
        data[offset:offset+2] = bytes(2)
    elif case == 'oversized':
        sectors, = struct.unpack_from('>I', data, 3*512+12)
        sb = root*512 + 1024
        assert struct.unpack_from('<H', data, sb+56) == (0xef53,)
        shift, = struct.unpack_from('<I', data, sb+24)
        struct.pack_into('<I', data, sb+4, (sectors >> (1+shift)) + 1)
    path = raw.with_name(case+'.raw')
    path.write_bytes(data)
    return path


def boot(media, out, first, raw, case, mutant):
    s = DiskSession(replace(media, elf=out/'hdd/PRODUCT.ELF'), '7450', out/case, 200)
    unit = int(case != 'save')
    s.bootpath = DEVICE + str(unit) + r':2,\LOADER.ELF'
    s.serial_console = case in ('partition', 'missing')
    if unit:
        attach(s, 0, first)
    attach(s, unit, raw)

    def logged(message):
        return any(p.is_file() and message in p.read_bytes() for p in
                   (s.out/'serial-a.log', s.out/'serial-b.log'))

    try:
        s.start()
        if s.serial_console:
            s.wait(lambda: logged(('OS32 HDD loader: '+s.bootpath).encode()), 'OF loader bootpath', 60)
        else:
            s.wait(lambda: s.word('pm_product_handoff') == 0x4f533332, 'loader handoff', 60)
            name = (DEVICE + str(unit)).encode()
            assert s.word('pm_product_handoff', 36) == len(name)
            assert bytes(s.word('pm_product_handoff', 40+i, 'b') for i in range(len(name))) == name
        if case == 'missing':
            s.wait(lambda: logged(b'HDD loader cannot open PRODUCT.ELF'), 'missing product')
        elif case == 'corrupt':
            s.wait(lambda: logged(b'FATAL kboot hd0 select'), 'damaged ext2', 90)
        elif case == 'oversized':
            s.wait(lambda: logged(b'FATAL kboot hd0 root'), 'mount rejected', 90)
            assert logged(b'HDD root hd0 ext2_mount rc=-13')
            assert s.word('of_hd0_registrations') == 1
        elif case == 'partition':
            def stopped():
                registers = s.q.call('human-monitor-command', {'command-line': 'info registers'})
                nip = re.search(r'NIP\s+([0-9a-fA-F]+)', registers)
                return nip and int(nip[1], 16) == s.symbols['ppc_kernel_entry_stop']
            s.wait(stopped, 'product entry stopped')
            assert s.word('of_hd0_registrations') == 0
            assert logged(b'HDD root partition number mismatch') == (not mutant)
        else:
            s.wait(s.ready, 'resident HDD shell', 90)
            assert s.word('of_hd0_registrations') == 1
            s.close_rshell()
            s.keys('tick '+case+'\n')
            s.wait(lambda: 'root=hd0 child=0 value=selected learned=1' in s.screen('state'),
                   'mounted root, persisted value and learned candidate', 45)
            return
        assert not any(e['parts'][:2] == ['RSH', 'READY'] for e in s.events)
        assert s.word('exec_nest_level') == 0
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    hashes = set()
    with tempfile.TemporaryDirectory(prefix='of-hdd-') as tmp:
        work = Path(tmp)
        first = other_disk(work, 'first')
        source = 'userland/shell/cmd_base.c'
        shell = (ROOT/source).read_text()
        anchor = 'static int cmd_tick(int argc, char **argv)\n{'
        assert shell.count(anchor) == 1
        fixture = work/'cmd_base.c'
        fixture.write_text('#include <string.h>\n'+shell.replace(anchor, anchor+'\n'+
                           (ROOT/'tools/ppc/of_hdd_probe.inc').read_text()))
        device = 'drivers/base/dev.c'
        registrations = (ROOT/device).read_text()
        anchor = '    dev_table[dev_num] = dev;'
        assert registrations.count(anchor) == 1
        counter = work/'dev.c'
        counter.write_text('volatile unsigned of_hd0_registrations;\n' + registrations.replace(
            anchor, '    if (!kstrcmp(dev->name, "hd0")) ++of_hd0_registrations;\n' + anchor))
        for variant in (['normal', 'no-number-check', 'stale-root'] if args.mutate else ['normal']):
            mutant = variant == 'no-number-check'
            out = work/variant
            out.mkdir()
            settings = mutant_settings(out, 'number', 'platform/powermac/product_boot.c',
                        'if (p->root_partition != pm_expected_plan.root_partition)', 'if (0)') if mutant else []
            settings = [arg.replace('KB_MUT_', 'HD_MUT_', 1) for arg in settings]
            if variant == 'stale-root':
                settings = mutant_settings(out, variant, 'platform/powermac/ide_tree.c',
                                           '#elif PM_INSTALL_MEDIA', '#elif 0')
            media = build_media(out, '-j6', 'IMAGE=powermac',
                                'KB_SH_OUT='+str(work/'user'), 'PPC_RES_MUT_SRC='+source,
                                'PPC_RES_MUT_FILE='+str(fixture), 'KB_MUT_EXTRA_SRC='+device,
                                'KB_MUT_EXTRA_FILE='+str(counter), *settings)
            digest = hashlib.sha256(extract(media.iso, 'PRODUCT.ELF')).hexdigest()
            assert digest not in hashes
            hashes.add(digest)
            print(out.name, digest, flush=True)
            if args.build_only:
                continue
            if not mutant:
                stale_bootargs(media, out, first, variant == 'stale-root')
                if variant == 'stale-root':
                    print('stale CD bootargs runtime RED')
                    continue
            raw = install(media, out)
            for case in (['partition'] if mutant else ['save', 'read', 'missing', 'corrupt', 'oversized', 'partition']):
                path = raw if case in ('save', 'read') else control(raw, media, case)
                if case == 'oversized':
                    before = (first.read_bytes(), path.read_bytes())
                boot(media, out, first, path, case, mutant)
                if case == 'oversized':
                    assert before == (first.read_bytes(), path.read_bytes())
            if mutant:
                print('root partition diagnostic runtime RED')
    print(f'PPC OF HDD {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    main()
