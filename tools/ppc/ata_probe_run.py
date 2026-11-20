#!/usr/bin/env python3
"""Run the standalone ATA probe on mac99 with a disposable IDE fixture."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

CPUS = ("7450", "7455")
DISK_BYTES = 1024 * 1024
SECTOR_BYTES = 512
PREFIX = bytes(range(64))
SIGNATURE = bytes.fromhex("55aa")
RESET = b"ATA probe: reset-all\r\n"
DONE = rb"(?m)^ATA probe done channels=0x[0-9a-f]{4}\r?$"


def drive(path, index, media):
    escaped = str(path).replace(",", ",,")
    return f"file={escaped},format=raw,if=ide,index={index},media={media}"


def check_serial(data):
    expected = b"lba0=" + PREFIX.hex().encode() + b" tail=55aa sig55aa=yes"
    # Keep the model and sector dump together on device 0 of one channel.
    data = data.replace(b"\r\n", b"\n")
    for record in re.split(rb"(?m)^(?=ATA node=|device=|ATA probe done)", data):
        lines = record.splitlines()
        if lines and lines[0] == b"device=0" and all(
            line in lines for line in (b"select status=0x50 [DRDY DSC]", b"presence=ata",
                                       b"model=QEMU HARDDISK", expected)
        ):
            return
    raise RuntimeError("missing QEMU HARDDISK and expected LBA 0 on device 0")


def run_cpu(work, iso, cpu, bios, timeout):
    disk = work / "disk.raw"
    with disk.open("xb") as image:
        image.write(PREFIX)
        image.seek(SECTOR_BYTES - len(SIGNATURE))
        image.write(SIGNATURE)
        image.truncate(DISK_BYTES)

    serial = work / "serial.log"
    stderr = work / "qemu.stderr"
    argv = [
        "qemu-system-ppc", "-M", "mac99", "-cpu", cpu, "-m", "128M", "-smp", "1",
        "-accel", "tcg", "-bios", str(bios),
        "-drive", drive(disk, 0, "disk") + ",snapshot=on",
        "-drive", drive(iso, 2, "cdrom"), "-boot", "d",
        "-prom-env", r"boot-device=cd:,\BOOT.ELF", "-prom-env", "auto-boot?=true",
        "-display", "none", "-serial", f"file:{serial}", "-serial", "null",
        "-monitor", "none", "-no-reboot",
    ]
    try:
        with stderr.open("wb") as err, subprocess.Popen(
            argv, stdin=subprocess.DEVNULL, stdout=err, stderr=err
        ) as guest:
            deadline = time.monotonic() + timeout
            checked = False
            try:
                while time.monotonic() < deadline:
                    if serial.exists():
                        data = serial.read_bytes()
                        done = re.search(DONE, data)
                        if done and not checked:
                            check_serial(data)
                            checked = True
                    if guest.poll() is not None:
                        data = serial.read_bytes() if serial.exists() else b""
                        # -no-reboot turns the probe's OF reset-all into a QEMU exit.
                        if checked and RESET in data and guest.returncode == 0:
                            return
                        raise RuntimeError(f"QEMU exited before completion and reset ({guest.returncode})")
                    time.sleep(0.1)
                raise RuntimeError(f"no completion and reset within {timeout}s (checked={checked})")
            finally:
                if guest.poll() is None:
                    guest.terminate()
                    try:
                        guest.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        guest.kill()
                        guest.wait()
    finally:
        disk.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path,
                        default=Path("build/out/ppc-kboot/ata-probe/BOOT.ELF"))
    parser.add_argument("--out", type=Path, required=True,
                        help="parent directory for a new run and its serial logs")
    parser.add_argument("--bios", type=Path, default=Path("/usr/share/qemu/openbios-ppc"))
    parser.add_argument("--timeout", type=int, default=180, metavar="SECONDS")
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("--timeout must be positive")

    args.out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="mac99-ata-", dir=args.out.resolve()))
    print(f"Logs: {work}", flush=True)
    cd = work / "cd"
    cd.mkdir()
    shutil.copyfile(args.elf, cd / "BOOT.ELF")
    iso = work / "boot.iso"
    subprocess.run(
        ["genisoimage", "-quiet", "-hfs", "-r", "-part", "-no-desktop",
         "-o", str(iso), str(cd)], check=True
    )

    failed = False
    for cpu in CPUS:
        case = work / cpu
        case.mkdir()
        try:
            run_cpu(case, iso, cpu, args.bios, args.timeout)
        except (OSError, RuntimeError) as error:
            print(f"FAIL {cpu}: {error} ({case})", flush=True)
            failed = True
        else:
            print(f"PASS {cpu} QEMU HARDDISK LBA 0 ({case / 'serial.log'})", flush=True)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
