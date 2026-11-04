#!/usr/bin/env python3
"""Collect HW records. rshell EOT carries no status: query `echo $?` after HW.

Return the guest status when the record, echo, EOT and terminal rc agree;
return 1 for incomplete/invalid transport or records, or an unknown status.
The HW command is sent once only; even an incomplete response is saved.
"""
import re

STATUS = {'OK': 0, 'ARG': 2, 'NOSYS': 3, 'WINDOW': 4, 'IO': 5,
          'TIMEOUT': 124, 'ABORT': 130}


def add_arguments(parser):
    parser.add_argument('--out', required=True, help='save the complete received body')
    parser.add_argument('line', nargs='+', help='hw command, without the hw prefix')


def inspect(text):
    """Return (terminal status, errors); identify an unfinished begin by seq/line."""
    errors, run, last, pending, terminal = [], None, None, None, None
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
                continue
            seq, line = int(fields['seq']), int(fields['line'])
            phase, op = fields['phase'], fields['op']
            rc = fields['rc']
            if not op or rc not in (('PENDING',) if phase == 'begin' else STATUS):
                errors.append('invalid operation or rc')
            if re.fullmatch(r'(?:pci|io|mmio)(?:read|write)(?:8|16|32)', op):
                for key in (('target', 'addr', 'val') if phase == 'begin'
                            else ('addr', 'val')):
                    if not fields[key]:
                        raise ValueError('empty field')
                for key in ('addr', 'val'):
                    if not re.fullmatch(r'0x[0-9a-fA-F]+', fields[key]):
                        raise ValueError('invalid hexadecimal field')
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


def run(args, send, baud=9600, out=print):
    del baud
    line = 'hw ' + ' '.join(args.line)
    if '\n' in line or '\r' in line:
        out('[hwb] command must be one line')
        return 2
    text, error = send(line, args.timeout)
    with open(args.out, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    terminal, errors = inspect(text)
    if error:
        errors.append(error)
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
    if len(values) != 1 or int(values[0]) != STATUS[terminal]:
        out('[hwb] missing or inconsistent guest status')
        return 1
    out('[hwb] complete rc=%s (%d)' % (terminal, STATUS[terminal]))
    return STATUS[terminal]
