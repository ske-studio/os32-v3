"""Observe sound initialization on the normal PC-98 boot path."""
from pathlib import Path
import re
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main
from tools.ops.kernel_includes import include_flags


def verify(config):
    pc98 = ROOT / "sys/platform/pc98"
    memory = (pc98 / "kernel_boot_memory.inc").read_text()
    machine = (pc98 / "kernel.c").read_text().split("void platform_boot_machine(void)", 1)[1]
    machine = re.sub(r'/\*.*?\*/|"(?:\\.|[^"\\])*"', '', machine, flags=re.S)
    path = machine.replace("kernel_boot_memory(mem_kb, &boot);", memory)
    calls = re.findall(r'\b(fm_module_load|pcm_module_load)\(\);', path)
    if calls != ["fm_module_load", "pcm_module_load"]:
        raise RuntimeError("PCM boot order")
    if not re.search(r'if\s*\(memory_boot_dma_available\(\)\)\s*\{?\s*pcm_module_load\(\);', path):
        raise RuntimeError("PCM boot DMA gate")

    start = machine.index("int snd_rc = fm_module_load();")
    end = machine.index("tvram_print", start)
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "boot_calls.inc").write_text(machine[start:end])
        subprocess.run([sys.executable, "-B", "tools/gen_sqlite_imports.py", "--module", "pcm",
                        "--out-dir", tmp], check=True)
        exe = Path(tmp) / "boot"
        subprocess.run(["gcc", "-m32", "-std=gnu11", "-O2", "-nostdlib", "-static",
                        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                        "-fno-pie", "-no-pie", "-ffreestanding", "-fno-stack-protector",
                        "-DTICK_PROBE", "-D__cdecl=", "-I" + tmp, "-Itests/support/fixtures/fm_probe", "-I.",
                        *include_flags(ROOT), "tests/support/fixtures/pcm_route/boot.c",
                        "-o", exe], check=True)
        rc = subprocess.run(["qemu-i386", exe]).returncode
        if rc == 2:
            raise RuntimeError("WSS diagnostic window")
        if rc:
            raise RuntimeError("PCM boot admission")


if __name__ == "__main__":
    main(lambda config: None, verify)
