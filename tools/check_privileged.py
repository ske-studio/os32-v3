"""Locate the x86 disassembler for the product ISA audit."""
import os
import subprocess

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def objdump_bin():
    cross = os.environ.get("CROSS_DIR")
    if not cross:
        for env_file in (".env", ".env.sample"):
            path = os.path.join(PROJ_DIR, env_file)
            if os.path.isfile(path):
                for line in open(path):
                    line = line.strip()
                    if line.startswith("CROSS_DIR="):
                        cross = line.split("=", 1)[1].strip()
                        break
            if cross:
                break
    for cand in (
        os.path.join(cross, "bin", "i386-elf-objdump") if cross else None,
        "i386-elf-objdump",
    ):
        if not cand:
            continue
        try:
            subprocess.run([cand, "--version"], capture_output=True, check=True)
            return cand
        except (OSError, subprocess.CalledProcessError):
            continue
    return None
