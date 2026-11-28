"""Deliver real boot reports and clocked repeats through each machine adapter."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main
from tests.support.product import flags


def verify(config, viewer=False, boot=False, occupied=False, registration=False, full=False):
    ppc = config != "pc98"
    prefix = "powerpc" if ppc else "i386"
    cc = Path(os.environ["PPC_CROSS_DIR" if ppc else "CROSS_DIR"]) / "bin" / (prefix + "-elf-gcc")
    fixture = Path("tests/support/fixtures/input_policy")
    variable = "KB_INC" if ppc else "INC_KERNEL"
    includes = subprocess.check_output(["make", "-s", "--no-print-directory", *flags(config),
        "--eval=work-includes: ; @echo $(" + variable + ")", "work-includes"], text=True).split()
    sources = ["sys/kern/kstring_c.c", "sys/kern/io_work.c", "sys/kern/io_policy.c", "sys/kern/idle_work.c"]
    defines = []
    if viewer:
        sources.append(fixture / "osri_work.c")
        sources.extend("sys/platform/pc98/video/splash_raster" + suffix + ".c"
                       for suffix in ("", "_play", "_plan"))
        defines.append("-Wl,--wrap=pc98_splash_raster_state")
    else:
        sources.append(fixture / ("work_boot.c" if boot else "work.c"))
        sources.append("sys/platform/" + ("powermac/usb_key_report.c" if ppc else "pc98/usb_module.c"))
        hal = ["usb_input", "kbd_merge", "keymap_eval", "keymap_state", "keymap_registry",
               "legacy_keymap", "legacy_mode", "legacy_ring", "in2_dev"]
        # The boot-order probe also runs on the pre-6b source tree.
        if Path("sys/hal/input/input_work.c").exists():
            hal.append("input_work")
        sources.extend("sys/hal/input/" + name + ".c" for name in hal)
        sources.extend(["sys/dev/usb/hid_kbd_map.c", "sys/input/keymaps/hid_jp.c",
                        "sys/input/input_policy.c", "sys/input/input_delivery.c", "sys/input/input_context.c",
                        "sys/kern/io_kapi.c"])
        if not boot:
            sources.extend(["sys/kern/av_focus.c", "sys/hal/input/in2_excl.c", "sys/hal/input/in2_reclaim.c"])
        if occupied:
            defines.append("-DOCCUPIED")
        if full:
            defines.extend(["-DFULL", "-Wl,--wrap=io_register"])
        if registration:
            defines.append("-DCHECK_REGISTRATION")
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        (path / "arch_io.h").write_text((fixture / "arch_io.h").read_text() +
            "\nstatic inline int _irq_enabled(void){return !input_test_irq;}\n"
            "static inline void _idle(void){}\n"
            "static inline void _enable(void){input_test_irq=0;}\n")
        (path / "platform_io.h").write_text("static inline unsigned inp(unsigned port){return 0;}\n")
        if not ppc and not viewer and not boot:
            jump = path / "jump.o"
            subprocess.run(["nasm", "-f", "elf32", "sys/arch/x86/kernel/setjmp.asm", "-o", jump], check=True)
            inspector = path / "pending.c"
            inspector.write_text('#include "sys/hal/input/input_work.c"\n'
                'unsigned work_pending(void) { unsigned n=0; for(unsigned i=0;i<REPEAT_SLOTS;i++)'
                ' n+=slots[i].key.src!=0; return n; }\n')
            sources.remove("sys/hal/input/input_work.c")
            sources.extend(["sys/input/keymaps/pc98_jp.c", inspector, jump, fixture / "work_v86.c", "sys/platform/pc98/legacy/v86.c"])
        if viewer:
            (path / "platform_io.h").write_text(
                "unsigned osri_status(unsigned); void osri_out(unsigned,unsigned);\n"
                "static inline unsigned inp(unsigned port){return osri_status(port); }\n"
                "static inline void outp(unsigned port,unsigned value){osri_out(port,value); }\n")
        exe = path / "repeat"
        subprocess.run([cc, "-Os", "-ffunction-sections", "-fdata-sections", "-ffreestanding",
            "-fno-builtin", "-nostdlib", "-static", "-Wl,--gc-sections", "-D__cdecl=",
            *(["-msdata=none", "-msoft-float"] if ppc else []), "-I" + folder, "-I" + str(fixture),
            *includes, "-I.", *defines, fixture / "entry.S", *sources, "-lgcc", "-o", exe], check=True)
        result = subprocess.run(["qemu-ppc" if ppc else "qemu-i386", exe])
        if result.returncode:
            name = "OSRI frame" if viewer else "USB repeat delivery"
            raise Failure(f"{name} probe {result.returncode}")


if __name__ == "__main__":
    main(lambda config: None, verify)
