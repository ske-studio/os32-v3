"""Recover the next playback IRQ after one undelivered edge."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main
from tools.ops.kernel_includes import include_flags


def verify(config):
    fixture = ROOT / "tests/support/fixtures/cs4231_tick_ack"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "playback"
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-D__cdecl=", "-I" + str(fixture), "-I.",
                        *include_flags(ROOT), fixture / "playback.c",
                        "sys/dev/sound/cs4231/pcm_cs4231_math.c", "-o", exe], check=True)
        subprocess.run(["qemu-x86_64", exe], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
