"""Nonblocking TCP adapter for the generation-local operator."""
import base64
import json
import os
import re
import socket
import struct
import time

RECORD_MAX = 16 * 1024


def _b64(data):
    return base64.b64encode(data).decode('ascii')


class OperatorSocket:
    def __init__(self, addr, operator, info):
        self.operator = operator
        self.info = info
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(addr if addr is not None else ('127.0.0.1', 8027))
        self.listener.listen()
        self.listener.setblocking(False)
        self.clients = {}

    def close(self):
        for fd in list(self.clients):
            self._disconnect(fd)
        self.listener.close()

    def fileno_list(self):
        return [self.listener.fileno(), *self.clients]

    def _disconnect(self, fd):
        client = self.clients.pop(fd, None)
        if client is not None:
            client['socket'].close()

    def _send(self, client, obj):
        data = json.dumps(obj, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
        if len(data) > RECORD_MAX:
            data = b'{"ok":false,"error":"BAD_REQUEST","detail":"response too long"}'
        client['out'].extend(struct.pack('!I', len(data)) + data)

    def _flush(self, fd, client):
        try:
            if client['out']:
                n = os.write(client['socket'].fileno(), client['out'])
                del client['out'][:n]
        except BlockingIOError:
            pass
        except OSError:
            self._disconnect(fd)

    def on_readable(self, fd):
        if fd == self.listener.fileno():
            while True:
                try:
                    sock, _ = self.listener.accept()
                except BlockingIOError:
                    break
                sock.setblocking(False)
                self.clients[sock.fileno()] = dict(socket=sock, data=bytearray(),
                                                   out=bytearray(), wait=None, log=False)
            return
        client = self.clients.get(fd)
        if client is None:
            return
        try:
            data = os.read(client['socket'].fileno(), 65536)
        except BlockingIOError:
            return
        except OSError:
            self._disconnect(fd)
            return
        if not data:
            self._disconnect(fd)
            return
        client['data'].extend(data)
        while len(client['data']) >= 4:
            size = struct.unpack('!I', client['data'][:4])[0]
            if size > RECORD_MAX:
                self._send(client, dict(ok=False, error='BAD_REQUEST', detail='record too long'))
                self._flush(fd, client)
                self._disconnect(fd)
                return
            if len(client['data']) < size + 4:
                break
            raw = bytes(client['data'][4:4 + size])
            del client['data'][:4 + size]
            try:
                request = json.loads(raw.decode('utf-8'))
                if not isinstance(request, dict):
                    raise ValueError('object required')
                self._request(client, request)
            except (ValueError, UnicodeError):
                self._send(client, dict(ok=False, error='BAD_REQUEST', detail='invalid JSON or request'))
        self._flush(fd, client)

    def _error(self, client, code, detail):
        self._send(client, dict(ok=False, error=code, detail=detail))

    def _request(self, client, req):
        op = req.get('op')
        fields = {'probe': {'op'}, 'submit': {'op', 'mode', 'line'},
                  'result': {'op', 'id', 'wait_ms'}, 'resume': {'op'},
                  'notice': {'op', 'wait_ms'}, 'log': {'op'}}
        if not isinstance(op, str) or op not in fields or set(req) != fields[op]:
            raise ValueError('fields')
        if op == 'probe':
            info = self.info()
            ready = self.operator.ready
            pending = self.operator.pending
            self._send(client, dict(ok=True, agent_generation=self.operator.generation,
                commit=info['commit'], config=info['config'], peer=info['peer'],
                hello=self.operator.session is not None,
                ready={k: ready[k] for k in ('mode', 'last_id', 'state')} if ready else None,
                pending=dict(id=pending['id'], state=pending['state'].lower()) if pending else None))
        elif op == 'submit':
            if req['mode'] not in ('cui', 'gui', 'diag') or not isinstance(req['line'], str):
                raise ValueError('submit')
            pending = self.operator.pending
            if pending and pending['state'] == 'UNKNOWN':
                self._error(client, 'UNKNOWN_PENDING', 'resume required')
                return
            try:
                ident = self.operator.submit(req['mode'], req['line'], client)
            except ValueError as exc:
                self._error(client, 'INVALID_LINE', str(exc))
                return
            except RuntimeError as exc:
                code = {'busy': 'BUSY', 'not ready': 'NOT_READY'}.get(str(exc), 'BAD_REQUEST')
                self._error(client, code, str(exc))
                return
            self._send(client, dict(ok=True, id=ident))
        elif op in ('result', 'notice'):
            wait = req['wait_ms']
            if type(wait) is not int or wait < 0:
                raise ValueError('wait_ms')
            if op == 'result' and (not isinstance(req['id'], str) or
                                   not re.fullmatch(r'[0-9a-f]{8}', req['id'])):
                raise ValueError('id')
            if client['wait'] is not None:
                raise ValueError('request already waiting')
            client['wait'] = (op, req.get('id'), time.monotonic() + wait / 1000)
            pending = self.operator.pending
            if (op == 'result' and wait > 0 and pending is not None and
                    pending['id'] == req['id'] and pending['state'] == 'running'):
                # Keep the expiry with the operation even if the CLI disconnects
                # before the reply. A zero wait remains a nonblocking query.
                pending['result_deadline'] = min(pending.get('result_deadline', float('inf')),
                                                 client['wait'][2])
            self._complete_wait(client, time.monotonic())
        elif op == 'resume':
            try:
                self.operator.resume()
            except RuntimeError as exc:
                self._error(client, 'NOT_READY', str(exc))
                return
            self._send(client, dict(ok=True))
        else:
            client['log'] = True
            self._send(client, dict(ok=True))

    def _complete_wait(self, client, now):
        self._expire_result(now)
        wait = client['wait']
        if wait is None:
            return
        op, ident, deadline = wait
        if op == 'notice':
            if not self.operator.notices and now < deadline:
                return
            notices = [dict(id='!', body_b64=_b64(body)) for body in self.operator.notices]
            self.operator.notices.clear()
            response = dict(ok=True, notices=notices)
        else:
            pending = self.operator.pending
            if pending is None or pending['id'] != ident:
                response = dict(ok=False, error='NO_SUCH_ID', detail='unknown id')
            else:
                state = pending['state'].lower()
                if state == 'running' and now < deadline:
                    return
                body = pending['result'] or b''
                response = dict(ok=True, state='pending' if state == 'running' else state,
                                drop=0, body_b64=_b64(body), complete=state == 'done')
                if state == 'done':
                    self.operator.take_result()
        client['wait'] = None
        self._send(client, response)

    def _expire_result(self, now):
        pending = self.operator.pending
        if (pending is not None and pending['state'] == 'running' and
                now >= pending.get('result_deadline', float('inf'))):
            self.operator.unknown()

    def poll(self, now):
        self._expire_result(now)
        for fd, client in list(self.clients.items()):
            self._complete_wait(client, now)
            self._flush(fd, client)

    def publish_log(self, record):
        event = dict(event='log', **{k: record[k] for k in
                     ('boot', 'seq', 'drop_records', 'drop_bytes', 'flags')})
        event['text_b64'] = _b64(record['text'])
        for client in self.clients.values():
            if client['log']:
                self._send(client, event)
