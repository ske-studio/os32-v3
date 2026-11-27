"""Observe USB shell input across a selectively suppressed SFS CALL."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import RPC_FIELDS, link_media
from tests.system.ppc_link_session import boot, check

SHORT = "suppressed CALL configured for 50 ticks"


def distance(before, after):
    return (after - before) & 0xffffffff


def ordered(before, after):
    return 0 < distance(before, after) < 0x80000000


def check_close(g):
    check(g["seen"] == 7, "missing OPEN, WDATA or RELEASE")
    check(g["reason"] == g["source"] and g["reason"] in (1, 2), "first termination cause")
    check(g["prev_event"] and g["end_event"] and ordered(g["prev_event"], g["end_event"]) and
          ordered(g["prev"], g["deadline"]) and distance(g["deadline"], g["end"]) < 0x80000000,
          "missing first foreground deadline crossing")
    check(g["closes"] == 1 and g["close_event"] and g["close_round"] == g["end_round"] and
          ordered(g["end_event"], g["close_event"]), "close outside first observing round")
    check(g["timeout_event"] and g["return_event"] and ordered(g["close_event"], g["return_event"]),
          "missing client timeout or return")
    if g["reason"] == 2:
        check(ordered(g["end_event"], g["timeout_event"]) and ordered(g["timeout_event"], g["close_event"])
              and g["timeout_round"] == g["close_round"], "client timeout/close order")
    else:
        check(ordered(g["close_event"], g["timeout_event"]) and ordered(g["timeout_event"], g["return_event"]),
              "poll close/notification order")


def exercise(vm, op, late):
    deadline = vm.deadline
    serial_log = vm.directory / "boot-serial.log"

    def serial():
        return serial_log.read_bytes()

    def wait(predicate, label, seconds=30):
        op.wait(predicate, min(deadline, time.monotonic() + seconds), label)

    def screen_line(text):
        return any(row.strip(" \x00") == text for row in vm.screen(deadline).splitlines())

    def key(code, modifiers=()):
        offset = len(serial())
        codes = (*modifiers, code)
        events = [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": c}}}
                  for down, group in ((True, codes), (False, reversed(codes))) for c in group]
        vm.command("input-send-event", deadline, events=events)
        wait(lambda: serial()[offset:].count(b" down=0") >= len(codes), "USB key release " + code)

    def type_line(line):
        for char in line:
            key("spc" if char == " " else char)

    def close_shell():
        wait(lambda: vm.word("rshell_active", 0, deadline) == 0
             and "[Remote shell closed]" in vm.screen(deadline), "local shell")

    def open_shell():
        start = len(op.events)
        type_line("rshell")
        key("ret")
        wait(lambda: op.operator.ready and op.operator.ready["mode"] == "cui" and
             any(e["parts"][:2] == ["RSH", "READY"] for e in op.events[start:]), "RSH READY")

    def command(line, payload):
        rid, generation = op.submit(line, deadline)
        result = op.expect_once(rid, generation, payload, deadline)
        check(result["state"] == "done" and result["drop"] == 0, "incomplete " + line)

    op.enabled = False
    (op.root / "f").write_bytes(b"usb-rpc\n")
    wait(lambda: b"protocol=02" in serial(), "mouse enumeration", 60)
    check(b"protocol=01" not in serial(), "keyboard already enumerated")
    vm.command("device_add", deadline, driver="usb-kbd", id="kbd")
    wait(lambda: b"protocol=01" in serial(), "keyboard enumeration")
    key("esc")
    op.enabled = True
    close_shell()
    type_line("echo first")
    key("c", ("ctrl",))
    check(not screen_line("first"), "Ctrl-C executed first")
    key("ret")
    wait(lambda: screen_line("first"), "USB first")
    open_shell()
    command("cat /host/f", b"usb-rpc\n")
    gate = {}
    op.next_call = gate
    rid, generation = op.submit("cat /host/f", deadline)
    wait(lambda: "wdata" in gate, "suppressed SFS CALL WDATA")
    key("shift")
    wait(lambda: "release" in gate, "guest RELEASE of suppressed CALL")
    result = op.expect_once(rid, generation, b"cat: /host/f: Host is down\n", deadline)
    offset = (gate["rid"] % 16) * len(RPC_FIELDS) * 4
    guest = {name: vm.word("rpc_trace", offset + 4*i, deadline) for i, name in enumerate(RPC_FIELDS)}
    vm.record("rpc ticks", dict(host=gate, guest=guest))
    check(guest["rid"] == gate["rid"] and guest["seen"] & 1 and guest["generation"],
          "missing tagged CALL deadline")
    width = distance(guest["start"], guest["deadline"])
    check(width in (50, 100), f"unexpected CALL width {width}")

    def check_host():
        check(result["state"] == "done" and result["drop"] == 0, "incomplete cat failure")
        ticks = distance(gate["wdata"], gate["release"])
        check(80 <= ticks <= 200, f"SFS RELEASE after {ticks} ticks")

    def recovery():
        key("esc")
        close_shell()
        start = vm.word("tick_count", 0, deadline)
        offset = len(serial())
        type_line("echo hi")
        key("ret")
        wait(lambda: screen_line("hi"), "USB hi")
        check(vm.word("tick_count", 0, deadline) != start and b"USB KEY src=1" in serial()[offset:],
              "missing tick or USB progress")
        check(b"usb: HC dead" not in serial(), "USB HC dead")
        open_shell()
        command("cat /host/f", b"usb-rpc\n")
        offset = len(serial())
        vm.command("device_del", deadline, id="kbd")
        wait(lambda: b"USB RELEASE kind=0 src=1" in serial()[offset:], "keyboard unplug")
        command("echo unplugged", b"unplugged\n")
        offset = len(serial())
        vm.command("device_add", deadline, driver="usb-kbd", id="kbd2")
        wait(lambda: b"protocol=01" in serial()[offset:], "keyboard re-enumeration")
        key("esc")
        close_shell()
        type_line("echo alive")
        key("ret")
        wait(lambda: screen_line("alive"), "USB alive")
        open_shell()
        command("echo alive", b"alive\n")

    for observation in (lambda: check_close(guest), check_host, recovery):
        try:
            observation()
        except (Failure, TimeoutError, RuntimeError, OSError, ValueError) as exc:
            if width != 50:
                raise
            vm.record("short CALL diagnostics", str(exc))
    if width == 50:
        raise Failure(SHORT)


def verify():
    media = link_media(usb_trace=True, rpc_trace=True)
    version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
    with ThreadPoolExecutor(2) as pool:
        jobs = [pool.submit(boot, cpu, "usb-lan", media, version, exercise, 180, "usb-mouse,id=mouse")
                for cpu in ("7450", "7455")]
        results = [job.result() for job in jobs]
    if all(code == 0 for code, _ in results):
        return 0
    if all(result == (1, SHORT) for result in results):
        print(SHORT)
        return 1
    for cpu, (code, reason) in zip(("7450", "7455"), results):
        print(cpu, code, "short RPC window" if reason == SHORT else reason)
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
