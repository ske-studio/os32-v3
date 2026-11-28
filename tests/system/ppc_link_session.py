"""PowerMac resident remote shell, with early and late host agents."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
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
          "resident fault without new generation", "SFS end missing after exit 7"}


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
        capture_end(end)
        if data["duplicate"]:
            raise Failure("duplicate PRINT payload")  # only after capture END and one-OUT held
        check(data["state"] == "done" and data["drop"] == 0, f"truncated {rid}")
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
        return ready() and word("rshell_active") and op.ready_released(previous)
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

    # The three resident-shell mutations have already returned their own verdict.
    command("cd /bin", b"")
    # The CWD spelling (e.g. a trailing slash) belongs to the VFS; the scope only has to restore it.
    cwd = command("pwd")
    check(cwd.rstrip(b"\n").rstrip(b"/") == b"/bin", "cd /bin did not take effect")
    def hello_scope():
        lines = command("sfs run /bin/hello.bin scope").splitlines()
        check(sum(line.startswith(b"sfs: transport=lan ") for line in lines) == 1,
              "SFS scope did not begin")
        check(lines.count(b"hello scope") == 1, "SFS child start count")
        command("echo $?", b"7\n")
        check(b"sfs: exit=7" in lines, "SFS end missing after exit 7")
        check(lines.count(b"sfs: exit=7") == 1, "duplicate SFS end")
        command("pwd", cwd)

    hello_scope()
    # A scope that moves the CWD must hand the original one back (sfs_restore_cwd in rshell.c).
    moved = command("sfs run cd /").splitlines()
    check(b"sfs: exit=0" in moved, "SFS cd scope completion")
    command("pwd", cwd)
    fault_lines = command("sfs run /bin/fault.bin").splitlines()
    check(b"sfs: exit=139" in fault_lines, "SFS fault scope completion")
    hello_scope()
    command("umount /host", b"")
    # cmd_umount queries the VFS mount table before attempting removal.
    check(b"not mounted" in command("umount /host"), "/host still mounted")
    refused = command("sfs run /bin/hello.bin scope")
    check(b"requires --sfs-lan" in refused, "SFS LAN entry did not refuse without /host")
    check(b"hello scope" not in refused, "SFS child started without /host")

    if not late:
        offset = serial.stat().st_size
        previous = word("g_shell_service", 8)
        op.submit("s12-switch", limit(10))
        wait(lambda: serial.read_bytes()[offset:].count(b"/bin/second.bin returned 12") >= 2,
             20, "second resident did not exit twice")
        check(point(6, (0, 0, 0, 0, 0))[3] == previous, "switch retirement generation")
        check(word("switch_service_inactive") == 1, "old shell service was not retired")
        for index in (7, 8):
            entry = point(index, (1, 0, 1, 1, 0))
            check(entry[3] > previous and entry[9:] == resident[9:], "second resident identity")
            previous = entry[3]
        check(vm.screen(vm.deadline).count("S12 second resident") >= 2, "second startup markers")


def boot(cpu, case, media, version, scenario=exercise, seconds=180, usb_device="usb-kbd,id=kbd"):
    iso, metadata = media
    directory = Path("evidence") / cpu / case
    directory.mkdir(parents=True)
    start = time.monotonic()
    observing = False
    try:
        with network(usb_device) as (io, devices), (directory / "qemu.stderr").open("wb") as err:
            (directory / "boot.json").write_text(json.dumps(dict(metadata, cpu=cpu, case=case,
                sha=os.getenv("GITHUB_SHA") or os.getenv("OS32_BUILD_COMMIT"), run=os.getenv("GITHUB_RUN_ID"),
                attempt=os.getenv("GITHUB_RUN_ATTEMPT"), origin=start, qemu=version,
                argv=guest_args(iso, cpu, directory, devices, True))))
            with guest(iso, cpu, directory, devices=devices, deadline=start+seconds, stderr=err,
                       interactive=True) as vm:
                vm.network = io
                with peer(vm, directory) as op:
                    observing = True
                    scenario(vm, op, case == "late")
                vm.diagnose()
        return 0, ""
    except (Failure, TimeoutError, OSError, ValueError, RuntimeError) as exc:
        (directory / "failure.txt").write_text(str(exc))
        infrastructure = not observing or (isinstance(exc, OSError) and not isinstance(exc, TimeoutError))
        return (2 if infrastructure else 1), str(exc)


def verify():
    media = link_media(switch=True)
    directory = media[0].parent
    files = (directory / "user/shell.elf", directory / "second/shell.bin", directory / "root-input.iso", media[0])
    print("SFS media sha256 " + json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                            for p in files}), flush=True)
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
