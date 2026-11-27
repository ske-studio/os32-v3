"""Exercise both PC-98 FM addresses with a port model."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main
from tools.ops.kernel_includes import include_flags


def verify(config):
    fixture = ROOT / "tests/support/fixtures/fm_probe"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "probe"
        subprocess.run([sys.executable, "-B", "tools/gen_sqlite_imports.py", "--module", "fm",
                        "--out-dir", tmp], check=True)
        subprocess.run(["gcc", "-m32", "-nostdlib", "-static", "-fno-pie", "-no-pie",
                        "-ffreestanding", "-fno-stack-protector", "-I" + tmp, "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-D__cdecl=", "-I" + str(fixture), "-I.",
                        *include_flags(ROOT), fixture / "probe.c",
                        "sys/dev/sound/fm/fm_body.c", "sys/dev/sound/fm/opn_probe.c", "sys/kern/irq/irq_math.c",
                        "sys/kern/irq/irq.c", "sys/platform/pc98/irq_policy.c",
                        "sys/platform/pc98/irq_complete.c", "-o", exe], check=True)
        subprocess.run(["qemu-i386", exe], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
