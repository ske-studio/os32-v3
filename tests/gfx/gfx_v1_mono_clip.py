"""Compare public DIRECT MONO submissions against CPU rendering."""
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
        exe = Path(tmp) / "mono"
        subprocess.run(["gcc", "-m32", "-static", "-nostdlib", "-no-pie", "-fno-pie",
                        "-ffreestanding", "-fno-stack-protector", "-Os", "-std=gnu11",
                        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                        "-D__cdecl=", "-I.", *include_flags(ROOT),
                        "tests/support/fixtures/gfx_v1_mono_clip/submit.c",
                        "sys/hal/gfx/gfx_hw_submit.c", "sys/hal/gfx/gfx_hw_mono_cpu.c",
                        "sys/dev/video/geforce/gf_hook.c", "-o", exe], check=True)
        subprocess.run(["qemu-i386", exe], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
