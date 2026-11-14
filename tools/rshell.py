#!/usr/bin/env python3
"""Send one rshell command through the host agent or the serial fallback."""
import argparse
import base64
import binascii
import json
from pathlib import Path
import subprocess
import sys

from remote_transport import CommandTransport


def body_without_echo(body, line):
    if not body.endswith(b'\x04'):
        raise ValueError('missing EOT')
    body = body[:-1]
    if line == 'exit':
        return body
    body = body.lstrip(b'\r\n')
    echo, sep, body = body.partition(b'\n')
    if not sep or echo.rstrip(b'\r') != b'> ' + line.encode('ascii'):
        raise ValueError('echo mismatch')
    return body


def serial_command(args, line):
    if not args.port:
        print('serial requires --port', file=sys.stderr)
        return 1
    command = [sys.executable, str(Path(__file__).with_name('rshell_serial.py')),
               '--port', args.port, '--baud', str(args.baud), '--timeout',
               str(args.timeout), 'cmd', line]
    result = subprocess.run(command, stdout=subprocess.PIPE)
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
        return result.returncode
    # rshell_serial removes EOT itself, but retains the echo.
    try:
        body = body_without_echo(result.stdout + b'\x04', line)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    sys.stdout.buffer.write(body)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--transport', choices=('auto', 'lan', 'serial'), default='auto')
    parser.add_argument('--port', help='serial port, needed for serial fallback')
    parser.add_argument('--baud', type=int, default=9600)
    parser.add_argument('--timeout', type=float, default=15.0)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--probe', action='store_true')
    action.add_argument('--resume', action='store_true')
    parser.add_argument('mode', nargs='?', choices=('cmd',))
    parser.add_argument('line', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.timeout <= 0 or (args.mode is None and args.line):
        parser.error('invalid timeout or command')
    if (args.probe or args.resume) and (args.mode or args.line or args.transport == 'serial'):
        parser.error('--probe and --resume require the agent and no command')
    if not (args.probe or args.resume or args.mode):
        parser.error('specify cmd, --probe or --resume')
    line = ' '.join(args.line)
    if args.mode and not line:
        return 0
    if args.mode and (len(line) > 126 or any(ord(c) < 32 or ord(c) > 126 for c in line)):
        print('invalid command line', file=sys.stderr)
        return 1
    if args.transport == 'serial':
        return serial_command(args, line)
    client = CommandTransport()
    try:
        if args.probe or args.resume:
            result = client.probe(args.timeout) if args.probe else client.resume(args.timeout)
            print(json.dumps(result, ensure_ascii=False))
            return 0 if result.get('ok') else 1
        result = client.request('cui', line, args.timeout)
        if result.get('state') == 'serial' and args.transport == 'auto':
            return serial_command(args, line)
        if result.get('state') != 'done' or not result.get('complete') or not result.get('ok'):
            print('communication incomplete: ' + str(result.get('state', result.get('error', 'error'))), file=sys.stderr)
            return 1
        try:
            body = base64.b64decode(result['body_b64'], validate=True)
            sys.stdout.buffer.write(body_without_echo(body, line))
        except (KeyError, ValueError, UnicodeError, binascii.Error) as exc:
            print('invalid response: ' + str(exc), file=sys.stderr)
            return 1
        return 0
    except (OSError, ValueError, UnicodeError) as exc:
        print('communication failed: ' + str(exc), file=sys.stderr)
        return 1
    finally:
        client.close()


if __name__ == '__main__':
    sys.exit(main())
