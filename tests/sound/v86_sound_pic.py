from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.fm_control import run
from tests.support.media import main


def verify(config):
    run("pic.c", ["sys/platform/pc98/legacy/v86_pic.c", "sys/platform/pc98/legacy/v86.c"],
        guest_sections=[(".ivt", 0), (".guest_stack", 0x20000), (".guest_code", 0x30000)])


if __name__ == "__main__":
    main(lambda config: None, verify)
