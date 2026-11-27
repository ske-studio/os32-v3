from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main


def verify(config):
    fixture = Path("tests/support/fixtures/input_policy")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "gshell"
        subprocess.run(["rustc", "--edition=2021", "-Awarnings", fixture / "gshell.rs", "-o", exe], check=True)
        if subprocess.run([exe]).returncode:
            raise Failure("incorrect old-window release")


if __name__ == "__main__":
    main(lambda config: None, verify)
