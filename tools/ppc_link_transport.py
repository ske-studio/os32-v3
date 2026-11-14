"""PowerPC acceptance client for the agent operator JSON contract.

Diagnostics use submit(mode='diag'); service 65532 and PDIAG framing belong
 to the agent/guest, never to this client. No automatic serial retry is made.
"""
import base64
import time

from remote_transport import CommandTransport


class Host:
    def __init__(self, host=None, port=None, *, boot=None, output=print):
        self.transport = CommandTransport(host, port)
        self.boot = None
        self.constraint = boot.hex() if isinstance(boot, bytes) else boot
        self.output = output
        self.high = {}
        self.drops = {}

    def close(self):
        self.transport.close()

    def probe(self, timeout=4):
        return self.transport.probe(timeout)

    def subscribe_log(self, timeout=4):
        reply = self.transport.subscribe_log(timeout)
        if not reply.get('ok'):
            raise ValueError(f'LOG subscription failed: {reply}')
        return reply

    def receive(self, timeout=4):
        """Return one decoded LOG, or None for a boot excluded by the filter."""
        event = self.transport.next_log(timeout)
        boot = event['boot']
        if self.constraint is not None and boot != self.constraint:
            return None
        seq = event['seq']
        previous = self.high.get(boot, -1)
        if seq > previous + 1:
            self.output(f'gap: {seq - previous - 1}')
        elif seq <= previous:
            self.output(f'duplicate or out of order: {seq}')
        self.high[boot] = max(previous, seq)
        self.boot = bytes.fromhex(boot)
        records, size = event['drop_records'], event['drop_bytes']
        self.drops[boot] = (records, size)
        self.output(f'log seq={seq} drop_records={records} drop_bytes={size}; loss after last frame unknown')
        return dict(event, payload=base64.b64decode(event['text_b64'], validate=True))

    def result(self, ident, timeout=4):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'request {ident} timeout; execution may have occurred')
            reply = self.transport.result(ident, int(remaining * 1000), remaining)
            if not reply.get('ok') or reply.get('state') != 'pending':
                return reply

    def request(self, command, timeout=4):
        """Submit an existing diagnostic CLI line (PDIAG service 65532)."""
        return self._request('diag', command, timeout)

    def send_line(self, line, timeout=4):
        """Send a complete rshell line using the operator's cui mode."""
        return self._request('cui', line, timeout)

    def _request(self, mode, line, timeout):
        deadline = time.monotonic() + timeout
        reply = self.transport.submit(mode, line, timeout)
        if not reply.get('ok'):
            return reply
        return self.result(reply['id'], max(0, deadline - time.monotonic()))
