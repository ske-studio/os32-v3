"""Observe the ime command's exit status and output through a stub KernelAPI."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import Failure, main


def verify(config):
    errors = []
    with tempfile.TemporaryDirectory() as tmp:
        obj = Path(tmp) / "ime.o"
        exe = Path(tmp) / "cli"
        cc = ["gcc", "-std=gnu11", "-D__cdecl=", "-Isdk/include/os32"]
        subprocess.run([*cc, "-Dmain=ime_main", "-c", "userland/cmds/ime.c", "-o", obj], check=True)
        subprocess.run([*cc, "tests/support/fixtures/ime_result/cli.c", obj, "-o", exe], check=True)
        for args, message in [
            (["on"], "FEP enabled."),
            (["mode", "hira"], "Mode switched to Hiragana."),
            (["mode", "kata"], "Mode switched to Katakana."),
        ]:
            states = ["unavailable", "available"]
            if args in (["on"], ["mode", "hira"]):
                states.append("stuck")
            for state in states:
                result = subprocess.run(["qemu-x86_64", exe, state, *args], capture_output=True, text=True)
                if state == "available":
                    ok = result.returncode == 0 and result.stdout == message + "\n" and not result.stderr
                else:
                    ok = result.returncode == 1 and not result.stdout and len(result.stderr.splitlines()) == 1 and result.stderr.strip()
                if not ok:
                    errors.append(f"{state} {' '.join(args)}: rc={result.returncode} stdout={result.stdout.strip()!r} stderr={result.stderr.strip()!r}")
    if errors:
        raise Failure(" | ".join(errors))


if __name__ == "__main__":
    main(lambda config: None, verify)
