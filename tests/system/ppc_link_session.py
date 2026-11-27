"""PowerMac resident remote shell, with early and late host agents."""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import link_media
from tests.support.ppc_peer import network, peer
from tests.support.ppc_vm import guest, guest_args
from host_rshell import EXECUTE, RSHELL

FAULTS = {"duplicate PRINT payload", "reader retained after owner exit",
          "resident fault without new generation"}


def check(condition, reason):
    if not condition:
        raise Failure(reason)


def exercise(vm, op, late):
    def limit(seconds):
        return min(time.monotonic() + seconds, vm.deadline)

    def word(name, offset=0):
        return vm.word(name, offset, vm.deadline)

    def wait(test, seconds, label):
        op.wait(test, limit(seconds), label)

    def ticks(count):
        start = word("tick_count")
        wait(lambda: (word("tick_count") - start) % 2**32 >= count, 30, "guest ticks stalled")

    def point(index, expected):
        fields = [word("shell_flow", 4*(12*index+i)) for i in range(12)]
        check(fields[3] and tuple(fields[4:9]) == expected, f"flow point {index}: {fields}")
        return fields

    def ready():
        return (op.agent.established and op.operator.ready and op.operator.ready["mode"] == "cui"
                and word("g_shell_service", 8) and word("exec_nest_level") == 1
                and word("cur_res_owner") == 1 and word("g_cur_app") == 0)

    def capture_end(end):
        check(word("g_capture") == 0 and word("g_reader") == 0xffffffff and
              word("p3_end_count") == (end+1) % 2**32 and word("p3_freeze_rc") == 0 and
              word("rshell_active") == 1 and word("boot_serial_mirror") == 0,
              "capture did not complete")

    def command(line, expected=None, seconds=20):
        wait(ready, 60, "missing cui READY")
        end = word("p3_end_count")
        deadline = limit(seconds)
        rid, generation = op.submit(line, deadline)
        data = op.expect_once(rid, generation, expected, deadline)
        check(data["state"] == "done" and data["drop"] == 0, f"truncated {rid}")
        capture_end(end)
        if data.get("duplicate"):
            raise Failure("duplicate PRINT payload")  # only after capture END and one-OUT held
        return data["body"][len(line.encode())+3:-1]

    observations = []
    def observe(reason):
        observations.append(reason)
        vm.record("mutation observation", reason)

    if late:
        op.enabled = False
        wait(lambda: word("g_shell_service", 8) and word("exec_nest_level") == 1 and
             word("cur_res_owner") == 1 and word("g_cur_app") == 0, 60, "late resident startup")
        ticks(300)
        op.enabled = True
    wait(ready, 60, "missing initial cui READY")
    resident = point(0, (1, 0, 1, 1, 0))
    check(resident[9:] == [0, vm.symbols["ppc_kapi_table"], vm.symbols["g_shell_service"]],
          "resident ABI arguments")
    try:
        command("p3-output", "FD1 UTF8 あ\n".encode())
    except Failure as exc:
        if str(exc) != "duplicate PRINT payload":
            raise
        observe(str(exc))
    screen = vm.screen(vm.deadline)
    check("FD1 UTF8" in screen and chr(0x2422) in screen and
          "> p3-output" not in screen and "KBOOT" not in screen, "framebuffer output")
    if not observations:
        for line, payload in (("pwd", b"/\n"), ("echo $PATH", b"/bin:/sbin:/usr/bin\n"),
                              ("echo $HOME", b"/home/user\n"), ("echo $SHELL", b"/shell\n"),
                              ('echo "two words" | cat', b"two words\n"),
                              ("cat /bin/sample.txt > /tmp/rsh", b""),
                              ("cat /tmp/rsh", "ASCII ｶﾅ 日本語\n".encode()),
                              ("p3-buffer", b"buffered line\n")):
            command(line, payload)
        # Charge only the help command to the additional long-output budget.
        started = time.monotonic()
        vm.deadline += 480
        help_text = command("help", seconds=480)
        vm.deadline -= 480 - (time.monotonic() - started)
        check(all(name in help_text for name in (b"rshell", b"echo", b"lspci")), "help command groups")
        count = sum(e["parts"][:2] == ["RSH", "READY"] for e in op.events)
        ticks(500)
        check(sum(e["parts"][:2] == ["RSH", "READY"] for e in op.events) > count, "READY retry")
        command("cat /no-such-file")
        check(re.fullmatch(rb"[1-9][0-9]*\n", command("echo $?")), "missing failure status")
        command("/bin/hello.bin ok", b"hello ok\n")
        command("echo $?", b"7\n")
        child = point(1, (2, 1, 2, 2, 2))
        check(child[2] and child[9] and child[10:] == [word("ring3_tramp_page"), 0], "child identity")
        point(2, (1, 0, 1, 1, 0))
        command("/bin/fault.bin")
        command("echo $?", b"139\n")
        point(3, (1, 0, 1, 1, 0))
        previous = len(op.events)
        for notice in (f"{op.operator.next_id-1:08x} echo duplicate\n".encode(),
                       b"00000001 echo old\n", b"00000100 "+b"x"*127+b"\n",
                       b"00000101 echo bad\necho bad\n"):
            sent = op.raw(RSHELL, EXECUTE, notice, vm.deadline)
            wait(lambda: sent.delivery == "acked", 10, "rejection notice not delivered")
        command("echo accepted", b"accepted\n")
        check(sum(e["parts"][:2] == ["RSH", "OUT"] for e in op.events[previous:]) == 1,
              "guest executed invalid notification")
    vm.key("esc", (), vm.deadline)
    wait(lambda: word("rshell_active") == 0 and "[Remote shell closed]" in vm.screen(vm.deadline),
         30, "local shell prompt")
    vm.keys("echo usb\n", vm.deadline)
    def usb_echo():
        screen = vm.screen(vm.deadline)
        return "OS32:/$ echo usb" in screen and re.search(r"(?m)^usb *$", screen)
    wait(usb_echo, 20, "USB echo input and output")
    previous = len(op.events)
    vm.keys("rshell\n", vm.deadline)
    def reopened():
        # The host records READY before sending its response. Keep servicing
        # the exchange until the guest releases it, including any retries.
        return ready() and word("rshell_active") and any(
            e["parts"][:2] == ["RSH", "READY"] and
            op.agent.ledger.get(e["link_rid"], {}).get("st") == "RELEASED"
            for e in op.events[previous:])
    wait(reopened, 30, "RSH reopen")
    if not observations:
        end = word("p3_end_count")
        vm.hold_serial_line(limit(10))
        rid, generation = op.submit("echo deferred", limit(20))
        try:
            op.wait_delivery(rid, "acked", seconds=10)
        except Failure as exc:
            pending = op.operator.pending
            sender = op.agent.notifier
            delivery = pending["execute"].delivery if pending else None
            raise Failure(f"{exc} (rid={rid} delivery={delivery} window={sender.window} "
                          f"silent={sender.silent})") from exc
        op.expect_pending(rid, "running", seconds=1)
        vm.uart.write(b"\r")
        result = op.expect_once(rid, generation, b"deferred\n", limit(20))
        check(result["state"] == "done" and result["drop"] == 0, "deferred output truncated")
        capture_end(end)
        vm.check_serial_rejection(vm.screen(vm.deadline), "echo deferred")
    old = word("g_shell_service", 8)
    serial = vm.directory / "boot-serial.log"
    offset = serial.stat().st_size
    op.raw(RSHELL, EXECUTE, b"00001000 p3-fault\n", vm.deadline)
    wait(lambda: word("shell_flow", 4*(12*4+3)) == old, 30, "resident fault not observed")
    previous_ready = op.operator.ready
    wait(lambda: b"/sys/shell.bin returned -" in serial.read_bytes()[offset:], 20, "resident exit log missing")
    fault = point(4, (0, 0, 0, 0, 0))
    check(word("p3_exit_owner") == resident[7] and word("p3_exit_reader") == resident[7],
          "faulting resident did not own reader")
    if fault[1] == resident[7]:
        observe("reader retained after owner exit")
    else:
        check(fault[1] == 0xffffffff and word("g_capture") == 0, "resident fault cleanup")
    ticks(300)
    if word("g_shell_service", 8) == old:
        observe("resident fault without new generation")
    else:
        wait(lambda: ready() and op.operator.ready is not previous_ready, 60, "missing restarted resident")
        restarted = point(5, (1, 0, 1, 1, 0))
        check(restarted[3] > old and restarted[9:] == resident[9:], "restarted identity")
        if not observations:
            command("echo afterfault", b"afterfault\n")
        generation = op.operator.generation
        op.restart()
        wait(lambda: ready() and op.operator.ready is not previous_ready, 30, "new agent READY")
        check(op.operator.generation != generation, "agent generation reused")
        if not observations:
            command("echo generation", b"generation\n")
    peak, failures = word("kmalloc_peak_bytes"), word("p3_alloc_failures")
    check(0 < peak < 1024*1024 and failures == 0, f"heap peak={peak} failures={failures}")
    if observations:
        check(len(observations) == 1, "multiple mutation signatures")
        raise Failure(observations[0])


def boot(cpu, case, media, version):
    iso, metadata = media
    directory = Path("evidence") / cpu / case
    directory.mkdir(parents=True)
    start = time.monotonic()
    observing = False
    try:
        with network() as (io, devices), (directory / "qemu.stderr").open("wb") as err:
            (directory / "boot.json").write_text(json.dumps(dict(metadata, cpu=cpu, case=case,
                sha=os.getenv("GITHUB_SHA") or os.getenv("OS32_BUILD_COMMIT"), run=os.getenv("GITHUB_RUN_ID"),
                attempt=os.getenv("GITHUB_RUN_ATTEMPT"), origin=start, qemu=version,
                argv=guest_args(iso, cpu, directory, devices, True))))
            with guest(iso, cpu, directory, devices=devices, deadline=start+180, stderr=err,
                       interactive=True) as vm:
                vm.network = io
                with peer(vm, directory) as op:
                    observing = True
                    exercise(vm, op, case == "late")
                vm.diagnose()
        return 0, ""
    except (Failure, TimeoutError, OSError, ValueError, RuntimeError) as exc:
        (directory / "failure.txt").write_text(str(exc))
        infrastructure = not observing or (isinstance(exc, OSError) and not isinstance(exc, TimeoutError))
        return (2 if infrastructure else 1), str(exc)


def verify():
    media = link_media()
    version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
    results = []
    with ThreadPoolExecutor(2) as pool:
        for case in ("early", "late"):
            jobs = [(cpu, pool.submit(boot, cpu, case, media, version)) for cpu in ("7450", "7455")]
            results.extend((f"{cpu}/{case}", *job.result()) for cpu, job in jobs)
    for code in (2, 1):
        independent = [f"{name}: {reason}" for name, status, reason in results
                       if status == code and reason not in FAULTS]
        if independent:
            print("; ".join(independent))
            return code
    reasons = {reason for _, _, reason in results}
    if reasons == {""}:
        return 0
    if len(reasons) == 1:
        print("; ".join(f"{name}: {reason}" for name, _, reason in results))
    else:
        print("; ".join(f"{name}: inconsistent observation (see failure.txt)" for name, _, _ in results))
    return 1


if __name__ == "__main__":
    try:
        if sys.argv[2] == "build":
            require(Path("tests/support/fixtures/ppc_link/flow.inc"))
        else:
            sys.exit(verify())
    except subprocess.CalledProcessError as exc:
        print(f"link-sh: {exc}")
        sys.exit(1)
    except (OSError, Failure) as exc:
        print(exc)
        sys.exit(2)
