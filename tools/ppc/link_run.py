"""Pump the LAN connection while waiting for QMP replies."""
import json
import os
import select
import time

from run import QMP, require


class PumpQMP(QMP):
    def __init__(self, process, deadline, pump):
        self.pump = pump
        super().__init__(process, deadline)
    def read(self):
        while b'\n' not in self.buffer:
            self.pump()
            require(time.monotonic() < self.deadline, 'QMP deadline')
            if select.select([self.process.stdout], [], [], 0.005)[0]:
                data = os.read(self.process.stdout.fileno(), 4096)
                require(data, 'QMP EOF')
                self.buffer += data
        self.pump()
        line, self.buffer = self.buffer.split(b'\n', 1)
        return json.loads(line)
