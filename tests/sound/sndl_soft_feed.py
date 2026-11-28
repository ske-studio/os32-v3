"""Run the ordinary OPNA pump while a simulated PCM device consumes frames."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main


def verify(config):
    with tempfile.TemporaryDirectory() as folder:
        exe = Path(folder) / "feed"
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-D__cdecl=", "-I.", "-Isdk/include/os32",
                        "-Isdk/include", "tests/support/fixtures/sndl_soft_feed/feed.c",
                        "userland/lib/snd/mix.c", "-o", exe], check=True)
        subprocess.run(["qemu-x86_64", exe], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
