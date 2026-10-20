"""TASK_FONTPERF F3: a .g16 bundle of the fetched JF Dot Shinonome 16 for host tests.

Runs the product generators (tools/g16/gen_preload16.py, tools/gen_g16.py) into
`work` — the same commands build/assets.mk runs — and returns the bundle path."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def g16_fixture(jf, work):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    pre, g16 = work / 'preload16.txt', work / 'JF-Dot-Shinonome16.g16'
    for cmd in ([ROOT / 'tools/g16/gen_preload16.py', '--ttf', str(jf), '-o', str(pre)],
                [ROOT / 'tools/gen_g16.py', '--ttf', str(jf), '--preload', str(pre), '-o', str(g16)]):
        r = subprocess.run([sys.executable, '-B', *map(str, cmd)], text=True, capture_output=True)
        assert r.returncode == 0, (cmd[0], r.returncode, r.stdout + r.stderr)
    return g16
