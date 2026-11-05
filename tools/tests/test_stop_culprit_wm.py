"""Run real gshell host tests and the STOP culprit runtime mutations."""
from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('k80 STOP culprit priority removed', 'src/multiapp.rs',
     '    if culprit != 0 {\n        return culprit;\n    }', '',
     'k80_stop_culprit_priority'),
    ('k80 STOP reserved priority removed', 'src/multiapp.rs',
     'let target = if culprit != 0 { culprit } else { mm.abort_kill_target };',
     'let target = mm.abort_kill_target; let _ = culprit;',
     'k80_stop_culprit_replaces_reserved_target'),
]

if __name__ == '__main__':
    mutate = '--mutate' in sys.argv
    # integration.py builds the real modules and runs the normal control first.
    sys.argv = [str(ROOT / 'userland/gshell/host/integration.py')]
    host = runpy.run_path(sys.argv[0])
    if mutate:
        host['MUTATIONS'][:] = MUTATIONS
        raise SystemExit(1 if host['mutate']() else 0)
