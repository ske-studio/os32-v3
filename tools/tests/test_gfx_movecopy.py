"""W1: real gshell modules with the integration host ABI, isolated mutations."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

CHECK = dict(name='gfx-movecopy-host', tier=1, needs=[], budget=30, covers=[
    'userland/gshell/src/**', 'userland/gshell/host/**', 'userland/gshell/build.rs',
    'sdk/rust/os32api/src/**', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/os32api_host.py', 'userland/lib/font/**',
    'tools/tests/test_gfx_movecopy.py'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('fep-source', 'wm::overlay_rects(st)', '[crate::modal::rect(), crate::taskbar::rect(st), crate::startmenu::rect()]'),
    ('dirty-source', 'visible::region_subtract_region(&old_vis, &st.windows[index].dirty)',
     'visible::region_subtract_region(&old_vis, &RectSet::EMPTY)'),
    ('issued-source', 'visible::region_subtract_region(&valid, &st.windows[index].issued)',
     'visible::region_subtract_region(&valid, &RectSet::EMPTY)'),
    ('recompute-after-copy', '    visible::recompute_and_expose(st);', ''),
    ('composite-before-copy', '    if !copy_valid(&ok, old_org, new_org) { ok.clear(); }',
     '    wm::composite_rect(st, old_outer);\n    if !copy_valid(&ok, old_org, new_org) { ok.clear(); }'),
    ('copy-row-order', 'if new.1 > old.1 { n - step } else { step }', 'if false { n - step } else { step }'),
    ('copy-column-order', 'if new.0 > old.0 { rects.len - 1 - j } else { j }',
     'if false { rects.len - 1 - j } else { j }'),
    ('screen-intersection', 'let (next, all) = intersect(&ok, &screen);',
     'let (next, all) = (ok, true);'),
)


def run_case(work, libs, mutation=None):
    gshell = work / 'gshell'
    shutil.copytree(ROOT / 'userland/gshell/src', gshell / 'src')
    shutil.copytree(ROOT / 'userland/gshell/host', gshell / 'host')
    source = gshell / 'src/movecopy.rs'
    text = source.read_text()
    if mutation:
        name, old, new = mutation
        assert text.count(old) == 1, name
        text = text.replace(old, new)
        if name == 'recompute-after-copy':
            text = text.replace('    wm::composite_rect(st, old_outer);',
                                '    visible::recompute_and_expose(st);\n    wm::composite_rect(st, old_outer);')
    source.write_text(text)
    root = (gshell / 'src/lib.rs').read_text().replace('#![no_std]', '#![allow(dead_code)]')
    root = root.replace('#[no_mangle]', '').replace('pub extern "C" fn main(', 'pub extern "C" fn guest_main(')
    root = re.sub(r'^mod (\w+);', lambda m: f'#[path="{gshell / "src" / (m[1]+".rs")}"] mod {m[1]};', root, flags=re.M)
    root += f'\n#[path="{gshell / "host/mocks.rs"}"] mod mocks;\n'
    (work / 'root.rs').write_text(root)
    api, font, deps = libs
    exe = work / 'tests'
    result = subprocess.run(['rustc', '--edition=2021', '--test', str(work / 'root.rs'),
        '--extern', f'os32api={api}', '--extern', f'os32_font={font}', '-L', f'dependency={deps}',
        '-o', str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(exe), '--test-threads=1', 'movecopy::tests::'], capture_output=True, text=True)
    assert result.returncode == (101 if mutation else 0), result.stdout + result.stderr
    assert re.search(r'test result: \w+\. \d+ passed; \d+ failed', result.stdout), result.stdout
    print(('RED ' + mutation[0]) if mutation else 'PASS movecopy', flush=True)


def run(ctx):
    sys.path.insert(0, str(ROOT / 'tools/tests'))
    import os32api_host
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        api = os32api_host.build(work)
        font, deps = os32api_host.build_font()
        subprocess.run(['rustc', '--edition=2021', str(ROOT / 'userland/gshell/build.rs'),
                        '-o', str(work / 'diag-build')], check=True, capture_output=True)
        env = os.environ.copy()
        env['OUT_DIR'] = str(work)
        env['CARGO_MANIFEST_DIR'] = str(ROOT / 'userland/gshell')
        subprocess.run([str(work / 'diag-build')], env=env, check=True, capture_output=True)
        previous = os.environ.get('OUT_DIR')
        os.environ['OUT_DIR'] = str(work)
        try:
            for i, mutation in enumerate((None, *MUTATIONS, None) if ctx.mutate else (None,)):
                case = work / str(i)
                case.mkdir()
                run_case(case, (api, font, deps), mutation)
        finally:
            if previous is None:
                del os.environ['OUT_DIR']
            else:
                os.environ['OUT_DIR'] = previous
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    raise SystemExit(run(SimpleNamespace(mutate=args.mutate)))
