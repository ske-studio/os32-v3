"""Exercise the common shell switch gate and restart loop in user-mode QEMU."""
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, require
from tests.support.product import recipes

FIXTURE = Path("tests/support/fixtures/shell_switch/gate.c")


def verify(config):
    ppc = config == "link-sh"
    prefix = "powerpc" if ppc else "i386"
    cc = Path(os.environ["PPC_CROSS_DIR" if ppc else "CROSS_DIR"]) / "bin" / (prefix + "-elf-gcc")
    exe = Path(os.environ["OS32_VERIFY_OUT"]) / "shell-switch"
    recipe = next(cmd for source, cmd in recipes(config)[0].values() if source == "sys/kern/gui.c")
    flags = [arg for arg in recipe if arg.startswith(("-I", "-D"))]
    if ppc:
        flags.extend(["-msdata=none", "-msoft-float"])
    subprocess.run([cc, *flags, "-O1", "-ffreestanding", "-fno-builtin", "-ffunction-sections",
                    "-fdata-sections", "-nostdlib", "-static", "-Wl,--gc-sections",
                    FIXTURE, "tests/support/fixtures/input_policy/entry.S", "sys/kern/gui.c",
                    "sys/kern/init/shell_loop.c", "sys/kern/kstring_c.c", "-lc", "-lgcc",
                    "-o", exe], check=True)
    rc = subprocess.run(["qemu-ppc" if ppc else "qemu-i386", exe], timeout=10).returncode
    if rc:
        raise Failure({1: "shell owner request rejected", 2: "switch request consumed twice",
                       3: "selected shell changed", 4: "invalid switch accepted"}.get(rc, f"probe rc={rc}"))


if __name__ == "__main__":
    main(lambda config: require(FIXTURE), verify)
