"""Cursor SHADOW regression and DIRECT GET/MONO/PUT sequence."""
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
    ('mono-color', 'color, -1, cx, cy, CUR_W, CUR_H)', '0, -1, cx, cy, CUR_W, CUR_H)'),
    ('pixel-restore', 'gfx_cmd_enqueue_put(cx, cy, CUR_W, CUR_H, st.cursor.saved.as_ptr(), CUR_W as u32, -1)',
     '{ for k in 0..NPIX { gfx_cmd_enqueue_put(cx + (k as i32) % CUR_W, cy + (k as i32) / CUR_W, 1, 1, st.cursor.saved.as_ptr().add(k), 1, -1); } 0 }'),
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
