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
    sender = op.agent.notifier
    last_ack = None
    execute_seq = None
    acknowledge = sender.ack

    def ack(sess, epoch, seq, window):
        nonlocal last_ack, execute_seq
        if (sess, epoch) == (sender.sess, sender.epoch) and seq < sender.next_seq:
            last_ack = seq
            pending = op.operator.pending
            for number, payload in sender.inflight:
                if pending and payload is pending["execute"]:
                    execute_seq = number
        acknowledge(sess, epoch, seq, window)

    sender.ack = ack

    def observe(stage):
        ready = op.latest_ready()
        pending = op.operator.pending
        vm.record("half-line", dict(stage=stage, ready_rid=ready,
                  ready_state=op.agent.ledger.get(ready, {}).get("st"),
                  generation=op.operator.generation, rid=pending["id"] if pending else None,
                  delivery=pending["execute"].delivery if pending else None,
                  execute_seq=next((n for n, payload in sender.inflight
                                    if pending and payload is pending["execute"]), execute_seq),
                  next_seq=sender.next_seq, inflight=[seq for seq, _ in sender.inflight],
                  ack=last_ack, window=sender.window, silent=sender.silent))

    try:
        run_half_line(vm, op, deadline, observe)
    except (Failure, TimeoutError) as exc:
        if isinstance(exc, TimeoutError) or "deadline" in str(exc) or str(exc) == "initial cui READY":
            observe("deadline")
        raise
    finally:
        sender.ack = acknowledge


def run_half_line(vm, op, deadline, observe):
    op.wait(lambda: op.operator.ready and op.operator.ready["mode"] == "cui"
            and op.operator.ready["state"] != "running" and op.ready_released(),
            min(deadline, time.monotonic() + 60), "initial cui READY")
    observe("before input")
    vm.hold_serial_line(deadline)
    vm.keys("echo half", deadline)
    rid, generation = op.submit("echo resumed", deadline)

    observe("submit")
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
