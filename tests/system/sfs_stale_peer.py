"""Exercise the SerialFS client and both 82557 transports against a DMA/tick model."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import Failure, main
from tools.ops.kernel_includes import include_flags

FIXTURE = ROOT / "tests/support/fixtures/sfs_stale_peer"
SOURCES = [
    "sys/dev/net/e82557/lan_body.c", "sys/dev/net/e82557/e82557_desc.c",
    "sys/fs/serialfs/sfs_proto.c", "sys/fs/serialfs/sfs_client.c", "sys/fs/serialfs/sfs_lan.c",
    "sys/net/nic.c", "sys/net/nic_rx.c", "sys/net/rx_work.c",
    "sys/kern/kstring_c.c", "sys/kern/io_work.c", "sys/kern/io_policy.c",
    "sys/kern/idle_work.c", "sys/kern/irq/tick.c", "sys/kern/irq/tick_budget.c",
    "sys/hal/time/tick.c", "lib/crc32.c",
]


def build(config):
    subprocess.run(["make", "build/out/lan/lan_imports.h"], check=True)


def verify(config):
    cross = Path(os.environ.get("CROSS_DIR", Path.home() / "opt/cross")) / "bin"
    flags = [cross / "i386-elf-gcc", "-nostdlib", "-static", "-std=gnu11", "-ffreestanding",
             "-ffunction-sections", "-fdata-sections", "-fno-pie", "-O2", "-D__cdecl=",
             "-I" + str(FIXTURE), "-I.", "-Ibuild/out/lan", *include_flags(ROOT)]
    with tempfile.TemporaryDirectory() as tmp:
        facade = Path(tmp) / "facade.o"
        exchange = Path(tmp) / "exchange"
        subprocess.run([*flags, "-c", FIXTURE / "facade.c", "-o", facade], check=True)
        # The resident facade and module normally have separate link images.
        symbols = subprocess.check_output([cross / "i386-elf-nm", "--defined-only", "-g", facade],
                                          text=True).splitlines()
        names = [line.split()[-1] for line in symbols]
        aliases = [f"--redefine-sym={name}=facade_{name}" for name in names
                   if name.startswith(("e82557_", "sfs_lan_"))]
        subprocess.run([cross / "i386-elf-objcopy", *aliases, facade], check=True)
        subprocess.run([*flags, "-Wl,--gc-sections", FIXTURE / "exchange.c", facade, *SOURCES,
                        FIXTURE / "normal_unused.S", "-lgcc", "-o", exchange], check=True)
        results = [subprocess.run(["qemu-i386", exchange, *args]).returncode
                   for args in ([], ["module"])]
        if any(results):
            raise Failure("LAN exchange state mismatch")


if __name__ == "__main__":
    main(build, verify)
