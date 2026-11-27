"""A USB Enter releases one queued LAN request after an incomplete serial line."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import link_media
from tests.system.ppc_link_session import boot

EARLY = "resumed before serial Enter"


def exercise(vm, op, late):
    deadline = vm.deadline
    op.wait(lambda: op.operator.ready and op.operator.ready["mode"] == "cui"
            and op.operator.ready["state"] != "running",
            min(deadline, time.monotonic() + 60), "initial cui READY")
    vm.hold_serial_line(deadline)
    vm.keys("echo half", deadline)
    rid, generation = op.submit("echo resumed", deadline)

    try:
        op.wait_delivery(rid, "acked", seconds=10)
        op.expect_pending(rid, "running", seconds=1)
    except Failure as exc:
        if str(exc) == f"early OUT before release: generation={generation} rid={rid}":
            result = op.expect_once(rid, generation, b"resumed\n", deadline)
            if result["state"] == "done" and result["drop"] == 0:
                raise Failure(EARLY) from None
        raise
    vm.key("ret", (), deadline)
    result = op.expect_once(rid, generation, b"resumed\n", deadline)
    if result["state"] != "done" or result["drop"]:
        raise Failure("incomplete resumed output")
    vm.check_serial_rejection(vm.screen(deadline), "echo resumed")


def verify():
    media = link_media(usb_trace=True)
    version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
    with ThreadPoolExecutor(2) as pool:
        jobs = [pool.submit(boot, cpu, "half-line", media, version, exercise)
                for cpu in ("7450", "7455")]
        results = [job.result() for job in jobs]
    if all(code == 0 for code, _ in results):
        return 0
    if all(result == (1, EARLY) for result in results):
        print(EARLY)
        return 1
    for cpu, (code, reason) in zip(("7450", "7455"), results):
        print(cpu, code, "early output" if reason == EARLY else reason)
    return max(code for code, _ in results)


if __name__ == "__main__":
    try:
        if sys.argv[2] == "build":
            require(Path("tests/support/fixtures/ppc_link/flow.inc"))
        else:
            sys.exit(verify())
    except (OSError, Failure, subprocess.CalledProcessError) as exc:
        print(exc)
        sys.exit(2)
