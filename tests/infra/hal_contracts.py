from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main
from tests.support.product import recipes


def verify(config):
    objects, _ = recipes(config)
    sources = ["sys/platform/pc98/d2_bind.c", "sys/hal/input/input_service.c",
               "sys/hal/time/clock_service.c", "tests/support/hal_pc98.c"]
    pc98 = config == "pc98"
    if not pc98:
        sources = ["sys/platform/powermac/system.c", "sys/platform/powermac/input_boot.c",
                   "sys/platform/powermac/usb_platform.c", "sys/kern/input_legacy.c",
                   *sources[1:3], "tests/support/hal_powermac.c"]
    sources.append("sys/hal/input/mouse_pointer.c")
    if pc98:
        sources.append("sys/dev/char/rtc.c")
    commands = {s: cmd for s, cmd in objects.values()}
    folder = Path("build/out/hal-contract-probe")
    folder.mkdir(parents=True, exist_ok=True)
    # Interpose only on the registration calls to validate the installed
    # resident tables, then forward them to the unmodified service.
    tables = folder / "registrations.c"
    code = []
    wrap = []
    for service, typename, header in (
            ("input", "InputOps", "sys/hal/input/input_service.h"),
            ("pointer", "PointerOps", "sys/hal/input/pointer_service.h"),
            ("clock", "ClockOps", "sys/hal/time/clock_service.h")):
        body = Path(header).read_text()
        record = re.search(r"typedef struct(?: " + typename + r")?\s*\{([^{}]*)\} " + typename + ";", body)
        if not record:
            raise OSError(f"cannot inspect {typename}")
        fields = re.findall(r"\(\*(\w+)\)\s*\(", record[1])
        if not fields:
            raise OSError(f"missing function pointers in {typename}")
        call = service + "_register"
        code.extend([f'#include "{header}"', f"void __real_{call}(const {typename} *);",
                     f"void __wrap_{call}(const {typename} *ops) {{",
                     "    if (!ops || " + " || ".join("!ops->" + field for field in fields) + ") __builtin_trap();",
                     f"    __real_{call}(ops);", "}"])
        wrap.append("-Wl,--wrap=" + call)
    tables.write_text("\n".join(code))
    sources.append(str(tables))
    compiled = []
    for index, src in enumerate(sources):
        cmd = commands.get(src, commands[sources[0]])
        flags = [x for x in cmd if x.startswith(("-I", "-D"))]
        flags.extend(["-D__cdecl=", "-DCHECK_INPUT=" + str(int(config != "shell"))])
        if not pc98:
            flags.extend(["-msdata=none", "-msoft-float", "-fno-short-enums"])
        if src == "sys/dev/char/rtc.c":
            (folder / "io.h").write_text("u8 inp(u16); void outp(u16, u8); void io_wait(void);\n")
            flags.insert(0, "-I" + str(folder))
        obj = folder / f"{index}.o"
        subprocess.run([cmd[0], *flags, "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                        "-m32", "-c", src, "-o", obj], check=True)
        compiled.append(obj)
    # Only unrelated ports and unexercised operations may trap. A call to an
    # unexpected device operation terminates the probe immediately.
    combined = folder / "combined.o"
    prefix = commands[sources[0]][0].removesuffix("gcc")
    subprocess.run([prefix + "ld", "-r", *compiled, "-o", combined], check=True)
    names = subprocess.check_output([prefix + "nm", "-u", combined], text=True).splitlines()
    stubs = folder / "devices.S"
    registrations = {"port_register", "con_output_register", "outp", "io_wait",
                     "kbd_stop_mouse_sample", "pointer_position"}
    stubs.write_text("\n".join(f".global {name}\n{name}: {instruction}"
                               for line in names for name in [line.split()[-1]] if not name.startswith("__real_")
                               for instruction in [("ret" if pc98 else "blr") if name in registrations else ("ud2" if pc98 else "trap")]))
    exe = folder / "probe"
    subprocess.run([prefix + "gcc", "-nostdlib", "-static", *compiled, stubs, *wrap, "-o", exe], check=True)
    if subprocess.run(["qemu-i386" if pc98 else "qemu-ppc", exe]).returncode:
        raise Failure("HAL event did not reach registered service")


if __name__ == "__main__":
    main(lambda config: None, verify)
