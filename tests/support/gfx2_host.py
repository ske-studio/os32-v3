"""Run a freestanding graphics contract against the candidate's product sources."""
from pathlib import Path
import subprocess
import tempfile

from tests.support.media import main
from tools.ops.kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def run_host(fixture, sources, observations, *, gc_sections=False, cflags=()):
    with tempfile.TemporaryDirectory() as temporary:
        exe = Path(temporary) / "probe"
        subprocess.run([
            "gcc", "-m32", "-march=i386", "-std=gnu11", "-O2", "-ffreestanding",
            "-fno-pie", "-no-pie", "-fno-stack-protector", "-nostdlib", "-static",
            *(["-ffunction-sections", "-Wl,--gc-sections"] if gc_sections else []),
            *cflags, "-I.", "-Isys/hal/gfx/v2", *include_flags(ROOT),
            f"tests/support/fixtures/gfx2/{fixture}.c", *sources, "-o", exe,
        ], cwd=ROOT, check=True)
        result = subprocess.run(["qemu-i386", exe], timeout=30)
        if result.returncode:
            reason = observations.get(result.returncode, f"probe exit {result.returncode}")
            raise SystemExit(f"FAIL {reason}")


def host_check(fixture, sources, observations, *, gc_sections=False, cflags=()):
    def verify(config):
        run_host(fixture, sources, observations, gc_sections=gc_sections, cflags=cflags)

    main(lambda config: None, verify)


# Product units used by the public submission and mapping fixture.
BACKING_SOURCES = (
    "sys/hal/gfx/v2/backend_reg.c",
    "sys/hal/gfx/v2/surface.c",
    "sys/hal/gfx/v2/sysmem.c",
    "sys/hal/gfx/v2/gart.c",
    "sys/hal/gfx/v2/lock.c",
    "sys/hal/gfx/v2/submit.c",
    "sys/hal/gfx/v2/check_op.c",
    "sys/hal/gfx/v2/format.c",
    "sys/hal/gfx/v2/fence.c",
    "sys/hal/gfx/v2/vram_heap.c",
    "sys/hal/gfx/v2/sw_alpha.c",
    "sys/hal/gfx/v2/sw_ckey.c",
    "sys/hal/gfx/v2/sw_copy.c",
    "sys/hal/gfx/v2/sw_fill.c",
    "sys/hal/gfx/v2/sw_hostdata.c",
    "sys/hal/gfx/v2/sw_line.c",
    "sys/hal/gfx/v2/sw_mono.c",
    "sys/hal/gfx/v2/sw_readback.c",
    "sys/hal/gfx/v2/sw_rop.c",
    "sys/hal/gfx/v2/sw_rop4.c",
    "sys/hal/gfx/v2/sw_stretch.c",
    "sys/hal/gfx/v2/sw_table.c",
    "lib/divdi3.c",
    "sys/hal/gfx/v2/display.c",
    "sys/hal/gfx/v2/v1_shim.c",
)
