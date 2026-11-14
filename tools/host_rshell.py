"""Generation-local LAN operator state (LAN_OPERATOR_DESIGN sections 2-3)."""
import re

RSHELL = 65534  # REMOTE_SVC_RSHELL
GSERIAL = 65533  # REMOTE_SVC_GSERIAL
EXECUTE = 1  # REMOTE_NOTIFY_EXECUTE
RESULT_MAX = 8193  # REMOTE_RESULT_BODY_MAX


class Operator:
    def __init__(self, notify):
        self.notify = notify
        self.generation = notify.generation
        self.next_id = 1
        self.session = None
        self.ready = None
        self.pending = None
        self.notices = []

    def session_start(self, session):
        self.session = session
        self.ready = None

    def session_reset(self):
        self.session = None
        self.ready = None
        if self.pending is not None:
            self.pending['state'] = 'UNKNOWN'

    def accept_ready(self, generation, session, boot, mode, last_id, state):
        if (generation != self.generation or session != self.session or
                session is None or not re.fullmatch(r'[0-9a-fA-F]{32}', boot) or
                mode not in ('cui', 'gui', 'diag') or
                state not in ('idle', 'running', 'done') or
                (last_id != 'none' and not re.fullmatch(r'[0-9a-f]{8}', last_id)) or
                (last_id == 'none' and state != 'idle')):
            return False
        boot = boot.lower()
        if self.pending is not None and self.pending['boot'] != boot:
            self.pending['state'] = 'UNKNOWN'
        self.ready = dict(boot=boot, mode=mode, last_id=last_id, state=state)
        return True

    def submit(self, mode, line, client):
        if self.pending is not None:
            raise RuntimeError('busy')
        if self.session is None or self.ready is None or self.ready['mode'] != mode:
            raise RuntimeError('not ready')
        if self.ready['state'] == 'running':
            raise RuntimeError('busy')
        if self.next_id > 0xffffffff:
            raise RuntimeError('id exhausted')
        if mode not in ('cui', 'gui') or not isinstance(line, str):
            raise ValueError('invalid mode or line')
        if not line or any(ord(c) < 32 or ord(c) > 126 for c in line):
            raise ValueError('invalid line')
        encoded = line.encode('ascii')
        if (mode == 'cui' and len(encoded) > 126) or (mode == 'gui' and len(encoded) + 9 > 128):
            raise ValueError('line too long')
        ident = '%08x' % self.next_id
        data = ident.encode() + b' ' + encoded + b'\n'
        if mode == 'gui':
            data = b'\x01' + data
        self.next_id += 1
        self.pending = dict(id=ident, boot=self.ready['boot'], mode=mode,
                            line=line, client=client, state='running', result=None)
        try:
            self.notify.notify_number(RSHELL if mode == 'cui' else GSERIAL, EXECUTE, data)
        except Exception:
            self.unknown()
            raise
        return ident

    def unknown(self):
        if self.pending is not None:
            self.pending['state'] = 'UNKNOWN'
            self.ready = None

    def resume(self):
        if (self.pending is None or self.pending['state'] != 'UNKNOWN' or
                self.ready is None or self.ready['boot'] != self.pending['boot']):
            raise RuntimeError('cannot resume')
        self.pending = None

    def accept_output(self, generation, session, boot, ident, dropped, body, length, mode):
        if (generation != self.generation or session != self.session or session is None or
                self.ready is None or boot.lower() != self.ready['boot']):
            return False
        if dropped != 0 or length != len(body) or length > RESULT_MAX:
            return False
        if mode == 'gui' and ident == '!':
            if not body.startswith(b'gs:! '):
                return False
            self.notices.append(body)
            return True
        pending = self.pending
        if (pending is None or pending['boot'] != boot.lower() or
                pending['id'] != ident or pending['mode'] != mode):
            return False
        if mode == 'cui':
            if not body.endswith(b'\x04'):
                return False
            text = body[:-1].lstrip(b'\r\n')
            if pending['line'] != 'exit' and text.split(b'\n', 1)[0].rstrip(b'\r') != b'> ' + pending['line'].encode('ascii'):
                return False
        elif mode == 'gui':
            prefix = b'gs:' + ident.encode('ascii') + b' '
            lines = body.splitlines()
            if not lines or any(not line.startswith(prefix) for line in lines):
                return False
            if re.fullmatch(rb'end [0-4]', lines[-1][len(prefix):]) is None:
                return False
        else:
            return False
        pending['result'] = body
        pending['state'] = 'done'
        return True

    def take_result(self):
        if self.pending is None or self.pending['state'] != 'done':
            return None
        result = self.pending
        self.pending = None
        return result
