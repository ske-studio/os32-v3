"""Observe foreground cancellation and renew a host session without a BYE."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "tools")]
from host_sfs_link import SfsLink
import serialfs_host as sfs
from tests.support.media import Failure, require
from tests.support.ppc_fixture import hfs_cancel_media
from tests.support.ppc_vm import guest


def host_renewal(requests):
    nonces = [sfs.try_frame(frame, 0)[2].payload[4:8] for frame in requests]
    if len(nonces) != 2 or nonces[0] == nonces[1]:
        raise Failure("expected two distinct guest nonces")
    with tempfile.TemporaryDirectory() as root:
        Path(root, "probe").touch()
        peer = bytes.fromhex("020000000001")
        host = SfsLink(root, peer=peer)
        sessions = []
        for request in requests:
            reply = host.call(request, peer)
            if not reply:
                raise Failure("host ignored HELLO")
            sessions.append(sfs.try_frame(reply, 0)[2].sid)
        old, new = sessions
        if not old or not new or old == new or old not in host.server.closed:
            raise Failure("host retained old session")

        def stat(sid):
            return host.call(sfs.encode(sfs.T_STAT, sid, 1, b"\x06/probe"), peer)

        if stat(old):
            raise Failure("old session received a reply")
        reply = stat(new)
        frame = sfs.try_frame(reply, 0)
        if (frame[0] != "frame" or frame[2].type != sfs.T_STAT | sfs.T_RESP
                or frame[2].sid != new or frame[2].payload[:4] != bytes(4)):
            raise Failure("new session STAT failed")


def verify():
    iso = hfs_cancel_media()
    for cpu in ("7450", "7455"):
        directory = Path("evidence") / cpu / "cancel"
        with guest(iso, cpu, directory, devices=["-nic", "none"],
                   deadline=time.monotonic() + 90) as vm:
            vm.wait(lambda output: None if re.search(r"^HFS result=.+\r?$", output, re.M)
                    else "HFS result")
        output = (directory / "boot-serial.log").read_text()
        result = re.search(r"^HFS result=(.+)$", output, re.M)[1]
        if result != "pass":
            raise Failure(result)
        host_renewal([bytes.fromhex(n) for n in re.findall(r"^HFS hello=([0-9a-f]+)$", output, re.M)])
        print(cpu, "cancel and host renewal passed")


if __name__ == "__main__":
    try:
        if sys.argv[2] == "build":
            require(Path("tests/support/fixtures/ppc_link/hfs_cancel.inc"))
        else:
            verify()
    except Failure as exc:
        print(exc)
        sys.exit(1)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(exc)
        sys.exit(2)
