"""Cursor SHADOW regression and DIRECT GET/FILL sequence."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='gfx-cursor-host', tier=1, needs=[], budget=5, covers=[
    'userland/gshell/src/cursor.rs', 'tools/tests/gfx_cursor_host.rs',
    'tools/tests/test_gfx_cursor.py'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'userland/gshell/src/cursor.rs'
MUTATIONS = (
    ('mode', 'if unsafe { gfx_direct != 0 } {\n        st.cursor.saved', 'if false {\n        st.cursor.saved'),
    ('fill-width', 'gfx_cmd_enqueue_fill(px, py, 1, 1, c, 0)', 'gfx_cmd_enqueue_fill(px, py, 2, 1, c, 0)'),
)

def run_case(folder, mutation=None):
    source = SOURCE.read_text()
    if mutation:
        name, old, new = mutation
        assert source.count(old) == 1, name
        source = source.replace(old, new)
    cursor = folder / 'cursor.rs'
    cursor.write_text(source)
    harness = (ROOT / 'tools/tests/gfx_cursor_host.rs').read_text().replace('CURSOR_PATH', str(cursor))
    (folder / 'host.rs').write_text(harness)
    exe = folder / 'host'
    subprocess.run(['rustc', '--edition=2021', '--test', str(folder / 'host.rs'), '-o', str(exe)], check=True, capture_output=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    assert result.returncode == (101 if mutation else 0), result.stdout + result.stderr
    print('RED ' + mutation[0] if mutation else 'PASS cursor paths')

def run(ctx):
    for mutation in (None, *MUTATIONS, None) if ctx.mutate else (None,):
        with tempfile.TemporaryDirectory() as tmp:
            run_case(Path(tmp), mutation)
    return 0
