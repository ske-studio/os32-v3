from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.fm_control import run
from tests.support.media import main


def verify(config):
    run("timer.c", ["sys/platform/pc98/legacy/v86_fm_timer.c", "sys/hal/sound/snd2_fmq.c"])


if __name__ == "__main__":
    main(lambda config: None, verify)
