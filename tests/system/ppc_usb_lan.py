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


def client_crossing(g):
    if not (g["prev_client_event"] and g["first_client_event"] and
            ordered(g["prev_client_event"], g["first_client_event"]) and
            ordered(g["prev_client_tick"], g["deadline"]) and
            distance(g["deadline"], g["first_client_tick"]) < 0x80000000):
        return False
    if g["source"] == 2:
        return (g["first_client_event"], g["first_client_tick"]) == (g["end_event"], g["end"])
    return (g["source"] == 1 and ordered(g["end_event"], g["first_client_event"]) and
            distance(g["end"], g["first_client_tick"]) < 0x80000000)


def deadline_cause(g):
    deadline = g["deadline"]
    missing = "incomplete deadline measurement"
    if not (g["seen"] == 7 and g["source"] in (1, 2) and
            all(g[k] for k in ("prev_event", "end_event", "end_round", "close_event", "close_round",
                               "prior_poll", "poll_round")) and
            client_crossing(g) and ordered(g["prior_tick"], deadline) and
            g["prior_wait"] in (0, 1) and g["can_wait"] in (0, 1) and
            ordered(g["prev_event"], g["end_event"]) and ordered(g["prev"], deadline) and
            distance(deadline, g["end"]) < 0x80000000 and
            ordered(g["end_event"], g["close_event"])):
        return missing
    late = g["late_poll"]
    if late:
        if not (ordered(g["prior_poll"], late) and
                distance(deadline, g["late_tick"]) < 0x80000000 and
                distance(late, g["poll_round"]) < 0x80000000 and
                distance(g["late_tick"], g["poll_tick"]) < 0x80000000 and
                g["late_wait"] in (0, 1) and
                (late != g["poll_round"] or (g["late_tick"], g["late_wait"]) ==
                 (g["poll_tick"], g["can_wait"]))):
            return missing
    elif (g["prior_poll"], g["prior_tick"], g["prior_wait"]) != (
            g["poll_round"], g["poll_tick"], g["can_wait"]):
        return missing
    if ordered(g["end_round"], g["close_round"]):
        return "close delayed after valid deadline observation"
    if g["close_round"] != g["end_round"] or not ordered(deadline, g["end"]):
        return missing
    if late and ordered(late, g["end_round"]):
        if not g["late_wait"]:
            return "foreground poll reached but unable to wait"
        return missing
    if ordered(g["prior_poll"], g["end_round"]):
        return "first foreground deadline crossing after deadline tick"
    return missing


def check_close(g):
    check(g["seen"] == 7, "missing OPEN, WDATA or RELEASE")
    # The link idle expiry (WDATA + 100) can only coincide with the deadline (start + 100);
    # the upper layer must still close exactly once in the first observing round.
    check(g["source"] in (1, 2) and (g["reason"] == g["source"] or
          (g["reason"] == 3 and g["reason_tick"] == g["deadline"])), "first termination cause")
    check(g["prev_event"] and g["end_event"] and ordered(g["prev_event"], g["end_event"]) and
          ordered(g["prev"], g["deadline"]) and distance(g["deadline"], g["end"]) < 0x80000000,
          "missing first foreground deadline crossing")
    check(client_crossing(g), "missing client deadline crossing")
    check(g["closes"] == 1 and g["close_event"] and g["close_round"] == g["end_round"] and
          ordered(g["end_event"], g["close_event"]), "close outside first observing round")
    check(g["timeout_event"] and g["return_event"] and ordered(g["close_event"], g["return_event"]),
          "missing client timeout or return")
    if g["source"] == 2:
        check(ordered(g["end_event"], g["timeout_event"]) and ordered(g["timeout_event"], g["close_event"])
              and g["timeout_round"] == g["close_round"], "client timeout/close order")
    else:
        check(ordered(g["close_event"], g["timeout_event"]) and ordered(g["timeout_event"], g["return_event"]),
              "poll close/notification order")


def check_timing(guest, gate, vm):
    if ordered(guest["deadline"], guest["end"]) or ordered(guest["end_round"], guest["close_round"]):
        vm.record("guest deadline", deadline_cause(guest))
    check_close(guest)
    ticks = distance(gate["wdata"], gate["release"])
    check(80 <= ticks <= 200, f"SFS RELEASE after {ticks} ticks")


def exercise(vm, op, late, confirm_short):
    deadline = vm.deadline
    serial_log = vm.directory / "boot-serial.log"

    def serial():
        return serial_log.read_bytes()

    def wait(predicate, label, seconds=30):
        op.wait(predicate, min(deadline, time.monotonic() + seconds), label)

    def screen_line(text):
        return any(row.strip(" \x00") == text for row in vm.screen(deadline).splitlines())

    def send_key(code, modifiers=()):
        offset = len(serial())
        codes = (*modifiers, code)
        events = [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": c}}}
                  for down, group in ((True, codes), (False, reversed(codes))) for c in group]
        vm.command("input-send-event", deadline, events=events)
        return lambda: wait(lambda: serial()[offset:].count(b" down=0") >= len(codes), "USB key release " + code)

    def key(code, modifiers=()):
        send_key(code, modifiers)()

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
    shift_released = send_key("shift")
    wait(lambda: "release" in gate, "guest RELEASE of suppressed CALL")
    wait(lambda: op.outputs(rid, generation), "suppressed cat completion")
    offset = (gate["rid"] % 16) * len(RPC_FIELDS) * 4

    def read(name):
        return vm.word("rpc_trace", offset + 4 * RPC_FIELDS.index(name), deadline)

    tag = {name: read(name) for name in ("rid", "seen", "generation", "start", "deadline")}
    check(tag["rid"] == gate["rid"] and tag["seen"] & 1 and tag["generation"],
          "missing tagged CALL deadline")
    width = distance(tag["start"], tag["deadline"])
    check(width in (50, 100), f"unexpected CALL width {width}")

    if width == 50:
        confirm_short()

    def check_guest():
        guest = {name: tag[name] if name in tag else read(name) for name in RPC_FIELDS}
        vm.record("rpc ticks", dict(host=gate, guest=guest))
        check_timing(guest, gate, vm)

    def check_host():
        result = op.expect_once(rid, generation, b"cat: /host/f: Host is down\n", deadline)
        check(result["state"] == "done" and result["drop"] == 0, "incomplete cat failure")

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

    check_observations(width, vm, (check_guest, shift_released, check_host, recovery))


def check_observations(width, vm, observations):
    for observation in observations:
        try:
            observation()
        except Exception as exc:  # width 50 is already settled; later diagnostics must not hide it
            if width != 50:
                raise
            vm.record("short CALL diagnostics", str(exc))
    if width == 50:
        raise Failure(SHORT)


def boot_cpu(cpu, media, version):
    short = False

    def confirm_short():
        nonlocal short
        short = True

    def scenario(vm, op, late):
        exercise(vm, op, late, confirm_short)

    try:
        result = boot(cpu, "usb-lan", media, version, scenario, 180, "usb-mouse,id=mouse")
    except Exception:
        if not short:
            raise
    return (1, SHORT) if short else result


def verify():
    media = link_media(usb_trace=True, rpc_trace=True)
    version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
    with ThreadPoolExecutor(2) as pool:
        jobs = [pool.submit(boot_cpu, cpu, media, version)
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
