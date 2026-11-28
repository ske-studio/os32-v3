"""A held local key reaches the resident shell while it polls for a line."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main


def check(config):
    cc = Path(os.environ["PPC_CROSS_DIR"]) / "bin/powerpc-elf-gcc"
    fixture = Path("tests/support/fixtures/input_policy")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "rshell"
        subprocess.run([cc, "-std=gnu11", "-Os", "-msdata=none", "-ffreestanding",
                        "-ffunction-sections", "-fdata-sections", "-nostdlib",
                        "-Wl,--gc-sections", "-Wno-attributes", "-D__OS32_USERLAND__",
                        "-DOS32_CRT_RESIDENT", "-I.", "-Isdk/include", "-Isdk/include/os32",
                        "-Isys/platform/powermac", "-Isys/include", "-Iuserland/lib",
                        "-Iuserland/shell", "-Iuserland/lib/filer", fixture / "entry.S",
                        fixture / "rshell_repeat.c", "userland/shell/serial_watchdog.c",
                        "-lgcc", "-o", exe], check=True)
        if subprocess.run(["qemu-ppc", exe]).returncode:
            raise Failure("resident line missed deferred repeat")


if __name__ == "__main__":
    main(lambda config: None, check)
