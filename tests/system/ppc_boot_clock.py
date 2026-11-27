"""Observe the decrementer rate verdict from an isolated core image."""
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, require
from tests.support.ppc_fixture import clock_media
from tests.support.ppc_vm import guest
from tests.system.ppc_boot import READY

WORD = r"([0-9a-fA-F]{16})"
VALUES = re.compile("KBOOT dec_rate " + " ".join(
    key + "=" + WORD for key in ("got", "L", "U", "dmin", "dmax")) +
    r" ticks=[0-9]+ reason=(rate-low|same-sample)")
ENDS = re.compile("KBOOT dec_rate " + " ".join(
    key + "=" + WORD for key in ("a0", "b0", "a1", "b1", "width0", "width1")))


def verdict(output):
    lines = output.replace("\r", "").split("\n")[:-1]
    for i, line in enumerate(lines):
        if line.startswith("FATAL "):
            values = VALUES.fullmatch(lines[i-2]) if i >= 2 else None
            ends = ENDS.fullmatch(lines[i-1]) if i >= 2 else None
            if line != "FATAL kboot dec_rate" or not values or not ends:
                raise Failure("unexpected guest stop: " + line)
            got, low, high, dmin, dmax = (int(v, 16) for v in values.groups()[:-1])
            a, b, c, d, w0, w1 = (int(v, 16) for v in ends.groups())
            reason = values.groups()[-1]
            if (b-a, d-c) == (w0, w1):
                if reason == "same-sample" and got == 0 and (a, b) == (c, d):
                    return reason
                if (reason == "rate-low" and a <= b <= c <= d and
                        (dmin, dmax) == (c-b, d-a) and high >= low and low-got > low//50+2):
                    return reason
            raise Failure("inconsistent decrementer diagnostics")
        if line == READY:
            return "ready"


def boot(cpu, iso, directory, metadata):
    directory.mkdir(parents=True)
    start = time.monotonic()
    with guest(iso, cpu, directory, devices=["-nic", "none"], deadline=start + 40) as vm:
        (directory / "argv.json").write_text(json.dumps(vm.process.args))
        vm.wait(lambda output: None if verdict(output) else "clock verdict")
    result = verdict((directory / "boot-serial.log").read_text(errors="replace"))
    (directory / "boot.json").write_text(json.dumps(dict(metadata, cpu=cpu, result=result,
            boot_seconds=time.monotonic()-start)))
    return result


def main():
    if sys.argv[2] == "build":
        for source in ("sys/arch/ppc/kernel_dec.c", "sys/platform/powermac/ppc_kernel.c"):
            require(Path(source))
        return 0
    start = time.monotonic()
    iso, metadata = clock_media()
    metadata["build_seconds"] = time.monotonic()-start
    result = boot("7455", iso, Path("evidence/7455/core"), metadata)
    print("7455/" + result)
    return int(result != "ready")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as exc:
        print(exc)
        sys.exit(1)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(exc)
        sys.exit(2)
