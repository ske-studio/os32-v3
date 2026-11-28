from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.fm_control import run
from tests.support.media import main, Failure


def smoke_guest(tmp):
    legacy = ROOT / "sys/platform/pc98/legacy"
    binary = tmp / "smoke.bin"
    subprocess.run(["nasm", "-f", "bin", legacy / "v86_test16.asm", "-o", binary], check=True)
    image = binary.read_bytes()
    array = (legacy / "v86.c").read_text().split("v86_test_code[] = {")[1].split("};")[0]

    # Replay the straight-line ISR's port writes on the production virtual PIC.
    listing = subprocess.check_output(["ndisasm", "-b16", binary], text=True)
    vector = int(re.search(r"mov word \[es:0x20\],(0x\w+)", listing)[1], 16)
    writes = []
    for line in listing.splitlines():
        address, _, insn = line.split(None, 2)
        if int(address, 16) < vector:
            continue
        if insn.startswith("mov al,"):
            al = int(insn.split(",")[1], 0)
        elif insn.startswith("out "):
            port = int(insn.split()[-1].split(",")[0], 0)
            writes.append(f"v86_pic_out({port}, {al});")
        elif insn == "iret":
            break
    fixture = tmp / "smoke.c"
    fixture.write_text("""
#include "v86_pic.h"
#include "tests/support/fixtures/fm_control/check.h"
void _start(void)
{
    u32 vector;
    v86_pic_reset();
    v86_pic_set_imr(0, 0xfe);
    for (int i = 0; i < 3; ++i) {
        v86_pic_request(0);
        check(v86_pic_candidate(&vector) == 0 && vector == 8, "smoke timer delivery");
        v86_pic_commit(0);
""" + "\n".join(writes) + """
    }
    passed();
}
""")
    run(fixture, [legacy / "v86_pic.c"])
    if image != bytes.fromhex(" ".join(re.findall(r"0x([0-9a-fA-F]{2})", array))):
        raise Failure("embedded smoke guest differs from assembly")


def verify(config):
    run("pic.c", ["sys/platform/pc98/legacy/v86_pic.c", "sys/platform/pc98/legacy/v86.c"],
        guest_sections=[(".ivt", 0), (".guest_stack", 0x20000), (".guest_code", 0x30000)])
    with tempfile.TemporaryDirectory() as tmp:
        smoke_guest(Path(tmp))


if __name__ == "__main__":
    main(lambda config: None, verify)
