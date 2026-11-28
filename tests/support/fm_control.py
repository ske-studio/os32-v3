"""Run the foreground FM queue, clock controller and virtual PIC on i386."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.ops.kernel_includes import include_flags


def run(fixture, sources, guest_sections=()):
    legacy = ROOT / "sys/platform/pc98/legacy"
    layout = [f"-Wl,--section-start={name}={address:#x}" for name, address in guest_sections]
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "control"
        subprocess.run(["gcc", "-m32", "-std=gnu11", "-O2", "-nostdlib", "-static",
                        "-fno-pie", "-no-pie", "-ffreestanding", "-fno-stack-protector",
                        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", *layout,
                        "-D__cdecl=", "-I.", "-I" + str(legacy), *include_flags(ROOT),
                        ROOT / "tests/support/fixtures/fm_control" / fixture, *sources, "-o", exe], check=True)
        # A host offset lets the guest map its IVT at zero without a native low mapping.
        base = ["-B", "0x100000000"] if guest_sections else []
        subprocess.run(["qemu-i386", *base, exe], check=True)
