"""Build the unchanged product Rust WM with g3fix tests in a temporary copy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run():
    path = ROOT / 'userland/gshell/host/integration.py'
    # Reuse the existing builder, before its normal/mutation driver starts.
    marker = "\nexe = build(ROOT / 'userland/gshell', 'integration')"
    out_line = "OUT = ROOT / 'userland/gshell/target/gshell-host'"
    text = path.read_text()
    assert marker in text and out_line in text, 'integration.py の区切りが変わった'
    with tempfile.TemporaryDirectory(prefix='g3fix-wm-') as td:
        # 共有の target/gshell-host を check-gshell-host と取り合わないよう出力先を一時ディレクトリに。
        source = text.split(marker)[0].replace(out_line, 'OUT = Path(_PROBE_OUT)')
        ns = {'__file__': str(path), '__name__': 'g3fix_wm_probe', '_PROBE_OUT': str(Path(td) / 'out')}
        exec(compile(source, str(path), 'exec'), ns)
        gshell = Path(td) / 'gshell'
        for name in ('src', 'host'):
            shutil.copytree(ROOT / 'userland/gshell' / name, gshell / name)
        tests = gshell / 'host/wm_tests.rs'
        with tests.open('a') as out:
            out.write('\n' + Path(__file__).with_suffix('.rs').read_text())
        exe = ns['build'](gshell, 'g3fix-probe')
        subprocess.run([str(exe), '--test-threads=1', '--nocapture', 'g3fix_'], check=True)


if __name__ == '__main__':
    run()
