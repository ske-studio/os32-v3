#!/usr/bin/env python3
"""Package and measure the T4a-2 VA0 SQLite prototype (no deployment)."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import mkmod
from module_schema import CONSTANTS, POLICIES

ROOT = Path(__file__).resolve().parents[1]


def measure(directory, vmkernel):
    directory = Path(directory)
    elf = (directory / 'sqlite.elf').read_bytes()
    metadata = json.loads((directory / 'sqlite_imports.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    image, stats = mkmod.build(elf, metadata, Path(vmkernel).read_bytes(), spec['version'],
                              spec['generations']['memory_layout'], module_id=CONSTANTS['ID_SQLITE'])
    (directory / 'sqlite.bin').write_bytes(image)
    stats['policy'] = {k: v for k, v in POLICIES[CONSTANTS['ID_SQLITE']].items()
                       if k.endswith('_max')}
    stats['sections'] = dict(zip(mkmod.SECTION_NAMES, mkmod.Elf(elf).body(
        POLICIES[CONSTANTS['ID_SQLITE']])[2]))
    # ld's archive inclusion preamble records every actually selected member.
    link_map = (directory / 'sqlite.map').read_text()
    stats['libgcc_members'] = sorted(set(re.findall(r'libgcc\.a\(([^)]+)\)', link_map)))
    if not stats['libgcc_members']:
        raise ValueError('missing explicit libgcc member ledger')
    stats['sqlite_version'] = re.search(r'#define SQLITE_VERSION\s+"([^"]+)"',
                                       (ROOT / 'lib/sqlite3/sqlite3.c').read_text())[1]
    stats['elf_sha256'] = hashlib.sha256(elf).hexdigest()
    stats['pool_in_body'] = False
    stats['configuration'] = 'full amalgamation; GNU89 -Os; product config; local kstring/kmath; external 512KiB pool; no stack in body'
    (directory / 'measurements.json').write_text(json.dumps(stats, indent=2) + '\n')
    print(json.dumps(stats, ensure_ascii=False, indent=2))
    return image, stats


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--vmkernel', type=Path, required=True)
    args = parser.parse_args()
    measure(args.directory, args.vmkernel)
