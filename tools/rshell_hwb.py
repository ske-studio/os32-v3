#!/usr/bin/env python3
"""Collect HW records. rshell EOT carries no status: query `echo $?` after HW.

Return the guest status when the record, echo, EOT and terminal rc agree;
return 1 for incomplete/invalid transport or records, or an unknown status.
The HW command is sent once only; even an incomplete response is saved.
"""
import re
from pathlib import Path

STATUS = {'OK': 0, 'ARG': 2, 'NOSYS': 3, 'WINDOW': 4, 'IO': 5,
          'TIMEOUT': 124, 'ABORT': 130}


def add_arguments(parser):
    parser.add_argument('--out', required=True, help='save the complete received body')
    parser.add_argument('--log-file', help='inspect the host file written by hw run/load -o')
    parser.add_argument('line', nargs='+', help='hw command without prefix, or sfs run hw ...; use -- before guest options')


def inspect(text):
    """Return (terminal status, errors); identify an unfinished begin by seq/line."""
    errors, run, last, pending, terminal = [], None, None, None, None
    pending_fill = None
    for row in text.splitlines():
        if not row.startswith('HW '):
            continue
        fields = {}
        for token in row[3:].split():
            if '=' in token:
                key, value = token.split('=', 1)
                if key in fields:
                    errors.append('duplicate field ' + key)
                fields[key] = value
        try:
            run_id = int(fields['run'])
            if run is None:
                run = run_id
            if run_id != run:
                errors.append('mixed run')
            if terminal is not None:
                errors.append('record after terminal')
            if fields.get('op') == 'end' and 'phase' not in fields:
                if terminal is not None:
                    errors.append('duplicate terminal')
                terminal = fields['rc']
                int(fields['lines']); int(fields['writes'])
                if 'log_rc' in fields:
                    int(fields['log_rc'])
                continue
            seq, line = int(fields['seq']), int(fields['line'])
            phase, op = fields['phase'], fields['op']
            rc = fields['rc']
            if not op or rc not in (('PENDING',) if phase == 'begin' else STATUS):
                errors.append('invalid operation or rc')
            numeric = []
            if re.fullmatch(r'(?:pci|io|mmio)(?:read|write)(?:8|16|32)', op):
                numeric += ['addr', 'val']
            elif re.fullmatch(r'mmiofill(?:8|16|32)', op):
                numeric += ['addr', 'val', 'count']
                if phase == 'end':
                    numeric += ['done']
            elif op == 'poll':
                numeric += ['addr', 'val']  # poll-address: P9 includes final address
                if phase == 'end':
                    numeric += ['reads', 'elapsed_us']
            elif op == 'wait':
                numeric += ['requested_us']
                if phase == 'end':
                    numeric += ['elapsed_us']
            elif phase == 'end' and rc == 'OK' and op in ('print', 'log'):
                key = 'tag' if op == 'print' else 'text'
                # hwb_log.c escapes spaces/controls as \xHH, quotes and slashes
                # with a backslash. Empty quoted strings are valid.
                if not re.fullmatch(r'"(?:[^"\\\x00-\x20\x7f]|\\(?:["\\]|x[0-9A-Fa-f]{2}))*"', fields[key]):
                    raise ValueError('invalid quoted field')
                if op == 'print':
                    numeric += ['val']
            for key in numeric:
                # P9: address resolution failures still emit begin/end records.
                if key == 'addr' and fields[key] == 'UNRESOLVED' and (
                        phase == 'begin' or (phase == 'end' and rc != 'OK')):
                    continue
                pattern = r'[0-9]+' if key in ('reads', 'requested_us', 'count', 'done') else r'0x[0-9a-fA-F]+'
                if not re.fullmatch(pattern, fields[key]):
                    raise ValueError('invalid numeric field')
            if re.fullmatch(r'mmiofill(?:8|16|32)', op):
                count = int(fields['count'])
                if count > 0xFFFFFFFF:
                    raise ValueError('fill count overflow')
                if phase == 'begin':
                    pending_fill = (seq, fields['addr'], fields['val'], count)
                else:
                    done = int(fields['done'])
                    if done > count or (rc == 'OK' and (count == 0 or done != count)):
                        raise ValueError('invalid fill progress')
                    if pending_fill != (seq, fields['addr'], fields['val'], count):
                        raise ValueError('fill does not match begin')
                    pending_fill = None
            if last is None and seq != 1:
                errors.append('sequence gap or duplicate at %d' % seq)
            if phase == 'begin':
                if pending is not None:
                    errors.append('missing end seq=%d line=%d' % pending[:2])
                if last is not None and seq != last + 1:
                    errors.append('sequence gap or duplicate at %d' % seq)
                last = seq
                pending = (seq, line, op)
            elif phase == 'end':
                if pending is not None and seq == pending[0]:
                    if (line, op) != pending[1:]:
                        errors.append('end does not match begin seq=%d' % seq)
                    pending = None
                else:
                    if pending is not None:
                        errors.append('missing end seq=%d line=%d' % pending[:2])
                        pending = None
                    if last is not None and seq != last + 1:
                        errors.append('sequence gap or duplicate at %d' % seq)
                    last = seq
            else:
                errors.append('invalid phase')
        except (KeyError, ValueError):
            errors.append('malformed HW record: ' + row)
    if pending is not None:
        errors.append('missing end seq=%d line=%d' % pending[:2])
    if terminal is None:
        errors.append('missing terminal')
    elif terminal not in STATUS:
        errors.append('unknown terminal rc ' + terminal)
    return terminal, errors


def terminal_row(text):
    return next((row for row in text.splitlines()
                 if row.startswith('HW ') and ' op=end ' in row and ' phase=' not in row), '')


def inspect_file(path):
    """Apply the same run/sequence/end checks to a LAN host log."""
    text = Path(path).read_text(encoding='utf-8')
    terminal, errors = inspect(text)
    return text, terminal, errors


def run(args, send, baud=9600, out=print):
    del baud
    # LAN uses an independently running lanfs_host (--allow-write).
    wrapped = args.line[:3] == ['sfs', 'run', 'hw']
    command = args.line[3:] if wrapped else args.line
    file_output = command[:2] in (['run', '-o'], ['load', '-o'])
    line = ('' if wrapped else 'hw ') + ' '.join(args.line)
    if '\n' in line or '\r' in line:
        out('[hwb] command must be one line')
        return 2
    text, error = send(line, args.timeout)
    with open(args.out, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    terminal, errors = inspect(text)
    if error:
        errors.append(error)
    expected = STATUS.get(terminal)
    if file_output:
        match = re.search(r' log_rc=(-?[0-9]+)$', terminal_row(text))
        if not match:
            errors.append('missing log_rc')
        elif int(match[1]):
            expected = STATUS['IO']
        host_file = getattr(args, 'log_file', None)
        if host_file and match and not int(match[1]):
            try:
                saved, file_terminal, file_errors = inspect_file(host_file)
                errors.extend('file: ' + item for item in file_errors)
                if (file_terminal != terminal or terminal_row(saved) !=
                        terminal_row(text).rsplit(' log_rc=', 1)[0]):
                    errors.append('file/serial terminal mismatch')
            except (OSError, UnicodeError) as exc:
                errors.append('file: ' + str(exc))
    elif getattr(args, 'log_file', None):
        errors.append('--log-file requires run/load -o')
    if errors:
        for item in errors:
            out('[hwb] ' + item)
        return 1
    status_text, error = send('echo $?', args.timeout)
    if error:
        out('[hwb] status: ' + error)
        return 1
    lines = [s.strip() for s in status_text.splitlines() if s.strip()]
    values = [s for s in lines if re.fullmatch(r'[0-9]+', s)]
    if len(values) != 1 or int(values[0]) != expected:
        out('[hwb] missing or inconsistent guest status')
        return 1
    out('[hwb] complete rc=%s (%d)' % (terminal, expected))
    return expected
