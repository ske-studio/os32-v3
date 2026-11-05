"""Host-side independent notification sender (NET_HOSTSVC_DESIGN §5)."""
from collections import deque
import secrets
import struct

OP_NOTIFY = 11
DATA_MAX = 236
QUEUE_MAX = 64
WINDOW_MAX = 4
RETRANSMIT_SECONDS = 0.1
ALIVE_SECONDS = 5.0


def new_generation():
    """Choose a nonzero boot identity, independent of service numbers."""
    return secrets.randbelow(0xffffffff) + 1


class Notify:
    def __init__(self, registry, generation):
        if not 0 < generation <= 0xffffffff:
            raise ValueError('generation must be a nonzero u32')
        self.registry = registry
        self.generation = generation
        self.dropped = 0
        self.reset()

    def reset(self):
        self.sess = None
        self.epoch = None
        self.pending = deque()
        self.inflight = deque()
        self.next_seq = 1
        self.window = 0
        self.deadline = None
        self.next_alive = None

    def session(self, sess, epoch, now):
        self.reset()
        self.sess, self.epoch = sess, epoch
        self._alive()
        self.next_alive = now + ALIVE_SECONDS

    def hello_extension(self):
        return struct.pack('<IH', self.generation, self.registry.resolve('os32.alive'))

    def notify(self, name, kind, data):
        service = self.registry.resolve(name)
        data = bytes(data)
        if not 1 <= service <= 65534 or not 0 <= kind <= 65535 or len(data) > DATA_MAX:
            raise ValueError('invalid notification')
        payload = struct.pack('<IHHHH', self.generation, service, kind, len(data), 0) + data
        if len(self.pending) + len(self.inflight) == QUEUE_MAX:
            self.dropped += 1
            if not self.pending:
                return
            self.pending.popleft()
        self.pending.append(payload)

    def _alive(self):
        self.notify('os32.alive', 1, struct.pack('<I', self.generation))

    def ack(self, sess, epoch, ack, window):
        if (sess, epoch) != (self.sess, self.epoch) or self.sess is None:
            return
        # Acknowledgements beyond the last sent sequence cannot acknowledge data.
        if ack >= self.next_seq:
            return
        while self.inflight and self.inflight[0][0] <= ack:
            self.inflight.popleft()
        self.window = min(max(window, 0), WINDOW_MAX)
        if not self.inflight:
            self.deadline = None

    def tick(self, now):
        if self.sess is None:
            return []
        if now >= self.next_alive:
            self._alive()
            self.next_alive = now + ALIVE_SECONDS
        frames = []
        if self.inflight and self.deadline is not None and now >= self.deadline:
            frames.extend((OP_NOTIFY, 0, seq, 0, 0, 0, payload)
                          for seq, payload in self.inflight)
            self.deadline = now + RETRANSMIT_SECONDS
        while self.pending and len(self.inflight) < self.window:
            payload = self.pending.popleft()
            seq = self.next_seq
            self.next_seq += 1
            self.inflight.append((seq, payload))
            frames.append((OP_NOTIFY, 0, seq, 0, 0, 0, payload))
            if self.deadline is None:
                self.deadline = now + RETRANSMIT_SECONDS
        return frames
