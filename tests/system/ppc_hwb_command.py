"""Exercise HW run interruption and the following POINT in the resident shell."""
from contextlib import nullcontext
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import HWB, hwb_media
from tests.support.ppc_vm import guest, guest_args
from tools.host_rshell import PPC_DIAG

PROMPT = r"(?:OS32:[^\n]*\$|sh>)"


def until(vm, deadline, reason, probe):
    while time.monotonic() < deadline:
        if vm.peer:
            vm.peer.pump()
        if value := probe():
            latest = vm.directory / "screen.txt"
            if latest.exists():
                (vm.directory / "last-good.txt").write_bytes(latest.read_bytes())
            return value
        vm.pump(min(0.1, max(0, deadline - time.monotonic())))
    raise Failure(reason)


def screen_text(vm, deadline):
    screen = "\n".join(row.rstrip(" \0") for row in vm.screen(deadline).splitlines())
    (vm.directory / "screen.txt").write_text(screen)
    return screen


def ohci(vm, deadline):
    until(vm, deadline, "shell prompt", lambda: re.search(PROMPT, screen_text(vm, deadline)))
    vm.keys("hw run /hw/ohci.hwb\n", deadline)
    screen = until(vm, deadline, "OHCI completion",
                   lambda: (s if "op=end rc=OK" in (s := screen_text(vm, deadline)) else None))
    serial = (vm.directory / "boot-serial.log").read_text(errors="replace")
    values = [int(v, 16) for v in re.findall(r'tag="CFG_VALUE" val=(0x[\da-fA-F]+)', screen)]
    offsets = [int(v, 16) for v in re.findall(r'tag="CFG_OFFSET" val=(0x[\da-fA-F]+)', screen)]
    revision = re.search(r'tag="HC_REVISION" val=(0x[\da-fA-F]+)', screen)
    if offsets != [0, 8, 16] or len(values) != 3 or not revision:
        raise Failure("incomplete OHCI reads")
    if values[0] != 0x003f106b or values[1] >> 8 != 0x0c0310 or not values[2] & ~15:
        raise Failure("OHCI PCI values")
    mmio = re.search(r'op=mmioread32[^\n]*?addr=(0x[\da-fA-F]+)', screen)
    for bus, dev, fn, addr in re.findall(r'USB OHCI BAR0 bdf=(\w+):(\w+)\.(\d+) pa=(\w+)', serial):
        bdf = f"{int(bus,16)}:{int(dev,16)}.{fn}"
        rev = re.search(rf'ohci {re.escape(bdf)} ports=\d+ rev=([\da-f]+)', serial)
        if (mmio and int(mmio[1], 16) == int(addr, 16) and rev and
                int(revision[1], 16) & 255 == int(rev[1], 16) and
                set(re.findall(r'op=pciread32[^\n]*?bdf=(\d+:\d+\.\d+)', screen)) == {bdf}):
            return
    raise Failure("OHCI controller or revision mismatch")


def record_run(vm, ident, op, **fields):
    with (vm.directory / "runs.jsonl").open("a") as log:
        print(json.dumps(dict(at=time.monotonic(), run=ident, op=op, **fields)), file=log)


def interrupt(vm, peer, via, op, runs, deadline):
    def word(symbol, offset=0):
        return vm.word(symbol, offset, deadline)

    before = len(runs) * 2
    if word("hw_points") != before:
        raise Failure("unexpected POINT before run")
    vm.keys(f"hw run /hw/{op}.hwb\n", deadline)

    def active_run():
        if word("hwb_active"):
            ids = re.findall(rf"HW run=(\d+) seq=[^\n]* op={op}\b", screen_text(vm, deadline))
            return next((ident for ident in reversed(ids) if ident not in runs), None)

    ident = until(vm, deadline, f"{op} active", active_run)
    runs.add(ident)
    record_run(vm, ident, op, via=via, active=1, points=before)
    if op == "poll":
        reads = word("hw_pci_reads")
        until(vm, deadline, "poll PCI read", lambda: word("hw_pci_reads") != reads)
    vm.key("e", (), deadline)
    if via == "usb":
        vm.key("pause", ("ctrl",), deadline)
    else:
        peer.notify_number(PPC_DIAG, 3, b"\x03", deadline)
    end = f"HW run={ident} op=end rc=ABORT"
    until(vm, deadline, f"{op} ABORT and prompt", lambda: not word("hwb_active") and
          re.search(re.escape(end) + r"[\s\S]*" + PROMPT, screen_text(vm, deadline)))
    record_run(vm, ident, op, active=0, result="ABORT")
    label = f"retained-{via}-{op}"
    bound = min(deadline, time.monotonic() + 20)
    vm.keys(f"cho {label}\n", bound)
    until(vm, bound, label, lambda: label in screen_text(vm, bound).splitlines())
    vm.keys(f"source /hw/{via}{op}.sh\n", deadline)
    until(vm, deadline, "reload status and POINT", lambda: word("hw_points") == before + 2 and
          not word("hwb_active") and f"loaded-{via}-{op} 0" in screen_text(vm, deadline).splitlines())
    points = [word("hw_points", 4*i) for i in (before+1, before+2)]
    record_run(vm, ident, op, points=points)
    if points == [1, 1]:
        raise Failure("reload POINT retained abort")
    if points != [1, 0]:
        raise Failure("invalid interrupt POINT")


def observe(media, devices, connect=None, network_io=None):
    iso, metadata = media
    directory = Path("evidence/7450/hwb").resolve()
    directory.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    deadline = started + 300
    (directory / "boot.json").write_text(json.dumps(dict(metadata, cpu="7450", case="hwb",
        sha=os.getenv("GITHUB_SHA") or os.getenv("OS32_BUILD_COMMIT"), run=os.getenv("GITHUB_RUN_ID"),
        attempt=os.getenv("GITHUB_RUN_ATTEMPT"), origin=started,
        qemu=subprocess.check_output(["qemu-system-ppc", "--version"], text=True),
        argv=guest_args(iso, "7450", directory, devices))))
    with (directory / "qemu-stderr.log").open("wb") as stderr, guest(
            iso, "7450", directory, devices=devices, deadline=deadline, stderr=stderr) as vm:
        vm.network = network_io
        with (connect(vm, ROOT) if connect else nullcontext()) as peer:
            vm.peer = peer
            if peer:
                until(vm, deadline, "LAN operator ready",
                      lambda: peer.snapshot(deadline)["mode"] == "cui")
            until(vm, deadline, "remote shell startup", lambda:
                  "initial read=" in (directory / "boot-serial.log").read_text(errors="replace") and
                  "Waiting for commands via serial" in screen_text(vm, deadline))
            vm.key("esc", (), deadline)
            ohci(vm, deadline)
            runs = set()
            for via in (("lan", "usb") if connect else ("usb",)):
                for op in ("wait", "poll"):
                    interrupt(vm, peer, via, op, runs, deadline)


if __name__ == "__main__":
    try:
        if sys.argv[2] == "build":
            require(HWB / "points.inc")
        else:
            from tests.support.ppc_peer import network, peer
            media = hwb_media()
            with network() as (io, devices):
                observe(media, devices, peer, io)
    except (Failure, TimeoutError) as exc:
        print(f"7450/hwb {exc}")
        sys.exit(1)
    except (ImportError, OSError, subprocess.CalledProcessError) as exc:
        print(f"7450/hwb {exc}")
        sys.exit(2)
