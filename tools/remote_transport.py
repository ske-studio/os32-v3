"""Client for the host agent's framed operator socket.

request returns a result mapping, a pre-submit ``serial`` decision, or
``unknown`` when submission may have occurred. Other operations return the
agent's JSON mappings; log events interleaved with replies are queued.
"""
import json
import os
import select
import socket
import struct
import time

RECORD_MAX = 16 * 1024
READY_TIMEOUT = 4.0


class CommandTransport:
    def __init__(self, host=None, port=None):
        self.host = host if host is not None else os.getenv('OS32_OPERATOR_HOST', '127.0.0.1')
        self.port = int(port if port is not None else os.getenv('OS32_OPERATOR_PORT', '8027'))
        self.sock = None
        self.events = []

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def _connect(self, deadline):
        if self.sock is None:
            self.sock = socket.create_connection((self.host, self.port), self._remaining(deadline))

    @staticmethod
    def _remaining(deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('operator timeout')
        return remaining

    def _read(self, size, deadline):
        data = bytearray()
        while len(data) < size:
            if not select.select([self.sock], [], [], self._remaining(deadline))[0]:
                raise TimeoutError('operator timeout')
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise ConnectionError('operator disconnected')
            data.extend(chunk)
        return bytes(data)

    def _receive(self, deadline):
        size = struct.unpack('!I', self._read(4, deadline))[0]
        if size > RECORD_MAX:
            raise ValueError('operator record too long')
        obj = json.loads(self._read(size, deadline).decode('utf-8'))
        if not isinstance(obj, dict):
            raise ValueError('operator response is not an object')
        return obj

    def _call(self, op, deadline, **fields):
        self._connect(deadline)
        data = json.dumps(dict(op=op, **fields), separators=(',', ':')).encode('utf-8')
        if len(data) > RECORD_MAX:
            raise ValueError('operator request too long')
        wire = memoryview(struct.pack('!I', len(data)) + data)
        while wire:
            if not select.select([], [self.sock], [], self._remaining(deadline))[1]:
                raise TimeoutError('operator timeout')
            wire = wire[self.sock.send(wire):]
        while True:
            obj = self._receive(deadline)
            if obj.get('event') == 'log':
                self.events.append(obj)
            else:
                return obj

    def call(self, op, timeout, **fields):
        try:
            return self._call(op, time.monotonic() + timeout, **fields)
        except (OSError, ValueError, UnicodeError):
            self.close()
            raise

    def probe(self, timeout=4):
        return self.call('probe', timeout)

    def submit(self, mode, line, timeout):
        return self.call('submit', timeout, mode=mode, line=line)

    def result(self, ident, wait_ms, timeout):
        return self.call('result', timeout, id=ident, wait_ms=wait_ms)

    def resume(self, timeout=4):
        return self.call('resume', timeout)

    def notice(self, wait_ms, timeout):
        return self.call('notice', timeout, wait_ms=wait_ms)

    def subscribe_log(self, timeout=4):
        return self.call('log', timeout)

    def next_log(self, timeout):
        if self.events:
            return self.events.pop(0)
        try:
            event = self._receive(time.monotonic() + timeout)
            if event.get('event') != 'log':
                raise ValueError('unexpected operator response')
            return event
        except (OSError, ValueError, UnicodeError):
            self.close()
            raise

    def request(self, mode, line, timeout):
        deadline = time.monotonic() + timeout
        ready_deadline = min(deadline, time.monotonic() + READY_TIMEOUT)
        try:
            while True:
                probe = self._call('probe', ready_deadline)
                ready = probe.get('ready')
                if probe.get('ok') and probe.get('hello') and ready and ready.get('mode') == mode:
                    break
                time.sleep(min(0.05, self._remaining(ready_deadline)))
        except (OSError, ValueError, UnicodeError):
            self.close()
            return {'state': 'serial'}
        try:
            submitted = self._call('submit', deadline, mode=mode, line=line)
            if not submitted.get('ok'):
                return submitted
            ident = submitted['id']
            while True:
                wait_ms = max(0, int(self._remaining(deadline) * 1000))
                result = self._call('result', deadline, id=ident, wait_ms=wait_ms)
                if not result.get('ok') or result.get('state') != 'pending':
                    return result
        except (OSError, ValueError, UnicodeError, KeyError):
            self.close()
            return {'state': 'unknown'}
