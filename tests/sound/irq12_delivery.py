"""Exercise the PC-98 IRQ entry bodies with modeled privileged operations."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main
from tools.ops.kernel_includes import include_flags


def verify(config):
    fixture = ROOT / "tests/support/fixtures/irq12_delivery"
    source = (ROOT / "sys/arch/x86/kernel/isr_stub.asm").read_text()
    start = source.index("global irq_stub_2\n")
    end = source.index("IRETD_USER", source.index("global irq_stub_13\n")) + len("IRETD_USER")
    reflect_start = source.index("%macro V86_REFLECT 1")
    reflect_end = source.index("%endmacro", reflect_start) + len("%endmacro")
    bodies = source[reflect_start:reflect_end] + "\n" + source[start:end]
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "delivery"
        asm = Path(tmp) / "entry.asm"
        obj = Path(tmp) / "entry.o"
        asm.write_text((fixture / "model.inc").read_text() + bodies + "\n")
        subprocess.run(["nasm", "-felf32", asm, "-o", obj], check=True)
        subprocess.run([sys.executable, "-B", "tools/gen_sqlite_imports.py", "--module", "fm",
                        "--out-dir", tmp], check=True)
        subprocess.run(["gcc", "-m32", "-nostdlib", "-static", "-fno-pie", "-no-pie",
                        "-ffreestanding", "-fno-stack-protector", "-I" + tmp, "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections,--wrap=v86_reflect_irq", "-D__cdecl=", "-Itests/support/fixtures/fm_probe", "-I.",
                        *include_flags(ROOT), fixture / "model.c", obj,
                        "sys/platform/pc98/irq_policy.c", "sys/platform/pc98/legacy/v86.c", "sys/kern/irq/irq_math.c",
                        "sys/kern/irq/irq.c", "sys/platform/pc98/pic.c",
                        "sys/platform/pc98/irq_complete.c", "-o", exe], check=True)
        subprocess.run(["qemu-i386", exe], check=True)


if __name__ == "__main__":
    main(lambda config: None, verify)
