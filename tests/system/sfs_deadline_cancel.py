"""Exercise foreground expiry and sequence renewal with an integer transport clock."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main
from tools.ops.kernel_includes import include_flags


def verify(config):
    gcc = Path(os.environ.get("CROSS_DIR", Path.home() / "opt/cross")) / "bin/i386-elf-gcc"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "exchange"
        subprocess.run([gcc, "-ffreestanding", "-nostdlib", "-static",
                        "-fno-pie", "-fno-stack-protector", "-std=gnu11", "-O2", "-ffunction-sections",
                        "-fdata-sections", "-Wl,--gc-sections", "-D__cdecl=", "-I.",
                        *include_flags(ROOT), "tests/support/fixtures/sfs_deadline_cancel/exchange.c",
                        "sys/fs/serialfs/sfs_client.c", "sys/fs/serialfs/sfs_proto.c",
                        "lib/crc32.c", "sys/kern/kstring_c.c", "-o", exe], check=True)
        subprocess.run(["qemu-i386", exe], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
