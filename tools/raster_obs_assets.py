#!/usr/bin/env python3
"""Generate independent N01..N16 observation assets and a deployment manifest."""
import argparse
import csv
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from PIL import __version__ as pillow_version

import img2osri

CONVERTER = Path(__file__).resolve().with_name('img2osri.py')
MANIFEST_FIELDS = (
    'N', 'source_path', 'source_sha256', 'status', 'reason', 'exit_code',
    'bytes', 'events', 'written_colors', 'actual_changed_colors_min',
    'actual_changed_colors_max', 'hblank_outs', 'total_outs', 'crc32',
    'tool_sha', 'library_versions', 'args',
)


def generate_assets(source, output_dir):
    source = Path(source).resolve()
    output_dir = Path(output_dir)
    source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
    tool_sha = hashlib.sha256(CONVERTER.read_bytes()).hexdigest()
    output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for colors in range(1, img2osri.PALETTE_COLORS + 1):
        stem = f'N{colors:02d}'
        output = output_dir / (stem + '.osri')
        # A failed regeneration must never leave an old deployable asset.
        output.unlink(missing_ok=True)
        row = dict(N=colors, source_path=str(source), source_sha256=source_sha,
                   tool_sha=tool_sha,
                   library_versions=f'Pillow={pillow_version} NumPy={np.__version__}')
        with tempfile.TemporaryDirectory(prefix=f'.{stem}-', dir=output_dir) as name:
            temporary = Path(name) / output.name
            command = [sys.executable, '-B', str(CONVERTER), str(source),
                       '--mode', 'PL-B', '--band', '1', '--obs-colors', str(colors),
                       '-o', str(temporary)]
            row['args'] = repr(command)
            try:
                result = subprocess.run(command, capture_output=True, text=True)
                log = result.stdout + result.stderr
                row['exit_code'] = result.returncode
                if result.returncode:
                    row.update(status='failed', reason=' '.join(log.split()) or
                               f'converter exited {result.returncode}')
                else:
                    row.update(img2osri.obs_statistics(temporary.read_bytes(), colors))
                    temporary.replace(output)
                    row.update(status='ok', reason='')
            except (OSError, ValueError) as exc:
                log = str(exc) + '\n'
                row.update(status='failed', reason=str(exc), exit_code=1)
            (output_dir / (stem + '.log')).write_text(log)
        rows.append(row)
        print(f'{stem}: {row["status"]} {row["reason"]}', flush=True)
    with (output_dir / 'manifest.tsv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=MANIFEST_FIELDS, delimiter='\t')
        writer.writeheader()
        writer.writerows(rows)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('--output-dir', type=Path, default=Path('assets'))
    args = parser.parse_args()
    generate_assets(args.input, args.output_dir)


if __name__ == '__main__':
    main()
