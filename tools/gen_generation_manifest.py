#!/usr/bin/env python3
"""D35 build set: IDs, canonical generations and hashes; no deployment."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    canon = json.loads((ROOT / 'sdk/kapi.json').read_text())
    paths = set()
    for pattern in ('build/out/kernel.bin', 'build/out/sqlite.bin', 'build/out/vmkernel.lz4',
                    'boot/*.bin', 'userland/**/*.bin', 'userland/*.shlib',
                    'build/sdk/**/*.a', 'build/sdk/**/*.o', 'build/sdk/**/*.h',
                    'build/sdk/**/*.ld', 'build/sdk/**/*.py', 'build/sdk/**/*.rs'):
        paths.update(p for p in ROOT.glob(pattern) if p.is_file())
    files = []
    for p in sorted(paths):
        files.append({'path': str(p.relative_to(ROOT)), 'size': p.stat().st_size,
                      'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
    # The set ID reflects the dirty source tree through the artifact hashes.
    build_id = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    manifest = {'build_id': build_id,
                'kernel_commit': subprocess.check_output(['python3', 'tools/gen_build_id.py', '--print'], cwd=ROOT, text=True).strip(),
                'kapi_version': canon['version'], 'generations': canon['generations'],
                'external': {p: 'not built (empty submodule)' if not (ROOT / p / 'Makefile').exists() else 'not included; rebuild with clean-external'
                             for p in ('apps', 'game')},
                'files': files,
                'link_inputs': [json.loads(p.read_text()) | {'path': str(p.relative_to(ROOT))}
                                for p in sorted(list((ROOT / 'userland').rglob('*.inputs.json')) + list((ROOT / 'build/out').glob('*.inputs.json')))]}
    out = ROOT / 'build/out/generations-manifest.json'
    out.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'generations-manifest: {len(files)} artifacts, {build_id[:16]}; apps/game excluded')

if __name__ == '__main__':
    main()
