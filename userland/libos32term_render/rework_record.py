"""Record this reimplementation only; never modify historical TDD_LOG.md."""
import hashlib
import shlex
import subprocess
import sys
from pathlib import Path

root = Path('userland/libos32term_render')
label, *command = sys.argv[1:]
hashes = '\n'.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p}'
                   for p in sorted([*root.glob('src/*.rs'), *root.glob('tests/*.rs')]))
p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
with (root / 'REWORK_LOG.md').open('a') as f:
    f.write(f'\n## {label}\n\n```text\n{hashes}\n$ {shlex.join(command)}\n'
            + p.stdout + f'\nexit={p.returncode}\n```\n')
print(p.stdout)
print(f'exit={p.returncode}')
sys.exit(p.returncode)
