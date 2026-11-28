"""Exercise the private kdebug timing path using a controlled microsecond clock."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main


def check(config):
    fixture = Path("tests/support/fixtures/input_policy")
    cc = Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "measure"
        subprocess.run([cc, "-ffreestanding", "-nostdlib", "-static", "-D__cdecl=",
                        "-I.", "-Isys/include", "-Isdk/include/os32",
                        fixture / "entry.S", fixture / "measure.c", "-o", exe], check=True)
        if subprocess.run(["qemu-i386", exe]).returncode:
            raise Failure("input timing counters")


if __name__ == "__main__":
    main(lambda config: None, check)
