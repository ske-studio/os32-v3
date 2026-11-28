from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.input.input_work_hid import verify
from tests.support.media import main

if __name__ == "__main__":
    main(lambda config: None, lambda config: verify(config, viewer=True))
