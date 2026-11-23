"""Install from directory and package media onto disposable mac99 disks."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

from kboot_accept import build_media
from link_sh_run import ROOT, Session, cross
from mkpkg import read_pkg
from product_accept import catalog
from root_lz4_layout import segments

MANAGEMENT = 'kernel/dev/install_management.c'
MUTATIONS = {
    'restore': ('rc = m->write(unit, s->lba, 1, s->bytes);',
                'rc = s->lba == 1 ? 0 : m->write(unit, s->lba, 1, s->bytes);'),
    'publish': ('r->state = AVAILABLE;\n    irq_restore(flags);\n    if (old)',
                'r->state = RAW_ONLY;\n    irq_restore(flags);\n    if (old)'),
}


class MutationDetected(Exception):
    pass


def build(out, case):
    out.mkdir()
    # Simulate I/O failure at two points on successive reservations. The first
    # fails before the DDR changes, the second after the first APM entry.
    anchor = '        rc = m->write(issued.unit, physical, count, in);'
    source = (ROOT / MANAGEMENT).read_text()
    assert source.count(anchor) == 1
    if case in MUTATIONS:
        before, after = MUTATIONS[case]
        assert source.count(before) == 1
        source = source.replace(before, after)
    fixture = out / 'install_management.c'
    fixture.write_text('volatile unsigned deploy_io_faults;\n' + source.replace(anchor, '''
        if (issued.phase == TABLE_READY &&
            ((deploy_io_faults == 0 && physical == 0) ||
             (deploy_io_faults == 1 && physical == 2))) {
            ++deploy_io_faults;
            rc = OS32_ERR_IO;
        } else {
    ''' + anchor + '\n        }'))
    settings = ['KB_MUT_SRC=' + MANAGEMENT, 'KB_MUT_FILE=' + str(fixture)]
    return build_media(out, '-j6', 'IMAGE=powermac', *settings)


def extract(iso, path):
    data = subprocess.check_output(['isoinfo', '-i', iso, '-x', '/' + path.upper() + ';1'])
    assert data, (iso, path)
    return data


def root_files(root):
    listing = subprocess.check_output(['isoinfo', '-i', root, '-f'], text=True)
    return {name[:-2].rstrip('.').lower(): subprocess.check_output(['isoinfo', '-i', root, '-x', name])
            for name in listing.splitlines() if name.endswith(';1')}


def check_media(media, out):
    def package(name):
        path = out/name
        path.write_bytes(extract(media.iso, name))
        _, entries = read_pkg(path)
        return {dest: payload for dest, _, payload in entries if payload is not None}

    volume = extract(media.iso, 'BOOT.HFS')
    assert len(volume) == 8*1024*1024
    assert package('BOOT.PKG') == {'/boot/boot.hfs': volume, '/boot/plan.bin': extract(media.iso, 'PLAN.BIN')}
    copies = root_files(out/'root.iso')
    assert {'/sbin/install.bin', '/sbin/cdinst.bin', '/sys/shell.bin', '/boot/fep.mod',
            '/sys/unicode.bin', '/fonts/shino16.kcg'} <= copies.keys()
    assert package('MINIMAL.PKG') == copies


def installed_elf(elf, symbol, plan):
    original = elf.read_bytes()
    symbols = subprocess.check_output([cross('nm'), '-n', elf], text=True)
    addr, = [int(row.split()[0], 16) for row in symbols.splitlines()
             if row.split()[-1] == symbol]
    offset, = [off + addr - va for kind, off, va, _, length, *_ in segments(original)
               if kind == 1 and va <= addr and addr + len(plan) <= va + length]
    return original[:offset] + plan + original[offset + len(plan):]


def verify_disk(raw, out, media):
    data = raw.read_bytes()
    assert struct.unpack_from('>HHI', data) == (0x4552, 512, len(data)//512)
    starts = []
    for i, kind in enumerate((b'Apple_partition_map', b'Apple_HFS', b'Apple_UNIX_SVR2'), 1):
        entry = data[i*512:(i+1)*512]
        sig, _, entries, start, size = struct.unpack_from('>HHIII', entry)
        assert sig == 0x504d and entries == 3 and entry[48:80].rstrip(b'\0') == kind
        assert size and start + size <= len(data)//512
        starts.append((start, size))
    assert starts[0] == (1, 63) and starts[1] == (64, 8*2048)
    start, size = starts[2]
    assert start == sum(starts[1]) and size == len(data)//512-start
    volume = data[64*512:start*512]
    assert volume[-1024:-512] == volume[1024:1536]
    forks = catalog(volume)
    expected = bytearray(extract(media.iso, 'BOOT.HFS'))
    template = catalog(expected)
    plan = struct.pack('>5I', 2, 3, start, size, 3)
    for name, symbol in [('LOADER.ELF', 'pm_loader_plan'), ('PRODUCT.ELF', 'pm_expected_plan')]:
        _, pos, length = forks[name]
        elf = out/'hdd'/name
        assert elf.read_bytes() == extract(media.iso, name)
        contents = installed_elf(elf, symbol, plan)
        assert volume[pos:pos+length] == contents, name
        _, offset, fork_size = template[name]
        assert fork_size == len(contents)
        expected[offset:offset+fork_size] = contents
    assert volume == expected
    partition = raw.with_suffix('.ext2')
    partition.write_bytes(data[start*512:(start+size)*512])
    for path, source in root_files(out/'root.iso').items():
        installed = subprocess.check_output(['debugfs', '-R', 'cat '+path, partition])
        assert installed == source, path


def runtime(media, out, entry, case):
    disk = out/(entry + '.raw')
    with disk.open('xb') as f:
        f.truncate(32 * 1024 * 1024)
    initial = disk.read_bytes()
    s = Session(media, '7450', out/(entry+'-session'), 900)
    s.usb_devices += ('-drive', f'file={disk},format=raw,if=ide,index=0,cache=writethrough')
    try:
        s.start()
        s.wait(s.ready, 'resident shell', 90)
        s.close_rshell()

        def confirm(answer):
            s.keys('clear\n')
            s.keys('/sbin/' + entry + '.bin\n')
            if entry == 'cdinst':
                s.wait(lambda: 'Select [0-3]' in s.screen('menu') and s.word('exec_nest_level') > 1,
                       'package menu', 60)
                s.key('1')
            s.wait(lambda: '[y/N]' in s.screen('confirm') and s.word('exec_nest_level') > 1,
                   'target confirmation', 90)
            s.key(answer)
            s.wait(lambda: s.word('exec_nest_level') == 1, 'installer returned', 240)

        confirm('n')
        assert disk.read_bytes() == initial
        for number, interruption in enumerate(('before table', 'partial table'), 1):
            confirm('y')
            assert s.word('deploy_io_faults') == number, interruption
            data = disk.read_bytes()
            # A formatted root proves execution reached the table stage.
            assert data[16448*512+1080:16448*512+1082] == b'\x53\xef', interruption
            if data[:16448*512] != initial[:16448*512]:
                if interruption == 'partial table' and data[512:514] == b'PM':
                    raise MutationDetected('restore')
                raise AssertionError(interruption)
            s.keys('clear\nmount /hd0 hd0 ext2\n')
            s.wait(lambda: 'mount: failed' in s.screen('unpublished'),
                   'interrupted install stays private', 30)
        confirm('y')
        success = 'Installation complete!' if entry == 'install' else 'Installation Complete'
        assert success in s.screen('committed')
        s.keys('clear\nmount /hd0 hd0 ext2\nls /hd0/sys\n')
        def listed():
            listing = s.screen('published')
            if 'mount: failed' in listing:
                raise MutationDetected('publish')
            return 'shell.bin' in listing.lower()
        s.wait(listed, 'shell file through the public mount', 60)
    finally:
        s.close()
    verify_disk(disk, out, media)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    hashes = set()
    with tempfile.TemporaryDirectory(prefix='ppc-deploy-') as directory:
        for case in ['normal'] + (list(MUTATIONS) if args.mutate else []):
            out = Path(directory)/case
            media = build(out, case)
            check_media(media, out)
            digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
            assert digest not in hashes
            hashes.add(digest)
            print(case, digest, flush=True)
            if args.build_only:
                continue
            for entry in ('install', 'cdinst') if case == 'normal' else ('install',):
                try:
                    runtime(media, out, entry, case)
                except MutationDetected as error:
                    if str(error) != case:
                        raise
                    print(case, 'runtime RED', flush=True)
                else:
                    assert case == 'normal', f'{case} survived'
    print(f'PPC deploy {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    main()
