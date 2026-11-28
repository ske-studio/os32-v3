import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main
from tests.support.product import flags
from tests.input.input_policy_abi import build_gui


def verify(config):
    fixture = Path("tests/support/fixtures/input_policy")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "focus"
        gui = build_gui(fixture, tmp)
        for image, includes in (("pc98", "INC_KERNEL"), ("link-sh", "KB_INC")):
            query = (f'input-focus-config: ; @echo $(filter -Isys/platform/% -Iplatform/%,'
                     f'$({includes})) $(filter sys/input/input_delivery.c,$(C_KERNEL))')
            selected = shlex.split(subprocess.check_output(
                ["make", "-s", "--no-print-directory", *flags(image), "--eval=" + query,
                 "input-focus-config"], text=True))

            subprocess.run([Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc", "-ffreestanding", "-fno-builtin", "-nostdlib", "-static", "-D__cdecl=", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                            "-I" + str(fixture), "-I.", "-Isys/include", "-Isys/hal/input",
                            "-Isdk/include/os32", *selected, "-Wl,--wrap=input_key_pressed",
                             "sys/hal/input/in2_excl.c", "sys/hal/input/in2_reclaim.c",
                            "sys/hal/input/legacy_ring.c", "sys/hal/input/kbd_merge.c", "sys/hal/input/input_work.c", "sys/kern/io_work.c", "sys/kapi/kapi_in2.c",
                            "sys/hal/input/keymap_eval.c", "sys/hal/input/keymap_state.c",
                            "sys/hal/input/keymap_registry.c", "sys/hal/input/legacy_mode.c",
                            "sys/input/input_policy.c", "sys/input/input_context.c", "sys/kern/kstring_c.c",
                            "sys/hal/input/legacy_keymap.c", "sys/input/keymaps/hid_jp.c",
                            fixture / "entry.S", fixture / "focus.c", "-o", exe], check=True)
            run = subprocess.run(["qemu-i386", exe], stdout=subprocess.PIPE)
            if run.returncode or subprocess.run([gui, "focus"], input=run.stdout).returncode:
                raise Failure("exclusive transition changed delivery")


if __name__ == "__main__":
    main(lambda config: None, verify)
