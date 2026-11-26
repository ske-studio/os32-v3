#!/usr/bin/env python3
"""Aggregate saved GFA1/GFW1 serial records; no guest or hardware operations."""
import argparse
import json
from pathlib import Path

MODES = ('byte-uc', 'dword-uc', 'dword-wc')
RECTS = ((0, 0), (640, 480), (320, 200), (640, 16), (32, 32),
         (16, 16), (63, 32), (1, 480), (1, 1))
FAILED = 0xffffffff


def stats(values):
    ordered = sorted(v for v in values if v != FAILED)
    if not ordered:
        return dict(count=0, failures=len(values), p50=None, p95=None, maximum=None)
    return dict(count=len(ordered), failures=len(values)-len(ordered),
                p50=ordered[(len(ordered)*50+99)//100-1],
                p95=ordered[(len(ordered)*95+99)//100-1], maximum=ordered[-1])


def cache_lines(cpu):
    """SDM CPUID leaf4, or P6 leaf2 L1 descriptors; never leaf1 EBX."""
    lines = {((b & 0xfff)+1) for leaf, _, a, b, _, _ in cpu
             if leaf == 4 and a & 0x1f}
    if lines:
        return sorted(lines)
    descriptors = {0x06: 32, 0x08: 32, 0x0a: 32, 0x0c: 32,
                   0x2c: 64, 0x30: 64, 0x66: 64, 0x67: 64, 0x68: 64}
    for leaf, _, a, b, c, d in cpu:
        if leaf != 2:
            continue
        for index, value in enumerate((a, b, c, d)):
            if value & 0x80000000:
                continue
            for byte in range(1 if index == 0 else 0, 4):
                descriptor = (value >> (byte*8)) & 0xff
                if descriptor in descriptors:
                    lines.add(descriptors[descriptor])
    return sorted(lines) or 'unknown'


def cpu_signature(cpu):
    for leaf, _, a, _, _, _ in cpu:
        if leaf == 1:
            family, model = (a >> 8) & 15, (a >> 4) & 15
            if family in (6, 15):
                model |= ((a >> 16) & 15) << 4
            if family == 15:
                family += (a >> 20) & 255
            return dict(family=family, model=model, stepping=a & 15)
    return 'unknown'


def parse_log(text, build_id, require_wm=False, pair_crc=None):
    mode = None
    state = None
    groups, cpu, msrs, ptes, wm = {}, [], {}, {}, {}
    pci = None
    pair = None
    done = pattern = False
    for raw in text.splitlines():
        if not raw.startswith(('GFA1 ', 'GFW1 ')):
            continue
        prefix, version, observed, kind, *fields = raw.split()
        if version != build_id or observed not in MODES or (mode and mode != observed):
            raise ValueError('build ID or mode mismatch')
        mode = observed
        if prefix == 'GFW1':
            if kind != 'sample' or len(fields) != 5:
                raise ValueError('invalid WM record')
            seq, *times = map(int, fields)
            if seq in wm or not 0 <= seq < 300:
                raise ValueError('duplicate/out-of-range WM sample')
            wm[seq] = times
        elif kind == 'pair':
            if pair is not None or len(fields) != 1:
                raise ValueError('invalid pair record')
            pair = int(fields[0])
        elif kind == 'state':
            if state or len(fields) != 3:
                raise ValueError('duplicate/invalid state')
            state = fields
        elif kind == 'sample':
            values = list(map(int, fields))
            if len(values) != 106:
                raise ValueError('incomplete sample record')
            case, run, byte_count, points, failed, bad, *times = values
            if not 0 <= case < len(RECTS) or not 0 <= run < 3 or (case, run) in groups:
                raise ValueError('duplicate/out-of-range sample group')
            if byte_count != RECTS[case][0]*RECTS[case][1] or bad or failed:
                raise ValueError('transfer/readback failure or byte count mismatch')
            if points != 100*RECTS[case][1]:
                raise ValueError('point count mismatch')
            if any(not 0 <= t <= FAILED for t in times):
                raise ValueError('invalid duration')
            groups[case, run] = dict(times=times, points=points, bytes=byte_count)
        elif kind == 'cpu':
            values = list(map(int, fields))
            if len(values) != 6:
                raise ValueError('invalid CPUID record')
            cpu.append(values)
        elif kind == 'msr':
            phase, reg, lo, hi = map(int, fields)
            if (phase, reg) in msrs:
                raise ValueError('duplicate MSR record')
            msrs[phase, reg] = (hi << 32) | lo
        elif kind == 'pte':
            phase, page, value = map(int, fields)
            if (phase, page) in ptes:
                raise ValueError('duplicate PTE record')
            ptes[phase, page] = value
        elif kind == 'pci':
            if pci is not None or len(fields) != 5:
                raise ValueError('invalid PCI record')
            pci = list(map(int, fields))
        elif kind == 'pattern':
            pattern = fields == ['0..255']
        elif kind == 'done':
            done = fields == ['OK']
        else:
            raise ValueError('unknown acceptance record')
    if not mode or not state or pci is None or pair is None:
        raise ValueError('missing version/state/PCI record')
    if pair_crc is not None and pair != pair_crc:
        raise ValueError('kernel/module pair CRC mismatch')
    effective, status, reason = state
    expected = 'WC' if mode == 'dword-wc' else 'UC'
    result = dict(build_id=build_id, pair_crc=pair, mode=mode, effective=effective, status=status,
                  reason=reason, pci=dict(command=pci[0], cache_line_dwords=pci[1] & 0xff,
                  latency=(pci[1] >> 8) & 0xff, bars=pci[2:]), cpu_raw=cpu,
                  cpu=cpu_signature(cpu),
                  cpu_cache_line_bytes=cache_lines(cpu),
                  msr_raw={f'{p}:{r}': v for (p, r), v in msrs.items()},
                  pte_raw={f'{p}:{i}': v for (p, i), v in ptes.items()})
    if status == 'SKIP':
        if reason not in ('bios-wc', 'unclassified', 'wc-unavailable') or groups or done:
            raise ValueError('invalid SKIP or samples in skipped mode')
        return result
    if status != 'OK' or effective != expected:  # Never label BIOS WC as UC.
        raise ValueError('effective memory type does not match comparison group')
    if not done or not pattern or len(groups) != len(RECTS)*3:
        raise ValueError('incomplete measurement series')
    for page in range(75):
        if (0, page) not in ptes or (1, page) not in ptes:
            raise ValueError('missing PTE observation')
        if (ptes[0, page] ^ ptes[1, page]) & ~0x60:
            raise ValueError('PTE changed')
    if any(t == FAILED for group in groups.values() for t in group['times']):
        result['status'] = 'FAIL'  # Failed clocks never enter the success population.
    result['rectangles'] = {}
    for case, (w, h) in enumerate(RECTS):
        runs = [groups[case, r] for r in range(3)]
        result['rectangles'][str(case)] = dict(width=w, height=h, bytes=w*h,
            points=[g['points'] for g in runs], runs=[stats(g['times']) for g in runs],
            pooled=stats([v for g in runs for v in g['times']]))
    if wm:
        if set(wm) != set(range(300)):
            raise ValueError('incomplete WM measurement series')
        for values in wm.values():
            if any(not 0 <= v <= FAILED for v in values) or (FAILED not in values and values[0] != sum(values[1:])):
                raise ValueError('invalid WM phase accounting')
        result['wm'] = {name: stats([wm[i][column] for i in range(300)])
                        for column, name in enumerate(('total', 'event', 'composite', 'present'))}
        if any(FAILED in values for values in wm.values()):
            result['status'] = 'FAIL'
    elif require_wm:
        raise ValueError('missing WM observations')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-id', required=True)
    parser.add_argument('--pair-crc', type=lambda value: int(value, 0), required=True,
                        help='kernel Image CRC, e.g. 0x1234abcd (same value as module pair)')
    parser.add_argument('--require-wm', action='store_true')
    parser.add_argument('logs', type=Path, nargs=3, help='one saved serial log per mode')
    args = parser.parse_args()
    try:
        results = [parse_log(path.read_text(), args.build_id, args.require_wm, args.pair_crc) for path in args.logs]
        if {r['mode'] for r in results} != set(MODES):
            raise ValueError('three distinct comparison modes required')
        # CPU, firmware PCI configuration and original mappings must agree.
        reference = results[0]
        for item in results[1:]:
            if item['pci'] != reference['pci'] or item['cpu_raw'] != reference['cpu_raw']:
                raise ValueError('CPU/PCI settings differ across builds')
            for key in ('msr_raw', 'pte_raw'):
                before = {k: v & (~0x60 if key == 'pte_raw' else -1)
                          for k, v in item[key].items() if k.startswith('0:')}
                baseline = {k: v & (~0x60 if key == 'pte_raw' else -1)
                            for k, v in reference[key].items() if k.startswith('0:')}
                if before != baseline:
                    raise ValueError('original MTRR/PTE settings differ across builds')
        print(json.dumps(results, indent=2))
        return int(any(r['status'] == 'FAIL' for r in results))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    raise SystemExit(main())
