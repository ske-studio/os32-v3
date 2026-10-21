"""Compile the real shell command with only its KAPI endpoints replaced."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="recover-cli-") as temp:
    binary = Path(temp) / "cli"
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=gnu11", "-Wall",
                    "-Wextra", "-Werror", str(ROOT / "tools/tests/recover_cli_host.c"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
