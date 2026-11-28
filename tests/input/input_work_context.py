from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.input.input_work_hid import verify
from tests.support.media import Failure, main


def check(config):
    verify(config, boot=True, registration=True)
    verify(config, boot=True)
    verify(config, boot=True, occupied=True)
    verify(config, boot=True, full=True)

    source = Path("userland/gshell/src/multiapp.rs").read_text()
    functions = []
    for name in ("sync_av_focus", "av_window", "av_owner"):
        start = source.index("pub fn " + name + "(")
        functions.append(source[start:source.index("\n}", start) + 2])
    with tempfile.TemporaryDirectory() as tmp:
        items = Path(tmp) / "focus.rs"
        items.write_text("\n".join(functions))
        exe = Path(tmp) / "focus"
        subprocess.run(["rustc", "--edition=2021", "-Awarnings", "--cfg", "real_focus",
            "tests/support/fixtures/input_policy/gshell.rs", "-o", exe], check=True,
            env={**os.environ, "WM_FOCUS_ITEMS": str(items)})
        if subprocess.run([exe]).returncode:
            raise Failure("WM same-owner focus notification")


if __name__ == "__main__":
    main(lambda config: None, check)
