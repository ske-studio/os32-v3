"""A mac99 guest with a bounded serial wait and a QMP monitor."""
from contextlib import contextmanager
import json
from pathlib import Path
import os
import select
import subprocess
import time


def remaining(deadline):
    seconds = deadline - time.monotonic()
    if seconds <= 0:
        raise TimeoutError("QEMU observation deadline expired")
    return seconds


class Guest:
    def __init__(self, process, serial, directory, deadline):
        self.process = process
        self.serial = serial
        self.directory = directory
        self.deadline = deadline
        self.buffer = b""
        self.greeting = None
        self.replies = {}
        self.request_id = 0
        self.sequence = 0
        self.qmp_closed = False

    def record(self, direction, message):
        self.sequence += 1
        with (self.directory / "qmp.log").open("a") as log:
            print(self.sequence, time.monotonic(), direction, json.dumps(message), file=log)

    def pump(self, timeout):
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if not ready:
            return
        chunk = os.read(self.process.stdout.fileno(), 8192)
        if not chunk:
            raise OSError("QMP closed")
        self.consume(chunk)

    def drain(self):
        """Record whatever QMP left in the pipe after QEMU has exited."""
        try:
            while chunk := os.read(self.process.stdout.fileno(), 8192):
                self.consume(chunk)
        except (OSError, ValueError) as exc:
            self.record("unavailable", f"drain: {exc}")

    def consume(self, chunk):
        lines = (self.buffer + chunk).split(b"\n")
        self.buffer = lines.pop()
        for line in lines:
            try:
                message = json.loads(line)
            except ValueError as exc:
                raise OSError("invalid QMP response") from exc
            self.record("recv", message)
            if "id" in message:
                self.replies[message["id"]] = message
            elif "QMP" in message:
                self.greeting = message

    def command(self, name, deadline, **arguments):
        self.request_id += 1
        request = dict(execute=name, arguments=arguments, id=self.request_id)
        self.record("send", request)
        remaining(deadline)
        self.process.stdin.write(json.dumps(request).encode() + b"\n")
        self.process.stdin.flush()
        while self.request_id not in self.replies:
            self.pump(remaining(deadline))
        reply = self.replies.pop(self.request_id)
        if "return" in reply:
            return reply["return"]
        raise OSError(f"{name}: {reply}")

    def wait(self, missing_marker):
        output = b""
        while True:
            remaining(self.deadline)
            output += self.serial.read()
            missing = missing_marker(output.decode(errors="replace"))
            if missing is None:
                return
            seconds = min(0.1, remaining(self.deadline))
            if self.process.poll() is None:
                if self.qmp_closed:
                    time.sleep(seconds)  # QMP ended but QEMU still runs: keep watching serial
                    continue
                try:
                    self.pump(seconds)
                except TimeoutError:
                    raise
                except OSError:
                    self.qmp_closed = True
                continue
            # QEMU ended: the guest may have written the marker just before.
            output += self.serial.read()
            missing = missing_marker(output.decode(errors="replace"))
            if missing is None:
                return
            raise OSError(f"QEMU exited {self.process.poll()} waiting for {missing!r}")

    def diagnose(self):
        for name, arguments in (("query-status", {}), ("screendump", {
                "filename": str((self.directory / "screen.ppm").resolve())})):
            try:
                self.command(name, time.monotonic() + 1, **arguments)
            except OSError as exc:
                self.record("unavailable", f"{name}: {exc}")


@contextmanager
def guest(iso, cpu, directory, *, devices, deadline):
    directory.mkdir(parents=True, exist_ok=True)
    serial = directory / "boot-serial.log"
    serial.touch()
    args = ["qemu-system-ppc", "-M", "mac99", "-cpu", cpu, "-m", "128M",
            "-accel", "tcg", "-cdrom", str(iso), "-boot", "d", "-prom-env",
            r"boot-device=cd:,\PPCKERN.ELF", "-g", "800x600x8", "-display", "none",
            "-serial", f"file:{serial}", "-serial", f"file:{directory}/serial1.log",
            "-trace", f"enable=escc_mem_writeb_data,file={directory}/uart.trace",
            *devices, "-monitor", "none", "-qmp", "stdio", "-no-reboot"]
    with serial.open("rb") as reader, subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
        vm = Guest(process, reader, directory, deadline)
        try:
            while vm.greeting is None:
                vm.pump(remaining(deadline))
            vm.command("qmp_capabilities", deadline)
            yield vm
        except Exception:
            vm.diagnose()
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            vm.drain()
