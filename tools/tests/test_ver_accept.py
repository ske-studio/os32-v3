"""V12: real formatter/CUI output through existing identity consumers, offline.

V8 owns the full-line oracle; V11 owns equality with the Rust reader/layout.
This suite reuses V11's snapshots, including FD/HDD/?/none/missing boot info.
--mutate corrupts an accepted CUI fixture and reruns the same acceptance check.
"""
import io
import json
import re
import sys

from ver_test_utils import ROOT, arguments, execute, function

sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'sdk'))
import rshell_peek as peek
import rshell_recover as recover
import os32x_hdr as header


class Port:
    """In-memory serial reply only: never opens a port, HTTP or the emulator."""
    def __init__(self, text):
        self.data = bytearray(('> ver\r\n' + text.replace('\n', '\r\n') + '\x04').encode())
        self.writes = []
        self.t = 0

    def clock(self):
        self.t += .001
        return self.t

    def read(self, count):
        data = bytes(self.data[:min(count, 7)])
        del self.data[:len(data)]
        return data

    def write(self, data):
        self.writes.append(data)
        return len(data)


def reports(runner):
    files = {
        'ver_format_source.c': (ROOT / 'kernel/ver_format.c').read_text(),
        'ver_state_source.c': function((ROOT / 'kernel/module/module.c').read_text(),
                                      'const char *kdebug_state_name('),
        'ver_shell_source.c': function((ROOT / 'userland/shell/cmd_base.c').read_text(),
                                      'static int cmd_ver('),
    }
    result = execute((ROOT / 'tools/tests/ver_about_host.c').read_text(), runner,
                     files, assembly='lib/kstring_asm.asm')
    assert result.returncode == 0, result.stderr
    cases = []
    for case in result.stdout.split('REPORT\n')[1:]:
        report, cui = case.split('CUI\n')
        assert cui.endswith('END\n')
        cases.append((report, cui.removesuffix('END\n')))
    assert len(cases) == 26
    return cases


def accept(report, cui):
    """The same assertions must reject a corrupted formerly accepted fixture."""
    entries = [line.split('\t', 1) for line in report.splitlines()]
    lines = cui.splitlines()
    assert len(lines) == len(entries)
    assert lines[0] == 'PC-9821 OS32 v3.0-dev (Ring3 Native)'
    assert sum(line.startswith('sqlite stack high-water=') for line in lines) == 1
    # V11 includes both the ordinary public=1 and u32-maximum snapshots.
    assert lines[-1] in ('  services: SERVICES_READY public=1',
                         '  services: SERVICES_READY public=4294967295')
    assert sum('services:' in line for line in lines) == 1

    # These are the predicates actually read by the real-hardware update skill.
    # Read only; this test does not invoke that deployment workflow.
    skill = (ROOT / '.claude/skills/os32-realhw-update/SKILL.md').read_text()
    assert 'ver (Commit・Image CRC・services の行が SERVICES_READY)' in skill
    assert 'ver の Commit と Image CRC が依頼した SHA の成果物と一致するか。services が SERVICES_READY か。' in skill
    commit_lines = [text for _, text in entries if text.startswith('Commit: ')]
    crc_lines = [text for _, text in entries if text.startswith('Image CRC: ')]
    assert len(commit_lines) == len(crc_lines) <= 1
    commit, crc = peek.parse_ver(cui)  # Real reader, not a copy of its regex.
    if not commit_lines:
        assert commit is None and crc is None
        return 'missing'
    assert commit == commit_lines[0].removeprefix('Commit: ')
    assert lines.count('  ' + commit_lines[0]) == 1
    assert lines.count('  ' + crc_lines[0]) == 1
    match = re.fullmatch(r'Image CRC: ([0-9a-f]{8}) \((\d+) bytes, (HDD|FD|\?) loader\)',
                         crc_lines[0])
    if match:
        assert crc == int(match[1], 16)
        loader = match[3]
    else:
        assert crc_lines[0] == 'Image CRC: none (loader did not record)' and crc is None
        loader = 'none'
    port = Port(cui)
    raw = io.BytesIO()
    try:
        recover.verify_boot(port, match[1] if match else '0123abcd', 10, raw, port.clock)
    except recover.RecoverError:
        assert loader in ('?', 'none'), 'valid HDD/FD output rejected'
    else:
        assert loader in ('HDD', 'FD'), 'unrecorded/unknown boot accepted'
    assert port.writes == [b'ver\n']
    assert raw.getvalue().startswith(b'> ver\r\n') and raw.getvalue().endswith(b'\x04')
    return loader


def min_api(runner):
    version = json.loads((ROOT / 'sdk/kapi.json').read_text())['version']
    assert version == 79 and header.OS32X_MIN_API == version
    # Real header generation raises low app.conf requests without editing them.
    for requested in (0, 63, 78, version, version + 1):
        blob = header.build_header(0, 0, 1, 0, 0, requested, header.OS32X_APP_LOAD_ADDR,
                                   0x4b8)
        assert header.parse_header(blob)['min_api_ver'] == max(requested, version)
    # Execute the actual exec guard against both kernel generations. This is
    # the API-version gate only; PM checks complete old/new binaries in guest.
    source = (ROOT / 'exec/exec.c').read_text()
    guard = re.search(r'else if \((hdr->min_api_ver > KAPI_VERSION)\) reason = '
                      r'"required KAPI version is newer than kernel";', source)
    assert guard, 'exec API-version guard changed'
    fixture = '''
#include "support.h"
static int newer(u32 requested, u32 KAPI_VERSION) {
    struct { u32 min_api_ver; } value = {requested}, *hdr = &value;
    return GUARD;
}
void _start(void) {
    CHECK(newer(79, 78)); CHECK(!newer(78, 79)); CHECK(!newer(79, 79));
    die(0);
}
'''.replace('GUARD', guard[1])
    assert execute(fixture, runner).returncode == 0
    print('PASS effective min_api=79; v78 rejects new / v79 accepts old API requirement')


def main():
    args = arguments()
    cases = reports(args.runner)
    assert {accept(report, cui) for report, cui in cases} == {'HDD', 'FD', '?', 'none', 'missing'}
    min_api(args.runner)
    print('PASS ver acceptance: existing readers, skill predicates, stack/services exactly once')
    if args.mutate:
        report, cui = cases[0]
        old = '  Image CRC: '
        assert cui.count(old) == 1
        try:
            accept(report, cui.replace(old, ' Image CRC: ', 1))
        except AssertionError:
            print('RED: accepted fixture CRC indentation changed to one space')
        else:
            raise AssertionError('MISSED: one-space CRC fixture')


if __name__ == '__main__':
    main()
