"""Capture OF properties on a configured serial console already at the prompt.

Pass absolute paths from OF ls for each bridge, mac-io, i2s, channel, sound,
interrupt parent, gpio and each gpio child. Ancestors are included. With no
paths, list the root to begin discovery. Redirect stdout to the host log.
"""
import argparse
import re
from types import SimpleNamespace

from product_accept import FirmwareSession


def paths_to_visit(paths):
    nodes = {'/': ()}
    for path in paths:
        if path == '/':
            continue
        parts = path.split('/')
        if parts[0]:
            raise ValueError(f'expected absolute OF path: {path!r}')
        for i, part in enumerate(parts[1:], 1):
            if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9,._+@-]*', part):
                raise ValueError(f'invalid OF node: {part!r}')
            if len(('dev ' + part).encode('ascii')) > 73:
                raise ValueError(f'OF node too long: {part!r}')
            nodes['/' + '/'.join(parts[1:i+1])] = parts[1:i+1]
    return nodes


def capture(session, nodes):
    for path, parts in nodes.items():
        session.command('dev /')
        for part in parts:
            session.command('dev ' + part)
        reply = session.command('pwd')
        if not re.search(rb'(?m)^\s*' + re.escape(path.encode('ascii')) +
                         rb'(?:\s+ok)?\s*$', reply):
            raise ValueError(f'OF did not resolve {path}')
        session.command('.properties')
        session.command('ls')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('serial')
    parser.add_argument('path', nargs='*')
    args = parser.parse_args()
    nodes = paths_to_visit(args.path)
    with open(args.serial, 'r+b', buffering=0) as port:
        capture(FirmwareSession(SimpleNamespace(stdin=port, stdout=port)), nodes)
