from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, PPC_MAKE, main, make, require


def build(config):
    make(*PPC_MAKE, "KBOOT_SH=1", "KBOOT_LINK=1", "common-compile")


def verify(config):
    # Expand the production input lists independently of the target prerequisites.
    rule = "list-inputs: ; @echo $(addprefix $(KB_OUT)/,$(filter %.c,$(C_COMMON_PC98_PENDING) $(C_COMMON_COMPILE_ONLY_PPC)))"
    names = subprocess.check_output(["make", "-s", *PPC_MAKE, "KBOOT_SH=1", "KBOOT_LINK=1",
                                     "--eval", rule, "list-inputs"], text=True).split()
    if not names:
        raise Failure("empty common source list")
    for name in names:
        obj = Path(name).with_suffix(".o")
        require(obj)
        with obj.open("rb") as file:
            header = file.read(20)
        if header[:6] != b"\x7fELF\x01\x02" or header[16:20] != b"\x00\x01\x00\x14":
            raise Failure(f"invalid PowerPC object {obj}")


if __name__ == "__main__":
    main(build, verify)
