"""Execute codec open, close and reclaim with injected WSS and DMA responses."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main
from tools.ops.kernel_includes import include_flags


def verify(config):
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "open"
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-D__cdecl=", "-Itests/support/fixtures/pcm_route",
                        "-I.", *include_flags(ROOT), "tests/support/fixtures/pcm_route/open.c",
                        "sys/dev/sound/cs4231/pcm_cs4231_math.c", "sys/dev/sound/cs4231/pcm_hw.c",
                        *[f"sys/hal/sound/snd2_{name}.c" for name in
                          ("reg", "caps", "stream", "stg", "seg")], "-o", exe], check=True)
        owner = Path(tmp) / "owner"
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-D__cdecl=", "-Itests/support/fixtures/pcm_route",
                        "-I.", *include_flags(ROOT), "tests/support/fixtures/pcm_route/owner.c",
                        "sys/dev/dma/dma8237.c", "sys/dev/dma/dma8237_math.c", "lib/dma_contract.c", "-o", owner], check=True)
        subprocess.run(["qemu-x86_64", owner], check=True)
        for case in ("normal", "pi-clear", "hal-drain"):
            subprocess.run(["qemu-x86_64", exe, "32", case, "reserved-bit"], check=True)
        for bios in (0x20, 0x61, 0x23):
            for case in ("normal", "irq", "dma", "mismatch", "restore", "drs", "drain",
                         "hal-drain", "hal-reclaim", "hal-restore", "pi", "hal-pi", "pi-clear", "pi-readback"):
                for mode in ("absent", "fm"):
                    subprocess.run(["qemu-x86_64", exe, str(bios), case, mode], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
