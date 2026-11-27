"""Observe reservation visibility and parent restoration in the owner table."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main


def verify(config):
    probe = """
#include "input_context.h"
unsigned ikm_policy_generation(void) { return 1; }
int in2_excl_keyboard(void) { return 0; }
int main(void) {
    KbdPolicySnapshot s;
    if (input_context_owner_reserve(1, 11, 0, 0, INPUT_CONTEXT_RUNNING)) return 1;
    if (!input_context_owner_select(1, 11) || !input_context_snapshot(&s)) return 2;
    input_context_owner_activate(1, 1);
    if (input_context_snapshot(&s) || s.owner != 1 || s.exec_gen != 11) return 3;
    if (input_context_owner_reserve(2, 12, 1, 11, INPUT_CONTEXT_RUNNING)) return 4;
    input_context_owner_activate(2, 1);
    if (input_context_owner_release(2, 12) || input_context_snapshot(&s)) return 5;
    return s.owner != 1 || s.exec_gen != 11;
}
"""
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        (folder / "io.h").write_text("static unsigned irq_save(void){return 0;}\n"
                                     "static void irq_restore(unsigned flags){(void)flags;}\n")
        result = subprocess.run(["cc", "-Werror=implicit-function-declaration", "-I" + directory,
                                 "-Isys/input", "-Isys/include", "-Isdk/include/os32",
                                 "sys/input/input_delivery.c", "sys/input/input_context.c",
                                 "-x", "c", "-", "-o", folder / "probe"], input=probe, text=True)
        if result.returncode or subprocess.run([folder / "probe"]).returncode:
            raise Failure("owner reservation visibility or recovery")
        result = subprocess.run([sys.executable, "tests/support/fixtures/input_policy/loader.py",
                        config, directory])
        if result.returncode:
            raise Failure("first arch entry or failed reservation")

if __name__ == "__main__":
    main(lambda config: None, verify)
