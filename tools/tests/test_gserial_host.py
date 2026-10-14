#!/usr/bin/env python3
"""Fake-port tests of the real gserial tool, including chunked CUI handoff.

No emulator or pyserial is needed. --mutate runs the normal control first,
then checks that each transport/protocol defect is RED in a copied module.
"""
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SRC = ROOT / "tools/gserial.py"
MOD = None


def load(path=SRC):
    spec = importlib.util.spec_from_file_location("gserial_under_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        self.now += 0.01
        if self.now > 10.0:
            raise RuntimeError("receive loop failed to time out")
        return self.now


class FakePort:
    def __init__(self, chunks=(), reply=None):
        self.chunks = list(chunks)
        self.writes = []
        self.reply = reply
        self.closed = False
        self.flushes = 0
        self.reads = 0

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, size):
        self.reads += 1
        # Keep a removed timeout guard bounded even if it no longer reads the
        # fake clock. Normal timeout tests need only a few dozen empty reads.
        if self.reads > 1000:
            raise RuntimeError("receive loop exceeded fake-port read budget")
        if not self.chunks:
            return b""
        # Respect the requested size; in_waiting exposes the whole next chunk.
        data = self.chunks[0][:size]
        self.chunks[0] = self.chunks[0][size:]
        if not self.chunks[0]:
            self.chunks.pop(0)
        return data

    def write(self, data):
        self.writes.append(data)
        if self.reply:
            self.chunks.extend(self.reply(data))
        return len(data)

    def flush(self):
        self.flushes += 1

    def close(self):
        self.closed = True


class GserialTests(unittest.TestCase):
    def setUp(self):
        self.mod = MOD
        self.mod.time = FakeClock()
        self.out = io.StringIO()
        self.err = io.StringIO()

    def client(self, port):
        return self.mod.Client(port, timeout=0.3, stdout=self.out, stderr=self.err)

    def test_matching_end_ignores_eot_and_other_ids(self):
        port = FakePort([b"console\x04data\r\ngs:deadbeef end 0\n",
                         b"gs:! cui done\ngs:1234abcd state q=0\r\n",
                         b"gs:1234abcd end 4 extra\ngs:1234abcd en",
                         b"d 0\ngs:! next\n"])
        with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd") as token:
            rc, lines = self.client(port).request("state")
        self.assertEqual((rc, lines), (0, ["state q=0", "end 4 extra"]))
        self.assertEqual(port.writes, [b"\x011234abcd state\n"])
        token.assert_called_once_with(4)
        self.assertIn("[notice] gs:! cui done", self.out.getvalue())
        self.assertIn("[incidental] gs:deadbeef end 0", self.err.getvalue())
        self.assertIn("console\x04data", self.err.getvalue())
        self.assertNotIn("[notice]", self.err.getvalue())
        self.assertEqual(self.mod.read_buffered(port, 100, 0.3), b"gs:! next\n")

    def test_px_cli_receives_sample_and_matching_end(self):
        payload = b"px ff,ff pb=4294967295 cm=4294967295"
        port = FakePort([b"gs:1234abcd " + payload + b"\r\ngs:1234abcd en", b"d 0\r\n"])
        with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd"):
            with patch.object(self.mod, "open_port", return_value=port):
                with patch.object(self.mod.sys, "stdout", self.out):
                    self.assertEqual(self.mod.main(["--port", "COM3", "px"]), 0)
        self.assertEqual(port.writes, [b"\x011234abcd px\n"])
        self.assertIn(payload.decode(), self.out.getvalue())
        self.assertIn("end 0", self.out.getvalue())
        self.assertTrue(port.closed)
        self.assertEqual(port.chunks, [])

    def test_no_matching_end_times_out(self):
        for response in (b"", b"gs:deadbeef end 0\n", b"gs:1234abcd pong\n"):
            with self.subTest(response=response):
                port = FakePort([response])
                with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd"):
                    with self.assertRaises(TimeoutError):
                        self.client(port).request("ping")

    def test_eot_consumes_one_and_keeps_chunk_tail(self):
        port = FakePort([b"\x04NEXT"])
        self.assertEqual(self.mod.read_eot_keep_rest(port, 0.3), (b"", True))
        self.assertEqual(self.mod.read_buffered(port, 100, 0.3), b"NEXT")
        self.assertEqual(port.reads, 1)
        port = FakePort([b"before\x04NEXT\x04TAIL"])
        self.assertEqual(self.mod.read_eot_keep_rest(port, 0.3), (b"before", True))
        self.assertEqual(port.reads, 1)
        self.assertEqual(self.mod.read_buffered(port, 4, 0.3), b"NEXT")
        self.assertEqual(self.mod.read_eot_keep_rest(port, 0.3), (b"", True))
        self.assertEqual(self.mod.read_buffered(port, 100, 0.3), b"TAIL")
        self.assertEqual(port.reads, 1)

    def test_eot_timeout_reports_failure(self):
        port = FakePort([b"partial"])
        self.assertEqual(self.mod.read_eot_keep_rest(port, 0.3), (b"partial", False))

    def test_leave_all_stages_share_buffer(self):
        def reply(data):
            self.assertEqual(data, b"\x011234abcd cui\n")
            return [b"gs:1234abcd cui queued\ngs:1234abcd end 0\n"
                    b"gs:! cui done\nRemote shell\x04NEXT"]
        port = FakePort(reply=reply)
        with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd"):
            self.assertEqual(self.client(port).leave(), 0)
        self.assertEqual(self.mod.read_buffered(port, 100, 0.3), b"NEXT")
        self.assertEqual(port.reads, 1)
        self.assertIn("[notice] gs:! cui done", self.out.getvalue())

    def test_leave_failure_and_missing_stages(self):
        for tail, expected in ((b"gs:! cui failed cfg\n", 1),
                               (b"", TimeoutError),
                               (b"gs:! cui done\n", TimeoutError),
                               (b"\x04", TimeoutError)):
            with self.subTest(tail=tail):
                port = FakePort([b"gs:1234abcd cui queued\ngs:1234abcd end 0\n" + tail])
                with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd"):
                    if expected is TimeoutError:
                        with self.assertRaises(TimeoutError):
                            self.client(port).leave()
                    else:
                        self.assertEqual(self.client(port).leave(), expected)

    def test_leave_checks_queued_and_command_failure(self):
        for response in (b"gs:1234abcd err busy\ngs:1234abcd end 4\n",
                         b"gs:1234abcd end 0\n"):
            port = FakePort([response + b"gs:! cui done\n\x04"])
            with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd"):
                self.assertEqual(self.client(port).leave(), 1)

    def test_leave_notice_during_request(self):
        port = FakePort([b"gs:1234abcd cui queued\ngs:! cui done\n"
                         b"gs:1234abcd end 0\n\x04NEXT"])
        with patch.object(self.mod.secrets, "token_hex", return_value="1234abcd"):
            self.assertEqual(self.client(port).leave(), 0)
        self.assertEqual(self.mod.read_buffered(port, 100, 0.3), b"NEXT")

    def test_enter_waits_for_ready(self):
        port = FakePort([b"\x04\n> os32gui\ngs:! wd expired\ngs:! ready v1\nTAIL"])
        self.assertEqual(self.client(port).enter(), 0)
        self.assertEqual(port.writes, [b"os32gui\n"])
        self.assertEqual(self.mod.read_buffered(port, 100, 0.3), b"TAIL")
        self.assertIn("[notice] gs:! ready v1", self.out.getvalue())

    def test_ids_are_fresh_token_hex_four(self):
        def reply(data):
            request_id = data[1:9]
            self.assertRegex(request_id, rb"^[0-9a-f]{8}$")
            return [b"gs:" + request_id + b" end 0\n"]
        port = FakePort(reply=reply)
        client = self.client(port)
        # Deterministic distinct tokens verify that every request calls secrets.
        with patch.object(self.mod.secrets, "token_hex", side_effect=["11111111", "22222222"]) as token:
            self.assertEqual(client.request("ping")[0], 0)
            self.assertEqual(client.request("state")[0], 0)
        self.assertEqual(token.call_count, 2)
        self.assertEqual(token.call_args_list[0].args, (4,))
        self.assertNotEqual(port.writes[0][1:9], port.writes[1][1:9])

    def test_keys_and_validation(self):
        self.assertEqual(self.mod.key_sequence("SHIFT+SPACE,A,RETURN"), "34+s 1d 1c")
        self.assertEqual(self.mod.key_sequence("CTRL+ESC,GRPH+KANA+KP8,NKEY:7f"),
                         "00+c 43+g+k 7f")
        self.assertEqual(self.mod.key_sequence("f10,at,homeclr,0"), "6b 1a 3e 0a")
        for sequence in ("", "UNKNOWN", "ALT+A", "SHIFT+SHIFT+A", "NKEY:80"):
            with self.assertRaises(ValueError):
                self.mod.key_sequence(sequence)
        port = FakePort()
        for command in ("type bad\nline", "type \x01", "type 日本", "type " + "a" * 120):
            with self.assertRaises(ValueError):
                self.client(port).request(command)
        self.assertEqual(port.writes, [])

    def test_cli_transport_commands_close_and_status(self):
        cases = [("key", ["SHIFT+SPACE,A,RETURN"], "key 34+s 1d 1c"),
                 ("type", ["a @ b"], "type a @ b"), ("wd", ["20"], "wd 20")]
        for command, args, wire in cases:
            port = FakePort(reply=lambda data: [b"gs:" + data[1:9] + b" end 0\n"])
            with patch.object(self.mod, "open_port", return_value=port) as opener:
                with patch.object(self.mod.sys, "stdout", self.out):
                    self.assertEqual(self.mod.main(["--port", "aidebug:http://fake", command] + args), 0)
            opener.assert_called_once_with("aidebug:http://fake", 9600)
            self.assertTrue(port.closed)
            self.assertEqual(port.writes[0][10:-1].decode(), wire)
        port = FakePort(reply=lambda data: [b"gs:" + data[1:9] + b" end 2\n"])
        with patch.object(self.mod, "open_port", return_value=port):
            with patch.object(self.mod.sys, "stdout", self.out):
                self.assertEqual(self.mod.main(["--port", "COM3", "ping"]), 1)
        self.assertTrue(port.closed)
        self.assertEqual(self.mod.DEFAULT_TIMEOUT, 15.0)


MUTATIONS = [
    ('prefix = "gs:" + request_id + " "', 'prefix = "gs:"', "accept other ids"),
    ('re.fullmatch(r"end ([0-4])", payload)', 're.search(r"end ([0-4])", payload)', "accept nonterminal suffix"),
    ('del buf[:end + 1]\n            return data, True', 'buf.clear()\n            return data, True', "discard EOT tail"),
    ('del buf[:end + 1]\n            return line.decode', 'buf.clear()\n            return line.decode', "discard line tail"),
    ('request_id = secrets.token_hex(4)', 'request_id = "1234abcd"', "reuse constant id"),
    ('if time.monotonic() >= deadline:', 'if False:', "remove timeout"),
    ('if rc or "cui queued" not in lines:', 'if False:', "ignore queued and rc"),
    ('"gs:! cui failed",', '"gs:! bogus failed",', "ignore CUI failure notice"),
    ('if not ok:', 'if False:', "ignore missing entry EOT"),
    ('self.notifications[start:]', '()', "lose notice during request"),
]


def run(module, quiet=False):
    global MOD
    MOD = module
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GserialTests)
    result = unittest.TextTestRunner(stream=io.StringIO() if quiet else sys.stdout,
                                    verbosity=1).run(suite)
    return result.wasSuccessful()


def mutate():
    original = SRC.read_text()
    good = True
    with tempfile.TemporaryDirectory(prefix="os32-gserial-") as directory:
        for index, (old, new, name) in enumerate(MUTATIONS, 1):
            if original.count(old) != 1:
                print("MUTATION %d NOT APPLICABLE: %s" % (index, name))
                good = False
                continue
            path = Path(directory) / ("mut%d.py" % index)
            path.write_text(original.replace(old, new, 1))
            module = load(path)
            red = not run(module, quiet=True)
            print("MUTATION %d %s: %s" % (index, "RED" if red else "GREEN (missed)", name))
            good = good and red
    return good


if __name__ == "__main__":
    ok = run(load())
    if "--mutate" in sys.argv:
        ok = mutate() and ok
    sys.exit(0 if ok else 1)
