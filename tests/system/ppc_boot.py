"""Boot the candidate ISO on a mac99 and observe the guest serial console."""
from pathlib import Path
import subprocess
import sys

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
    with subprocess.Popen(args) as guest:
        try:
            guest.wait(timeout=20)
        except subprocess.TimeoutExpired:
            guest.terminate()
            guest.wait()
        else:
            raise OSError(f"QEMU exited early ({guest.returncode})")


def verify(config):
    output = Path("boot-serial.log").read_text(errors="replace")
    print(output)
    markers = {"core": [READY], "link": [READY, "LAN STATE state=5 link=1", HEAP], "neg1": [TRAP]}
    remaining = output
    for marker in markers.get(config, ["Loading /sys/shell.bin", "\x04", HEAP]):
        if marker not in remaining:
            raise Failure(f"boot did not reach {marker}")
        remaining = remaining.split(marker, 1)[1]
    if config == "neg1" and READY in output:
        raise Failure("negative boot reached READY")


if __name__ == "__main__":
    main(build, verify)
