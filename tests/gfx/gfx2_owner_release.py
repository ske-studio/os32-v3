"""Exercise C15 quarantine and X1 teardown of an address space with a live lock."""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.gfx2_host import BACKING_SOURCES, run_host
from tests.support.media import main


def verify(config):
    owner_release = "sys/hal/gfx/v2/owner_release.c"
    run_host("gfx2_owner_release", [owner_release],
             {1: "teardown quarantines a busy backing"}, gc_sections=True)
    # X1 is static inside the exec module. Compile the full candidate function
    # with its real AppSlot/addrspace types and simulated non-graphics services.
    source = (ROOT / "sys/kern/exec/exec_finish.inc").read_text()
    start = source.index("static void exec_teardown_app(AppSlot *a)")
    end = source.index("\n}", start) + 2
    with tempfile.TemporaryDirectory() as directory:
        body = Path(directory) / "teardown.inc"
        body.write_text(source[start:end])
        run_host("gfx2_backing", [*BACKING_SOURCES, owner_release],
                 {13: "exit releases locked pages", 14: "failed unmap poisons address space"},
                 gc_sections=True, cflags=[f'-DGFX2_TEARDOWN_BODY="{body}"'])


if __name__ == "__main__":
    main(lambda config: None, verify)
