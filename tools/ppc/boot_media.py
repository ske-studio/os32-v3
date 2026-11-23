"""Package the final PowerMac ELF under its Open Firmware boot name."""
from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


@dataclass(frozen=True)
class Media:
    elf: Path
    map: Path
    iso: Path

    def verify(self):
        if not self.map.is_file():
            raise FileNotFoundError(self.map)
        contents = subprocess.check_output(['isoinfo', '-i', self.iso, '-x', '/PPCKERN.ELF;1'])
        if contents != self.elf.read_bytes():
            raise ValueError(f'{self.iso}: PPCKERN.ELF differs from {self.elf}')


def package(elf, iso, *assets):
    with tempfile.TemporaryDirectory(dir=iso.parent) as tmp:
        cd = Path(tmp) / 'cd'
        cd.mkdir()
        shutil.copyfile(elf, cd / 'PPCKERN.ELF')
        for asset in assets:
            shutil.copyfile(asset, cd / asset.name)
        image = Path(tmp) / 'boot.iso'
        subprocess.run(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop',
                        '-o', image, cd], check=True)
        image.replace(iso)


if __name__ == '__main__':
    package(*map(Path, sys.argv[1:]))
