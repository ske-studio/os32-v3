"""Client for the host agent's framed operator socket.

request returns a result mapping, a pre-submit ``serial`` decision, or
``unknown`` when submission may have occurred. Other operations return the
agent's JSON mappings; log events interleaved with replies are queued.
"""
import argparse
import json
import os
import select
import socket
import struct
import time

from host_agent_config import _endpoint, load_config

RECORD_MAX = 16 * 1024
READY_TIMEOUT = 4.0


class CommandTransport:
    def __init__(self, host=None, port=None, *, endpoint=None, instance=None):
        if endpoint is not None and instance is not None:
            raise ValueError('choose endpoint or instance')
        if endpoint is not None or instance is not None:
            if host is not None or port is not None:
                raise ValueError('choose host/port or endpoint/instance')
            if instance is not None:
                if not instance or instance in ('.', '..') or '/' in instance:
                    raise ValueError('invalid instance name')
                config = load_config(os.path.join('/etc/os32/host-agent', instance + '.json'))
                endpoint = config['operator-endpoint']
            host, port = _endpoint(endpoint, 'operator-endpoint')
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
        if deadline is None:
            return None
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
        return self.call('submit', timeout, mode=mode, line=line,
                         timeout_ms=max(1, int(timeout * 1000)))

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
            event = self._receive(None if timeout is None else time.monotonic() + timeout)
            if event.get('event') != 'log':
                raise ValueError('unexpected operator response')
            return event
        except (OSError, ValueError, UnicodeError):
            self.close()
            raise

    def request(self, mode, line, timeout):
        deadline = time.monotonic() + timeout
        ready_deadline = min(deadline, time.monotonic() + READY_TIMEOUT)
        while True:
            try:
                probe = self._call('probe', ready_deadline)
            except (OSError, ValueError, UnicodeError):
                self.close()
                return {'state': 'serial'}
            pending = probe.get('pending') or {}
            if pending.get('state', '').lower() in ('running', 'unknown'):
                error = 'BUSY' if pending['state'].lower() == 'running' else 'UNKNOWN_PENDING'
                return {'ok': False, 'error': error}
            ready = probe.get('ready') or {}
            if probe.get('ok') and probe.get('hello') and ready.get('mode') == mode:
                break
            remaining = ready_deadline - time.monotonic()
            if remaining <= 0.05:
                if remaining > 0:
                    time.sleep(remaining)
                if not probe.get('hello') or not ready:
                    return {'state': 'serial'}
                current = ready.get('mode', 'unavailable')
                return {'ok': False, 'error': f'mode={current}, requested={mode}'}
            time.sleep(0.05)
        try:
            submitted = self._call('submit', deadline, mode=mode, line=line,
                                   timeout_ms=max(1, int(timeout * 1000)))
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


def add_target_arguments(parser):
    target = parser.add_mutually_exclusive_group()
    target.add_argument('--endpoint', help='operator host:port')
    target.add_argument('--instance', help='host-agent configuration instance')


def main(argv=None):
    parser = argparse.ArgumentParser(description='Connect to the host agent operator')
    add_target_arguments(parser)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('log', help='display LOG events as JSON lines')
    commands.add_parser('probe', help='show agent configuration and peer')
    submit = commands.add_parser('submit', help='submit a guest command')
    submit.add_argument('mode', choices=('cui', 'gui', 'diag'))
    submit.add_argument('line')
    args = parser.parse_args(argv)
    client = CommandTransport(endpoint=args.endpoint, instance=args.instance)
    try:
        if args.command == 'log':
            response = client.subscribe_log()
            if not response.get('ok'):
                raise ValueError(str(response))
            while True:
                event = client.next_log(None)
                print(json.dumps(event, ensure_ascii=True), flush=True)
        elif args.command == 'probe':
            print(json.dumps(client.probe(), ensure_ascii=True))
        else:
            print(json.dumps(client.request(args.mode, args.line, 60), ensure_ascii=True))
    except KeyboardInterrupt:
        return 0
    finally:
        client.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
