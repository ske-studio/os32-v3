#!/usr/bin/env python3
"""Record the isolated fault fixture images; never replace the product manifest."""
import hashlib
import json
from pathlib import Path
import sys


def main():
    root = Path(sys.argv[1])
    files = {}
    for name in ('kernel.bin', 'sqlite.bin', 'vmkernel.lz4'):
        data = (root / name).read_bytes()
        files[name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    (root / 'manifest.json').write_text(json.dumps(
        {'fixture': 'OS32_R1_FIXTURE', 'files': files}, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
