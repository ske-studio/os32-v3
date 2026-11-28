"""Check the guest IPL's basic-bank register program after assembly."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main


def verify(config):
    with tempfile.TemporaryDirectory() as folder:
        image = Path(folder) / "fmtone.img"
        subprocess.run(["nasm", "-f", "bin", "userland/diag/soft86_tone.asm",
                        "-o", image], check=True)
        program = image.read_bytes()
    if len(program) != 77*2*8*1024 or any(program[1024:]):
        raise Failure("invalid IPL geometry")
    program = program[:1024]
    # The IPL uses the left-only, single-carrier sndl_test patch.
    tone = bytes.fromhex("30 01 40 0c 50 1f 80 0f b0 07 b4 80 a4 22 a0 69 28 10")
    if program.count(tone) != 1:
        raise Failure("missing left tone")
    if tone + bytes.fromhex("28 00") not in program:
        raise Failure("missing key-off")


if __name__ == "__main__":
    main(lambda config: None, verify)
