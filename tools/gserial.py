#!/usr/bin/env python3
"""Control gshell over 9600bps serial or --port aidebug:http://127.0.0.1:8025.

Do not use NP21/W /api/cmd while this tool owns its serial transport.
Requests use SOH plus a fresh random id; only that id's end closes a reply.
All reads share one receive buffer, including the CUI entry EOT handoff.
"""
import argparse
import base64
import binascii
import re
import secrets
import sys
import time

from rshell_serial import open_port, read_size
from remote_transport import CommandTransport

DEFAULT_TIMEOUT = 15.0
MAX_LINE = 128
EOT = b"\x04"

# Copy of np21w-src/src/win9x/aidebug/aidebug_keys.cpp s_named and ASCII keys.
KEY_NAMES = {
    "ESC": 0x00, "MINUS": 0x0b, "CIRCUMFLEX": 0x0c, "YEN": 0x0d,
    "BACKSPACE": 0x0e, "BS": 0x0e, "TAB": 0x0f, "ATMARK": 0x1a,
    "AT": 0x1a, "LBRACKET": 0x1b, "RETURN": 0x1c, "ENTER": 0x1c,
    "CR": 0x1c, "SEMICOLON": 0x26, "COLON": 0x27, "RBRACKET": 0x28,
    "COMMA": 0x30, "DOT": 0x31, "PERIOD": 0x31, "SLASH": 0x32,
    "UNDERSCORE": 0x33, "SPACE": 0x34, "XFER": 0x35, "ROLLUP": 0x36,
    "PAGEUP": 0x36, "ROLLDOWN": 0x37, "PAGEDOWN": 0x37, "INS": 0x38,
    "INSERT": 0x38, "DEL": 0x39, "DELETE": 0x39, "UP": 0x3a,
    "LEFT": 0x3b, "RIGHT": 0x3c, "DOWN": 0x3d, "HOME": 0x3e,
    "HOMECLR": 0x3e, "HELP": 0x3f, "NUMMINUS": 0x40, "NUMSLASH": 0x41,
    "NUMSTAR": 0x45, "NUMPLUS": 0x49, "NUMEQUAL": 0x4d,
    "NUMCOMMA": 0x4f, "NUMDOT": 0x50, "NFER": 0x51, "STOP": 0x60,
    "COPY": 0x61, "SHIFT": 0x70, "CAPS": 0x71, "KANA": 0x72,
    "GRPH": 0x73, "GRAPH": 0x73, "CTRL": 0x74,
}
KEY_NAMES.update(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ", (
    0x1d, 0x2d, 0x2b, 0x1f, 0x12, 0x20, 0x21, 0x22, 0x17,
    0x23, 0x24, 0x25, 0x2f, 0x2e, 0x18, 0x19, 0x10, 0x13,
    0x1e, 0x14, 0x16, 0x2c, 0x11, 0x2a, 0x15, 0x29)))
KEY_NAMES.update(zip("1234567890", range(1, 11)))
KEY_NAMES.update(zip("-.,/;:@[]\\", (
    0x0b, 0x31, 0x30, 0x32, 0x26, 0x27, 0x1a, 0x1b, 0x28, 0x0d)))
for _digit, _scan in zip("7894561230", (
        0x42, 0x43, 0x44, 0x46, 0x47, 0x48, 0x4a, 0x4b, 0x4c, 0x4e)):
    KEY_NAMES["NUM" + _digit] = KEY_NAMES["KP" + _digit] = _scan
KEY_NAMES.update(("F%d" % n, 0x61 + n) for n in range(1, 11))
MODIFIERS = {"SHIFT": "s", "CTRL": "c", "GRPH": "g", "GRAPH": "g",
             "KANA": "k"}


def key_sequence(sequence):
    """Translate SHIFT+SPACE,A,RETURN to guest scan/modifier tokens."""
    result = []
    for chord in sequence.split(","):
        parts = [part.strip().upper() for part in chord.split("+")]
        name = parts[-1]
        if re.fullmatch(r"NKEY:[0-9A-F]{1,2}", name):
            scan = int(name[5:], 16)
            if scan > 0x7f:
                raise ValueError("scan must be below 80")
        elif name in KEY_NAMES:
            scan = KEY_NAMES[name]
        else:
            raise ValueError("unknown key: " + name)
        flags = []
        for modifier in parts[:-1]:
            if modifier not in MODIFIERS or MODIFIERS[modifier] in flags:
                raise ValueError("invalid modifier: " + modifier)
            flags.append(MODIFIERS[modifier])
        result.append("%02x" % scan + "".join("+" + flag for flag in flags))
    return " ".join(result)


def _buffer(port):
    if not hasattr(port, "_gserial_rx"):
        port._gserial_rx = bytearray()
    return port._gserial_rx


def _fill(port, deadline):
    if time.monotonic() >= deadline:
        raise TimeoutError("gserial receive timed out")
    _buffer(port).extend(port.read(read_size(port)))


def read_buffered(port, size, timeout=DEFAULT_TIMEOUT):
    """Read up to size bytes, preserving any surplus in the shared buffer."""
    if size <= 0:
        return b""
    buf = _buffer(port)
    deadline = time.monotonic() + timeout
    while not buf:
        _fill(port, deadline)
    data = bytes(buf[:size])
    del buf[:size]
    return data


def _read_line(port, deadline):
    buf = _buffer(port)
    while True:
        end = buf.find(b"\n")
        if end >= 0:
            line = bytes(buf[:end]).removesuffix(b"\r")
            del buf[:end + 1]
            return line.decode("ascii", errors="replace")
        _fill(port, deadline)


def read_eot_keep_rest(port, timeout=DEFAULT_TIMEOUT):
    """Return (preceding bytes, seen EOT), consuming exactly one EOT."""
    buf = _buffer(port)
    deadline = time.monotonic() + timeout
    while True:
        end = buf.find(EOT)
        if end >= 0:
            data = bytes(buf[:end])
            del buf[:end + 1]
            return data, True
        try:
            _fill(port, deadline)
        except TimeoutError:
            data = bytes(buf)
            buf.clear()
            return data, False


class Client:
    def __init__(self, port, timeout=DEFAULT_TIMEOUT, stdout=None, stderr=None,
                 lan=None, transport="serial", port_factory=None):
        self.port = port
        self.timeout = timeout
        self.stdout = sys.stdout if stdout is None else stdout
        self.stderr = sys.stderr if stderr is None else stderr
        self.notifications = []
        self.lan = lan
        self.lan_submitted = False
        self.transport = transport
        self.port_factory = port_factory

    def _notice_or_incidental(self, line):
        if line.startswith("gs:! "):
            self.notifications.append(line)
            print("[notice] " + line, file=self.stdout)
        else:
            print("[incidental] " + line, file=self.stderr)

    def request(self, command):
        if any(ord(ch) < 0x20 or ord(ch) > 0x7e for ch in command):
            raise ValueError("command must contain printable ASCII only")
        if len(command) + 9 > MAX_LINE:
            raise ValueError("request exceeds 128 bytes")
        if self.lan is not None:
            result = self.lan.request("gui", command, self.timeout)
            if result.get("state") == "serial" and self.transport == "auto" and not self.lan_submitted:
                self._fallback()
            else:
                self.lan_submitted = True
                return self._lan_reply(result)
        request_id = secrets.token_hex(4)
        body = request_id + " " + command
        if len(body) > MAX_LINE:
            raise ValueError("request exceeds 128 bytes")
        self.port.write(b"\x01" + body.encode("ascii") + b"\n")
        self.port.flush()
        prefix = "gs:" + request_id + " "
        deadline = time.monotonic() + self.timeout
        lines = []
        while True:
            line = _read_line(self.port, deadline)
            if line.startswith(prefix):
                print(line, file=self.stdout)
                payload = line[len(prefix):]
                terminal = re.fullmatch(r"end ([0-4])", payload)
                if terminal:
                    return int(terminal.group(1)), lines
                lines.append(payload)
            else:
                self._notice_or_incidental(line)

    def _fallback(self):
        self.lan.close()
        self.lan = None
        if self.port is None:
            if self.port_factory is None:
                raise ValueError("serial requires --port")
            self.port = self.port_factory()

    def _lan_reply(self, result):
        if not result.get("ok") or result.get("state") != "done" or not result.get("complete") or result.get("drop") != 0:
            raise ValueError("communication incomplete: " + str(result.get("state", result.get("error", "error"))))
        try:
            body = base64.b64decode(result["body_b64"], validate=True).decode("ascii")
        except (KeyError, ValueError, UnicodeError, binascii.Error) as exc:
            raise ValueError("invalid LAN response") from exc
        rows = body.splitlines()
        match = re.fullmatch(r"gs:([0-9a-f]{8}) (.*)", rows[0]) if rows else None
        if not match:
            raise ValueError("invalid LAN response id")
        request_id = match.group(1)
        prefix = "gs:" + request_id + " "
        lines = []
        for index, line in enumerate(rows):
            if not line.startswith(prefix):
                raise ValueError("LAN response id mismatch")
            payload = line[len(prefix):]
            terminal = re.fullmatch(r"end ([0-4])", payload)
            print(line, file=self.stdout)
            if terminal:
                if index != len(rows) - 1:
                    raise ValueError("data after LAN end")
                return int(terminal.group(1)), lines
            lines.append(payload)
        raise ValueError("missing LAN end")

    def _lan_ready(self, mode, deadline):
        while time.monotonic() < deadline:
            probe = self.lan.probe(max(0.001, deadline - time.monotonic()))
            ready = probe.get("ready")
            if probe.get("ok") and probe.get("hello") and ready and ready.get("mode") == mode:
                return
            time.sleep(0.05)
        raise TimeoutError(mode + " READY timed out")

    def _wait_notice(self, success, failure=None, seen=()):
        deadline = time.monotonic() + self.timeout
        pending = list(seen)
        while True:
            if pending:
                line = pending.pop(0)
            elif self.lan is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("notice timed out")
                result = self.lan.notice(int(remaining * 1000), remaining)
                if not result.get("ok"):
                    raise ValueError("notice failed")
                for notice in result.get("notices", []):
                    pending.extend(base64.b64decode(notice["body_b64"], validate=True).decode("ascii").splitlines())
                continue
            else:
                line = _read_line(self.port, deadline)
                self._notice_or_incidental(line)
            if self.lan is not None:
                self._notice_or_incidental(line)
            if failure and (line == failure or line.startswith(failure + " ")):
                return 1
            if line == success or line.startswith(success + " "):
                return 0

    def enter(self):
        if self.lan is not None:
            result = self.lan.request("cui", "os32gui", self.timeout)
            if result.get("state") == "serial" and self.transport == "auto" and not self.lan_submitted:
                self._fallback()
            else:
                self.lan_submitted = True
                if not result.get("ok") or result.get("state") != "done" or not result.get("complete") or result.get("drop") != 0:
                    raise ValueError("GUI handoff incomplete")
                body = base64.b64decode(result["body_b64"], validate=True)
                if not body.endswith(EOT) or not body.lstrip(b"\r\n").startswith(b"> os32gui\n"):
                    raise ValueError("invalid GUI handoff")
                rc = self._wait_notice("gs:! ready")
                self._lan_ready("gui", time.monotonic() + self.timeout)
                return rc
        self.port.write(b"os32gui\n")
        self.port.flush()
        return self._wait_notice("gs:! ready")

    def leave(self):
        start = len(self.notifications)
        rc, lines = self.request("cui")
        if rc or "cui queued" not in lines:
            return 1
        if self._wait_notice("gs:! cui done", "gs:! cui failed",
                             self.notifications[start:]):
            return 1
        if self.lan is not None:
            self._lan_ready("cui", time.monotonic() + self.timeout)
            return 0
        body, ok = read_eot_keep_rest(self.port, self.timeout)
        if body:
            print("[incidental] " + body.decode("ascii", errors="replace"),
                  file=self.stderr)
        if not ok:
            raise TimeoutError("rshell entry EOT timed out")
        return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--transport", choices=("auto", "lan", "serial"), default="auto")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("ping", "state", "px", "dump", "cui", "flush", "enter", "leave"):
        commands.add_parser(name)
    commands.add_parser("key").add_argument("sequence")
    commands.add_parser("type").add_argument("text")
    commands.add_parser("wd").add_argument("seconds", type=int)
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    command = args.command
    try:
        if command == "key":
            command += " " + key_sequence(args.sequence)
        elif command == "type":
            command += " " + args.text
        elif command == "wd":
            if args.seconds < 0:
                raise ValueError("wd seconds must be nonnegative")
            command += " " + str(args.seconds)
        def open_serial():
            if not args.port:
                raise ValueError("serial requires --port")
            return open_port(args.port, args.baud)
        lan = None if args.transport == "serial" else CommandTransport()
        client = Client(None, args.timeout, lan=lan, transport=args.transport,
                        port_factory=open_serial)
        try:
            if lan is None:
                client.port = open_serial()
            if command in ("enter", "leave"):
                return getattr(client, command)()
            rc, _ = client.request(command)
            return 0 if rc == 0 else 1
        finally:
            if client.port is not None:
                client.port.close()
            if client.lan is not None:
                client.lan.close()
    except (OSError, TimeoutError, ValueError, AttributeError) as exc:
        print("gserial: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
