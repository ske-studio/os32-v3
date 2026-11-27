"""Run a freestanding graphics contract against the candidate's product sources."""
from pathlib import Path
import subprocess
import tempfile

from tests.support.media import main
from tools.ops.kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def host_check(fixture, sources, observations, *, gc_sections=False):
    def verify(config):
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary) / "probe"
            subprocess.run([
                "gcc", "-m32", "-march=i386", "-std=gnu11", "-O2", "-ffreestanding",
                "-fno-pie", "-no-pie", "-fno-stack-protector", "-nostdlib", "-static",
                *(["-ffunction-sections", "-Wl,--gc-sections"] if gc_sections else []),
                "-I.", "-Isys/hal/gfx/v2", *include_flags(ROOT),
                f"tests/support/fixtures/gfx2/{fixture}.c", *sources, "-o", exe,
            ], cwd=ROOT, check=True)
            result = subprocess.run(["qemu-i386", exe], timeout=30)
            if result.returncode:
                reason = observations.get(result.returncode, f"probe exit {result.returncode}")
                raise SystemExit(f"FAIL {reason}")

    main(lambda config: None, verify)
