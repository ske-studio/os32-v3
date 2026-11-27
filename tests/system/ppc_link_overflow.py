"""Capture a scrolling remote command and verify the next request completes."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import link_media
from tests.system.ppc_link_session import boot


def exercise(vm, op, late):
    def command(line, expected):
        op.wait(lambda: op.operator.ready and op.operator.ready["mode"] == "cui"
                and op.operator.ready["state"] != "running" and op.ready_released(),
                vm.deadline, "missing cui READY")
        rid, generation = op.submit(line, vm.deadline)
        return op.expect_once(rid, generation, expected, vm.deadline)

    result = command("p3-overflow", None)
    if result["state"] != "truncated" or result["drop"] <= 0:
        raise Failure("missing positive drop in truncated output")
    freeze = vm.word("p3_freeze_rc", 0, vm.deadline)
    recovered = command("echo recovered", b"recovered\n")
    if recovered["state"] != "done" or recovered["drop"]:
        raise Failure("incomplete recovered output")
    if freeze == 0:
        raise Failure("positive drop with successful freeze")


if __name__ == "__main__":
    try:
        if sys.argv[2] == "build":
            require(Path("tests/support/fixtures/ppc_link/flow.inc"))
        else:
            media = link_media(overflow=True)
            version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
            status, reason = boot("7450", "overflow", media, version, exercise, seconds=480)
            print(reason)
            sys.exit(status)
    except (OSError, Failure, subprocess.CalledProcessError) as exc:
        print(exc)
        sys.exit(2)
