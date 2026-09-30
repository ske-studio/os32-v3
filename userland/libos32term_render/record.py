"""Run from repository root; append unabridged command output and status."""
import subprocess
import sys
from pathlib import Path
label, *command = sys.argv[1:]
p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
with Path('userland/libos32term_render/TDD_LOG.md').open('a') as f:
    f.write('\n## ' + label + '\n\n```text\n$ ' + ' '.join(command) + '\n' + p.stdout + '\nexit=' + str(p.returncode) + '\n```\n')
print(p.stdout)
print('exit=' + str(p.returncode))

sys.exit(p.returncode)
