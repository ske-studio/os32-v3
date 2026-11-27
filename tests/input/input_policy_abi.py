"""Compile the private delivery layout with the two production C compilers."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main


def verify(config):
    fields = {
        "KbdPolicyAction": "action command_gen time_ms value",
        "KbdPolicyKey": ("src_generation map_id keycode sc_down_repeat physical_mods "
                         "logical_mods time_ms map_generation keysym kind action value"),
    }
    assertions = ['#include "input_delivery.h"']
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
    result = subprocess.run([cc, "-std=c89", "-Isys/input", "-Isys/include",
                             "-x", "c", "-fsyntax-only", "-"], input="\n".join(assertions),
                            text=True)
    if result.returncode:
        raise Failure("invalid delivery layout")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "wire"
        command = [cc, "-std=gnu11", "-Os", "-ffunction-sections", "-fdata-sections",
                        "-ffreestanding", "-fno-pie", "-nostdlib", "-static", "-Wl,--gc-sections",
                        "-I.", "-I" + str(fixture), "-Isys/include", "-Isys/input", "-Isdk/include/os32",
                        *(["-msdata=none", "-msoft-float"] if prefix == "powerpc" else []),
                        "-DRUST_WIRE=" + wire_initializer, "-fno-builtin", "sys/kern/kstring_c.c",
                        "sys/input/input_policy.c", "sys/input/input_delivery.c", "sys/input/input_context.c",
                        "-Isys/kern/exec", "-Isys/kern/mm", "-D__cdecl=",
                        "-Isys/arch/" + ("ppc" if prefix == "powerpc" else "x86"),
                        "-Isys/platform/" + ("powermac" if prefix == "powerpc" else "pc98"),
                        "sys/kapi/input_policy_provider.c",
                        "sys/hal/input/keymap_registry.c", "sys/hal/input/keymap_eval.c",
                        "sys/input/keymaps/hid_jp.c", "sys/input/keymaps/pc98_jp.c",
                        "sys/platform/powermac/input_keymap_backend.c" if ppc else "sys/platform/pc98/input_keymap.c",
                        "-Isys/hal/input", "sys/hal/input/legacy_mode.c", "sys/hal/input/legacy_ring.c",
                        "sys/hal/input/legacy_keymap.c", fixture / "wire.c", fixture / "entry.S"]
        runner = "qemu-ppc" if ppc else "qemu-i386"
        failure_probe = ["-DFAIL_INIT", "-Wl,--wrap=ikm_policy_init", "-Iuserland/ppc/sh/include",
                         "-Isdk/include", "userland/shell/ui.c", fixture / "legacy_shell.c"]
        for extra in ([], failure_probe):
            subprocess.run(command + extra + ["-lgcc", "-o", exe], check=True)
            if subprocess.run([runner, exe]).returncode:
                raise Failure("private provider or v2 LE mismatch")

if __name__ == "__main__":
    main(lambda config: None, verify)
