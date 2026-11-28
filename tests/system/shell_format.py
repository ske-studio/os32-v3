"""Exercise the shell's format command with a recording KAPI."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, require

FIXTURE = Path("tests/support/fixtures/shell_format/command.c")


def verify(config):
    cc = Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
    exe = Path(os.environ["OS32_VERIFY_OUT"]) / "shell-format"
    includes = (".", "sdk/include", "sdk/include/os32", "sys/include", "sys/arch/x86",
                "sys/platform/pc98", "sys/hal/input", "userland/lib", "userland/lib/filer")
    subprocess.run([cc, "-O2", "-ffunction-sections", "-fdata-sections", "-nostdlib",
                    "-D__OS32_USERLAND__", *["-I" + path for path in includes],
                    FIXTURE, "tests/support/fixtures/input_policy/entry.S",
                    "-Wl,--gc-sections", "-o", exe], check=True)
    if subprocess.run(["qemu-i386", exe], timeout=10).returncode:
        raise Failure("format drive, sector count or rejection mismatch")


if __name__ == "__main__":
    main(lambda config: require(FIXTURE), verify)
