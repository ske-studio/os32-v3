#!/usr/bin/env python3
"""P2 diagnostic acceptance plan, attached to link_run's 7450/7455 pump.

Run after host PDIAG/Operator diag support lands. This deliberately uses the
real host handler: today's 400/INVALID_LINE is a failed prerequisite, not PASS.
R1 is unnecessary: this stage changes no entry, paging or execution layout.
Body contract: id8 SP decimal opcode [SP existing big-endian argument hex] LF.
"""
import argparse
import os
import shutil
from pathlib import Path
import struct
import sys
from run import require
import link_run


class Diagnostics:
    def __init__(self):
        self.index = 0
        self.ident = None
        self.results = []
        # Operator diag takes the PLN1 CLI words; out-of-range lengths are refused by
        # the host before sending, and the guest bounds are fixed by ppc-link-diag-host.
        self.cases = [('status', 0, 68), ('clock', 0, 24), ('memory', 0, 32),
                      ('read-diag 0x80 0x80', 0, 128), ('peek 0x100000 0x100', 0, 256),
                      ('peek 0 1', 3, 0)]

    def __call__(self, agent, out):
        if self.index == len(self.cases):
            return True
        operator = agent.operator
        ready = operator.ready
        if not agent.established or not ready or ready['mode'] != 'diag':
            return False
        if self.ident is None:
            line, _, _ = self.cases[self.index]
            try:
                self.ident = operator.submit('diag', line, None)
            except ValueError as exc:
                raise RuntimeError('host Operator diag support required') from exc
            return False
        pending = operator.pending
        require(pending is not None and pending['id'] == self.ident, 'diagnostic ID lost')
        require(pending['state'] != 'UNKNOWN', 'diagnostic result UNKNOWN; no automatic retry')
        result = operator.take_result()
        if result is None:
            return False
        line, status, length = self.cases[self.index]
        body = result['result']
        # PDIAG status is separate from link's HTTP-like status 200.
        require(result.get('status') == status, 'diagnostic status '+line)
        require(isinstance(body, bytes) and len(body) == length, 'diagnostic length '+line)
        if self.index == 1:
            tick, hz, tb, us = struct.unpack('>IIQQ', body)
            require(hz > 0 and tb > 0, 'CLOCK fields')
        self.results.append(dict(id=self.ident, command=line, status=status, length=length))
        self.index += 1
        self.ident = None
        if self.index == len(self.cases):
            import json
            (out / 'diagnostics.json').write_text(json.dumps(self.results, indent=2))
            require(agent.counts['ack'] > 0, 'diagnostic NOTIFY ACK missing')
            return True
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    link_run.PREFIX = str(cross / 'bin/powerpc-elf-')
    missing = [name for name in (link_run.PREFIX+'nm', 'qemu-system-ppc', 'genisoimage')
               if shutil.which(name) is None]
    if missing:
        print('SKIP: missing PowerPC acceptance prerequisites: '+', '.join(missing))
        return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    args.out.mkdir(parents=True, exist_ok=True)
    for cpu in ('7450', '7455'):
        # The base harness also requires ACK and zero duplicate REQUEST rids.
        try:
            link_run.accept(args.elf.resolve(), cpu, args.out.resolve(), args.timeout,
                            require_ack=True, diagnostic=Diagnostics())
        except PermissionError as exc:
            print('SKIP: acceptance socket unavailable: '+str(exc))
            return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
