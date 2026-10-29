"""M1 P3: audit failures survive -O; only the designated slot tables count."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT/'docs/decisions/realhw/HWDEV_CORE_API.md'


def main():
    with tempfile.TemporaryDirectory(prefix='hwdev-audit-') as td:
        document = Path(td)/'contract.md'
        source = DOC.read_text()
        # A matching-looking row elsewhere must not change the 26-function contract.
        document.write_text(source+'\n| 25 | `not_a_core_slot` | `not_a_header.h` |\n')
        command = [sys.executable, '-O', '-c',
                   'import sys; from pathlib import Path; sys.path.insert(0,"tools"); '
                   'import hwdev_audit as a; a.DOC=sys.argv[1]; '
                   'a.audit(Path("build/out"),Path(sys.argv[2]))', str(document), os.environ['CROSS_DIR']]
        subprocess.run(command, cwd=ROOT, check=True)
        document.write_text(source.replace('| 0 | `trial_begin` | `kdebug/recover.h` |', ''))
        p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        assert p.returncode != 0 and 'core slot order/count' in p.stderr, p
    print('PASS audit under -O: rejects missing slot, ignores rows outside section 2')

if __name__ == '__main__':
    main()
