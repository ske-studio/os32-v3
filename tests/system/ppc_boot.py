"""Boot the candidate ISO on a mac99 and observe the guest serial console."""
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, PPC, PPC_OUT, main, ppc_build, require

READY = "KBOOT READY stage=pre-user"
HEAP = "LINK HEAP used="
TRAP = "FATAL trap vector=0x300 reason=0x2 addr=0x80000"


def build(config):
    ppc_build(config)
    iso = PPC_OUT / PPC[config][1] / "boot.iso"
    require(iso)
    serial = Path("boot-serial.log")
    args = ["qemu-system-ppc", "-M", "mac99", "-cpu", "7450", "-m", "128M",
            "-accel", "tcg", "-cdrom", str(iso), "-boot", "d", "-prom-env",
            r"boot-device=cd:,\PPCKERN.ELF", "-g", "800x600x8", "-display", "none",
            "-serial", f"file:{serial}", "-serial", "null", "-nic", "none",
            "-device", "sungem,mac=52:54:00:12:34:56", "-monitor", "none", "-no-reboot"]
    serial.write_text("")
    deadline = time.monotonic() + 40
    with subprocess.Popen(args) as guest:
        try:
            while True:
                output = serial.read_text(errors="replace")
                missing = missing_marker(config, output)
                if missing is None:
                    break
                if guest.poll() is not None:
                    raise OSError(f"QEMU exited early ({guest.returncode})")
                if time.monotonic() >= deadline:
                    print(output)
                    raise TimeoutError(f"QEMU timed out waiting for {missing!r}")
                time.sleep(0.1)
        finally:
            guest.terminate()


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
    output = Path("boot-serial.log").read_text(errors="replace")
    print(output)
    missing = missing_marker(config, output)
    if missing is not None:
        raise Failure(f"boot did not reach {missing}")


if __name__ == "__main__":
    main(build, verify)
