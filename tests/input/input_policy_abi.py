"""Exercise the public keyboard policy ABI with both production C compilers."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main, make
from tests.support.product import flags, recipes


def verify_gui(fixture, tmp, raw, byteorder):
    if len(raw) != 4:
        raise Failure("missing legacy GUI raw word")
    # Compile whole keyboard items from the candidate. The pump entry, raw
    # decoder and window delivery modules are included directly by the fixture.
    source = Path("userland/gshell/src/input.rs").read_text()
    items = []
    for kind, name in [
        ("const", "MOD_SHIFT"), ("const", "MOD_CAPS"), ("const", "SC_SPACE"),
        ("static", "SC2A"), ("static", "SC2A_SHIFT"), ("fn", "translate"),
        ("struct", "Target"), ("fn", "focus_target"), ("fn", "capture"),
        ("fn", "capture_keyboard"), ("fn", "capture_keyboard_observed"),
    ]:
        start = re.search(rf"^(?:pub(?:\(crate\))? )?{kind} {name}\b", source, re.M)
        if not start:
            raise Failure(f"missing WM keyboard item {name}")
        end = {"const": ";", "static": "\n];", "fn": "\n}", "struct": "\n}"}[kind]
        stop = source.index(end, start.end()) + len(end)
        items.append(source[start.start():stop])
    keyboard = Path(tmp) / "keyboard.rs"
    keyboard.write_text("\n".join(items))
    exe = Path(tmp) / "legacy_gui"
    subprocess.run(["rustc", "--edition=2021", "-Awarnings", "--cfg", "legacy_gui",
                    fixture / "gshell.rs", "-o", exe], check=True,
                   env={**os.environ, "WM_KEYBOARD_ITEMS": str(keyboard)})
    if subprocess.run([exe, str(int.from_bytes(raw, byteorder))]).returncode:
        raise Failure("legacy GUI window delivery mismatch")


def build(config):
    output = "build/out" if config == "pc98" else "build/out/ppc-kboot/link-sh"
    make(*flags(config), f"{output}/memmap_layout.h",
         "sys/kapi/kapi_generated.c" if config == "pc98" else f"{output}/gen/stamp")


def verify(config):
    fields = {
        "KbdPolicyAction": "action command_gen time_ms value",
        "KbdPolicyKey": ("src_generation map_id keycode sc_down_repeat physical_mods "
                         "logical_mods time_ms map_generation keysym kind action value"),
    }
    assertions = ['#include "os32_kbd_policy.h"']
    for record, names in fields.items():
        for index, name in enumerate(names.split()):
            assertions.append(f'typedef char offset_{record}_{name}'
                              f'[__builtin_offsetof({record}, {name}) == {index * 4} ? 1 : -1];')
        assertions.append(f'typedef char size_{record}'
                          f'[sizeof({record}) == {4 * len(names.split())} ? 1 : -1];')
    fixture = Path("tests/support/fixtures/input_policy")
    with tempfile.TemporaryDirectory() as tmp:
        rust = Path(tmp) / "layout"
        subprocess.run(["rustc", "--edition=2021", fixture / "layout.rs", "-o", rust], check=True)
        wire = subprocess.check_output([rust])
        if len(wire) != 32:
            raise Failure("v2 LE row mismatch")
        wire_initializer = ",".join(str(byte) for byte in wire)
    ppc = config == "link-sh"
    prefix = "powerpc" if ppc else "i386"
    variable = "PPC_CROSS_DIR" if ppc else "CROSS_DIR"
    cc = Path(os.environ[variable]) / "bin" / (prefix + "-elf-gcc")
    result = subprocess.run([cc, "-std=c89", "-Isdk/include/os32",
                             "-x", "c", "-fsyntax-only", "-"], input="\n".join(assertions),
                            text=True)
    if result.returncode:
        raise Failure("invalid delivery layout")
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        generated, recipe = next((source, cmd) for source, cmd in recipes(config)[0].values()
                                 if Path(source).name == "kapi_generated.c")
        init = (Path(generated).parents[2] / "sys/kern/exec/exec_kapi_init.inc"
                if ppc else Path("sys/kern/exec/exec_kapi_init.inc"))
        # Link the production wrappers and their generated table assignments;
        # other services are outside this fixture's link closure.
        lines = init.read_text().splitlines()
        (folder / "policy_table.h").write_text(
            "\n".join(line for line in lines if line.startswith("extern ") and "wrap_kbd_policy_" in line)
            + "\nstatic void bind_policy(KernelAPI *kapi) {\n"
            + "\n".join(line for line in lines if line.strip().startswith("kapi->kbd_policy_"))
            + "\n}\n")
        wrap = folder / "wrap.o"
        subprocess.run([cc, "-ffunction-sections", "-fdata-sections", "-ffreestanding",
                        "-fno-pie", "-fno-builtin", "-std=gnu11",
                        *(arg for arg in recipe[1:] if arg.startswith(("-I", "-D"))),
                        *(["-msdata=none", "-msoft-float"] if ppc else []),
                        "-c", generated, "-o", wrap], check=True)
        exe = folder / "wire"
        command = [cc, "-std=gnu11", "-Os", "-ffunction-sections", "-fdata-sections",
                        "-ffreestanding", "-fno-pie", "-nostdlib", "-static", "-Wl,--gc-sections",
                        "-I.", "-I" + tmp, "-I" + str(fixture), "-Isys/include", "-Isys/input", "-Isdk/include/os32",
                        *(["-msdata=none", "-msoft-float"] if prefix == "powerpc" else []),
                        "-DRUST_WIRE=" + wire_initializer, "-fno-builtin", "sys/kern/kstring_c.c",
                        "sys/input/input_policy.c", "sys/input/input_delivery.c", "sys/input/input_context.c",
                        "-Isys/kern", "-Isys/kern/exec", "-Isys/kern/mm", "-D__cdecl=",
                        "-Isys/arch/" + ("ppc" if prefix == "powerpc" else "x86"),
                        "-Isys/platform/" + ("powermac" if prefix == "powerpc" else "pc98"),
                        "sys/kapi/input_policy_provider.c",
                        "sys/hal/input/keymap_registry.c", "sys/hal/input/keymap_eval.c",
                        "sys/input/keymaps/hid_jp.c", "sys/input/keymaps/pc98_jp.c",
                        "sys/platform/powermac/input_keymap_backend.c" if ppc else "sys/platform/pc98/input_keymap.c",
                        "-Isys/hal/input", "sys/hal/input/legacy_mode.c", "sys/hal/input/legacy_ring.c",
                        "sys/hal/input/legacy_keymap.c", fixture / "wire.c", fixture / "entry.S", wrap]
        runner = "qemu-ppc" if ppc else "qemu-i386"
        failure_probe = ["-DFAIL_INIT", "-Wl,--wrap=ikm_policy_init", "-Iuserland/ppc/sh/include",
                         "-Isdk/include", "userland/shell/ui.c", fixture / "legacy_shell.c"]
        for extra in ([], failure_probe):
            subprocess.run(command + extra + ["-lgcc", "-o", exe], check=True)
            result = subprocess.run([runner, exe], stdout=subprocess.PIPE)
            if result.returncode:
                raise Failure(f"public policy ABI or v2 LE mismatch (probe {result.returncode})")
            if extra:
                verify_gui(fixture, tmp, result.stdout, "big" if ppc else "little")

if __name__ == "__main__":
    main(build, verify)
