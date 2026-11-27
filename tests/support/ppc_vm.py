"""A mac99 guest with a bounded serial wait and a QMP monitor."""
from contextlib import ExitStack, contextmanager
import json
from pathlib import Path
import os
import re
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
        self.screen_generation = 0

    def record(self, direction, message):
        self.sequence += 1
        with (self.directory / "qmp.log").open("a") as log:
            print(self.sequence, time.monotonic(), direction, json.dumps(message), file=log)

    def pump(self, timeout):
        inputs = [self.process.stdout]
        if getattr(self, "uart_out", None):
            inputs.append(self.uart_out)
        ready, _, _ = select.select(inputs, [], [], timeout)
        if getattr(self, "uart_out", None) in ready:
            self.uart_out.read(65536)
        if self.process.stdout not in ready:
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

    def word(self, symbol, offset, deadline):
        if not hasattr(self, "symbols"):
            nm = Path(os.getenv("PPC_CROSS_DIR", "/home/hight/opt/cross-ppc")) / "bin/powerpc-elf-nm"
            output = subprocess.check_output([nm, "-n", self.elf], text=True,
                                             timeout=remaining(deadline))
            self.symbols = {fields[2]: int(fields[0], 16) for line in output.splitlines()
                            if len(fields := line.split()) == 3}
        if symbol not in self.symbols:
            raise OSError(f"missing diagnostic symbol {symbol}")
        address = self.symbols[symbol] + offset
        reply = self.command("human-monitor-command", deadline,
                             **{"command-line": f"xp /1wx 0x{address:x}"})
        value = re.search(r":\s*0x([0-9a-fA-F]+)", reply)
        if not value:
            raise OSError(f"cannot read {symbol} at {address:x}: {reply}")
        return int(value[1], 16)

    def key(self, code, modifiers, deadline):
        keys = [{"type": "qcode", "data": name} for name in (*modifiers, code)]
        self.command("send-key", deadline, keys=keys, **{"hold-time": 80})
        time.sleep(min(0.14, remaining(deadline)))

    def keys(self, text, deadline):
        names = {" ": "spc", "\n": "ret", "-": "minus", ".": "dot", "/": "slash"}
        for char in text:
            self.key(names.get(char, char), (), deadline)

    def screen(self, deadline):
        self.command("stop", deadline)
        try:
            cells, cols, rows = (self.word("terminal", n, deadline) for n in (8, 16, 20))
            if not (0 < cols <= 200 and 0 < rows <= 100):
                raise ValueError(f"invalid terminal geometry {cols} x {rows}")
            self.screen_generation += 1
            self.record("screen", {"generation": self.screen_generation})
            dump = (self.directory / "screen.cells").resolve()
            self.command("pmemsave", deadline, val=cells, size=4*cols*rows, filename=str(dump))
            self.command("screendump", deadline, filename=str(dump.with_suffix(".ppm")))
            data = dump.read_bytes()
            if len(data) != 4*cols*rows:
                raise OSError("incomplete terminal cell dump")
            chars = "".join(chr(int.from_bytes(data[i:i+2], "big")) for i in range(0, len(data), 4))
            return "\n".join(chars[i:i+cols] for i in range(0, len(chars), cols))
        finally:
            self.command("cont", deadline)

    def hold_serial_line(self, deadline):
        with (self.directory / "uart.trace").open("rb") as trace:
            trace.seek(0, 2)
            self.uart.write(b"\x1bx")
            data = b""
            while True:
                data += trace.read()
                chars = re.findall(rb"escc_mem_readb_data Read channel [Aa], ch (\d+)\n", data)
                if chars == [b"27", b"120"]:
                    return
                self.pump(min(.02, remaining(deadline)))

    def check_serial_rejection(self, screen, command):
        lines = screen.splitlines()
        if not any("rshell: line with ESC not executed" in line and
                   any("> " + command in later for later in lines[i+1:])
                   for i, line in enumerate(lines)):
            raise ValueError("missing serial rejection before " + command)

    def diagnose(self):
        for name, arguments in (("query-status", {}), ("screendump", {
                "filename": str((self.directory / "screen.ppm").resolve())})):
            try:
                self.command(name, time.monotonic() + 1, **arguments)
            except OSError as exc:
                self.record("unavailable", f"{name}: {exc}")


def guest_args(iso, cpu, directory, devices, interactive=False):
    serial = directory / "boot-serial.log"
    uart = ["-serial", f"file:{serial}"]
    event = "escc_mem_writeb_data"
    if interactive:
        uart = ["-chardev", f"pipe,id=uart,path={directory}/uart,logfile={serial}",
                "-serial", "chardev:uart"]
        event = "escc_mem_readb_data"
    return ["qemu-system-ppc", "-M", "mac99", "-cpu", cpu, "-m", "128M",
            "-accel", "tcg", "-cdrom", str(iso), "-boot", "d", "-prom-env",
            r"boot-device=cd:,\PPCKERN.ELF", "-g", "800x600x8", "-display", "none",
            *uart, "-serial", f"file:{directory}/serial1.log",
            "-trace", f"enable={event},file={directory}/uart.trace",
            *devices, "-monitor", "none", "-qmp", "stdio", "-no-reboot"]


@contextmanager
def guest(iso, cpu, directory, *, devices, deadline, stderr=None, interactive=False):
    directory.mkdir(parents=True, exist_ok=True)
    serial = directory / "boot-serial.log"
    serial.touch()
    args = guest_args(iso, cpu, directory, devices, interactive)
    with ExitStack() as stack:
        if interactive:
            ends = []
            for suffix in ("in", "out"):
                fifo = directory / f"uart.{suffix}"
                os.mkfifo(fifo, mode=0o600)
                stack.callback(fifo.unlink)
                fd = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
                ends.append(stack.enter_context(os.fdopen(fd, "r+b", buffering=0)))
        reader = stack.enter_context(serial.open("rb"))
        process = stack.enter_context(subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr))
        vm = Guest(process, reader, directory, deadline)
        if interactive:
            vm.uart, vm.uart_out = ends
        try:
            while vm.greeting is None:
                vm.pump(remaining(deadline))
            vm.command("qmp_capabilities", deadline)
            vm.elf = Path(iso).resolve().with_name("PPCKERN.ELF")
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
