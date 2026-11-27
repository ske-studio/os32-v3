"""Boot the candidate ISO on a mac99 and observe the guest serial console."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, PPC, PPC_OUT, ppc_build, require

from tests.support.ppc_vm import guest

CPUS = ("7450", "7455")
READY = "KBOOT READY stage=pre-user"
HEAP = "LINK HEAP used="
TRAP = "FATAL trap vector=0x300 reason=0x2 addr=0x80000"


def boot(cpu, config, iso, metadata):
    started = time.monotonic()
    directory = Path("evidence") / cpu / config
    devices = ["-nic", "none", "-device", "sungem,mac=52:54:00:12:34:56"]
    try:
        directory.mkdir(parents=True)
        (directory / "boot.json").write_text(json.dumps(
            dict(metadata, cpu=cpu, case=config, origin=started)))
        with guest(iso, cpu, directory, devices=devices, deadline=started + 40) as vm:
            (directory / "argv.json").write_text(json.dumps(vm.process.args))
            vm.wait(lambda output: missing_marker(config, output))
        Path(cpu).mkdir(exist_ok=True)
        shutil.copyfile(directory / "boot-serial.log", Path(cpu) / "boot-serial.log")
        result = None
    except (OSError, Failure) as exc:
        result = type(exc)(f"{cpu}/{config}: {exc}")
    return result, time.monotonic() - started


def build(config):
    ppc_build(config)
    iso = PPC_OUT / PPC[config][1] / "boot.iso"
    require(iso)
    try:
        version = subprocess.check_output(["qemu-system-ppc", "--version"], text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise OSError(f"{config} ERROR: qemu-system-ppc --version: {exc}") from exc
    metadata = dict(sha=os.getenv("GITHUB_SHA"), run=os.getenv("GITHUB_RUN_ID"),
                    attempt=os.getenv("GITHUB_RUN_ATTEMPT"),
                    digest=hashlib.sha256(iso.read_bytes()).hexdigest(),
                    qemu=version)
    errors = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = {cpu: pool.submit(boot, cpu, config, iso, metadata) for cpu in CPUS}
        for cpu, job in jobs.items():
            error, seconds = job.result()
            status = "PASS" if error is None else "ERROR" if isinstance(error, OSError) and not isinstance(error, TimeoutError) else "FAIL"
            print(f"{cpu}/{config} {status} {seconds:.3f}s")
            if error:
                print(error)
                serial = Path("evidence") / cpu / config / "boot-serial.log"
                try:
                    with serial.open("rb") as stream:
                        stream.seek(max(0, os.fstat(stream.fileno()).st_size - 4096))
                        print(f"{cpu}/{config} serial tail:\n{stream.read(4096).decode(errors='replace')}")
                except OSError as exc:
                    print(f"{cpu}/{config} serial unavailable: {exc}")
                errors.append(error)
    if errors:
        error_type = OSError if any(isinstance(e, OSError) and not isinstance(e, TimeoutError) for e in errors) else Failure
        raise error_type("; ".join(map(str, errors)))


def missing_marker(config, output):
    if config == "neg1" and READY in output:
        raise Failure("negative boot reached READY")
    markers = {"core": [READY], "link": [READY, "LAN STATE state=5 link=1", HEAP], "neg1": [TRAP]}
    remaining = output
    for marker in markers.get(config, ["Loading /sys/shell.bin", "\x04", HEAP]):
        if marker not in remaining:
            return marker
        remaining = remaining.split(marker, 1)[1]
    return None


def verify(config):
    errors = []
    for cpu in CPUS:
        try:
            path = Path(cpu) / "boot-serial.log"
            missing = missing_marker(config, path.read_text(errors="replace"))
            if missing is not None:
                raise Failure(f"boot did not reach {missing}")
        except FileNotFoundError:
            errors.append(f"{cpu}/{config}: missing {cpu} boot serial")
        except Failure as exc:
            errors.append(f"{cpu}/{config}: {exc}")
    if errors:
        raise Failure("; ".join(errors))


if __name__ == "__main__":
    config, phase = sys.argv[1:]
    try:
        {"build": build, "verify": verify}[phase](config)
    except (Failure, subprocess.CalledProcessError) as exc:
        print(exc)
        sys.exit(1)
    except OSError as exc:
        print(exc)
        sys.exit(2)
