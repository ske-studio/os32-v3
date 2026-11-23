"""Build matching PowerMac boot and minimal packages from the paired product files."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def installer_packages(volume_path, root_iso, loader, product, dictionary, cd, scratch):
    # Both installers consume the same paired distribution. The ELF symbols
    # are translated to offsets in the actual HFS data forks at build time.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from mkmod import Elf
    from mkpkg import build_pkg, read_kapi_version

    volume = volume_path.read_bytes()
    offsets = []
    for file, name in [(loader, 'pm_loader_plan'), (product, 'pm_expected_plan')]:
        data = file.read_bytes()
        base = volume.find(data)
        if base < 0 or volume.find(data, base+1) >= 0:
            raise ValueError(f'{file}: expected one complete ELF in the HFS template')
        elf = Elf(data, 'ppc-powermac')
        sym = elf.symbol(name)
        section = elf.sections[sym['shndx']]
        off = section['offset'] + sym['value'] - section['addr']
        if sym['size'] != 20 or data[off:off+20] != bytes(20):
            raise ValueError(f'{name}: invalid reserved plan field')
        offsets.append(base+off)
    desc = cd/'PLAN.BIN'
    crc = zlib.crc32(volume, zlib.crc32(struct.pack('>2I', *offsets)))
    desc.write_bytes(struct.pack('>5I', 0x504d4231, len(volume), crc, *offsets))
    version = read_kapi_version(str(Path(__file__).resolve().parents[2]))
    (cd/'BOOT.PKG').write_bytes(build_pkg('BOOT', 1, [('/boot/plan.bin', desc),
                                 ('/boot/boot.hfs', volume_path)], False, version))
    listing = subprocess.check_output(['isoinfo', '-i', root_iso, '-f'], text=True)
    files = [('/db/fep.db', dictionary)]
    for path in listing.splitlines():
        if not path.endswith(';1'):
            continue
        dest = path[:-2].rstrip('.').lower()
        if dest == '/db/fep.db':
            continue
        local = scratch / dest.lstrip('/')
        local.parent.mkdir(parents=True, exist_ok=True)
        with local.open('wb') as out:
            subprocess.run(['isoinfo', '-i', root_iso, '-x', path], stdout=out, check=True)
        files.append((dest, local))
    (cd/'MINIMAL.PKG').write_bytes(build_pkg('MINIMAL', 1, files, False, version))


if __name__ == '__main__':
    volume, root, loader, product, dictionary = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(dir=volume.parent) as tmp:
        work = Path(tmp)
        installer_packages(volume, root, loader, product, dictionary, work, work/'root')
        for name in ('PLAN.BIN', 'BOOT.PKG', 'MINIMAL.PKG'):
            (work/name).replace(volume.parent/name)
