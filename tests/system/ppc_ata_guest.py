"""Exercise the mac99 ATA driver and compare guest observations to disposable raw media."""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import FIXTURE, diagnostic_media
from tests.support.ppc_vm import guest

CPUS = ("7450", "7455")
CASES = ("normal", "absent", "write", "deny")
ROOT = 16448
READ_FAULT = "read 2047 returned sector 2046"
WRITE_FAULT = "correct payload used two write commands"


def sector(lba):
    return bytes((19*i + (i >> 3) + 7*lba + (lba >> 8)) & 255 for i in range(512))


def digest(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return f"{value:08x}"


def changed_sectors(initial, actual):
    """Inclusive LBA ranges, including an unexpected extension or truncation."""
    ranges = []
    for pos in range(0, max(len(initial), len(actual)), 512):
        if initial[pos:pos + 512] != actual[pos:pos + 512]:
            sector_number = pos // 512
            if ranges and ranges[-1][1] + 1 == sector_number:
                ranges[-1][1] = sector_number
            else:
                ranges.append([sector_number, sector_number])
    return ranges


def observe(case, directory, initial):
    lines = (directory / "boot-serial.log").read_text(errors="replace").splitlines()

    def expect(line):
        if "ATA " + line not in lines:
            raise Failure(f"missing {line}")

    def alternate(normal, other, diagnosis):
        if "ATA " + other in lines and "ATA " + normal not in lines:
            return diagnosis
        expect(normal)
        if "ATA " + other in lines:
            raise Failure("conflicting guest observations")

    raw = b""
    if case != "absent":
        raw = (directory / "disk.raw").read_bytes()
        (directory / "changes.json").write_text(json.dumps(changed_sectors(initial, raw)))
    failures = [line for line in lines if line.startswith("ATA failed=")]
    if failures:
        raise Failure(failures[0])
    expect("done")
    if case == "absent":
        expect("present=0 registered=0")
        return
    expected = bytearray(initial)
    fault = None
    if case == "normal":
        for line in ("present=1 registered=0", "pa=80020000", "wait reads=2 rc=0",
                     "model=QEMU HARDDISK sectors=2048"):
            expect(line)
        for lba in (0, 1024):
            expect(f"lba={lba} rc=0 hash={digest(sector(lba))}")
        right, wrong = (digest(sector(i)) for i in (2047, 2046))
        if right == wrong:
            raise OSError("read fixture hash collision")
        fault = alternate("lba=2047 rc=0 hash=" + right, "lba=2047 rc=0 hash=" + wrong, READ_FAULT)
    else:
        expect("shell=0")
        if case == "deny":
            expect("unreserved=-17,-17 child=73 provider_calls=0")
        else:
            metadata = raw[:2048] + raw[ROOT*512:]
            expect(f"metadata={zlib.crc32(metadata):08x}")
            expected[:2048] = raw[:2048]
            expected[ROOT*512:] = raw[ROOT*512:]
            payload = bytes((i*37 + (i >> 8) + 13) & 255 for i in range(1024))
            expected[96*512:97*512] = payload[:512]
            expected[64*512:66*512] = payload
            for line in ("writes=0,0 range=-17", "end=0", "read=64 equal=1",
                         "read=65 equal=1", "read=96 equal=1"):
                expect(line)
            fault = alternate("commands=1", "commands=2", WRITE_FAULT)
    if raw != expected or len(raw) != len(initial):
        first = next((i for i, (a, b) in enumerate(zip(raw, expected)) if a != b),
                     min(len(raw), len(expected)))
        raise Failure(f"raw mismatch at byte {first}, disk length {len(raw)}")
    return fault


def boot(cpu, case, media, disk, version):
    iso, metadata = media
    directory = Path("evidence") / cpu / case
    directory.mkdir(parents=True)
    devices = ["-nic", "none"]
    if case != "absent":
        raw = directory / "disk.raw"
        raw.write_bytes(disk)
        devices.extend(["-drive", f"file={raw},if=ide,index=0,format=raw,cache=writethrough"])
    started = time.monotonic()
    try:
        with guest(iso, cpu, directory, devices=devices, deadline=started + 90) as vm:
            (directory / "boot.json").write_text(json.dumps(dict(
                metadata, sha=os.getenv("GITHUB_SHA") or os.getenv("OS32_BUILD_COMMIT"), run=os.getenv("GITHUB_RUN_ID"),
                attempt=os.getenv("GITHUB_RUN_ATTEMPT"), cpu=cpu, case=case,
                origin=started, qemu=version, argv=vm.process.args)))
            vm.wait(lambda output: None if "ATA done" in output.splitlines() else "ATA done")
            vm.diagnose()
        fault = observe(case, directory, disk)
        return 0, fault
    except (OSError, Failure) as exc:
        reason = f"{cpu}/{case} {exc}"
        (directory / "failure.txt").write_text(reason)
        return (1 if isinstance(exc, (TimeoutError, Failure)) else 2), reason


def verify():
    version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
    media = {name: diagnostic_media(name) for name in ("read", "write", "deny")}
    disk = b"".join(sector(i) for i in range(2048))
    blank = bytes((ROOT + 4000)*512)
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = {(cpu, case): pool.submit(boot, cpu, case,
                media["read" if case in ("normal", "absent") else case],
                blank if case == "write" else disk, version)
                for case in CASES for cpu in CPUS}
        results = {key: job.result() for key, job in jobs.items()}
    for status in (2, 1):
        for code, reason in results.values():
            if code == status:
                print(reason)
                return status
    faults = {key: reason for key, (_, reason) in results.items() if reason}
    if faults:
        for fault, case in ((READ_FAULT, "normal"), (WRITE_FAULT, "write")):
            if faults == {(cpu, case): fault for cpu in CPUS}:
                print(f"{CPUS[0]}/{case} {fault}")
                return 1
        cpu, case = next(iter(faults))
        print(f"{cpu}/{case} inconsistent ATA observations across CPUs or stimuli")
        return 1
    print("ATA guest passed all eight boots")
    return 0


if __name__ == "__main__":
    try:
        if sys.argv[2] == "build":
            for name in ("read.inc", "write.inc"):
                require(FIXTURE / name)
        else:
            sys.exit(verify())
    except (OSError, subprocess.CalledProcessError, Failure) as exc:
        print(exc)
        sys.exit(2)
