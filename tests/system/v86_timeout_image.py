"""Check the generated timeout IPL and the full RAW floppy layout."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.fixtures.v86_timeout.generate import generate
from tests.support.media import Failure, main

IMAGE = Path("build/out/tests/v86_timeout.img")


def verify(config):
    image = IMAGE.read_bytes()
    if len(image) != 1_261_568:
        raise Failure(f"unexpected RAW size: {len(image)}")
    if image[:3] != b"\xfb\xeb\xfe":
        raise Failure("invalid timeout IPL")
    if any(image[3:]):
        raise Failure("nonzero data after IPL")


if __name__ == "__main__":
    main(lambda config: generate(IMAGE), verify)
