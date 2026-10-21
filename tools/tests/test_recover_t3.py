"""T3 actual core fake-wire tests and focused mutations; no hardware."""
import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar
import test_recover as base

sys.path.insert(0, str(base.ROOT / "tools"))
import rshell_recover

MUTATIONS = [
    ('t3_old_session', 'rx_word(r->rx + 2) == r->token.session && r->token.session &&', 'r->token.session &&'),
    ('t3_rx_cap', 'n < RECOVER_RX_LIMIT; n++', 'n < RECOVER_RX_LIMIT + 1; n++'),
    ('t3_crc', 'rx_word(r->rx + 6) == crc32_core_final(crc32_core_update(CRC32_INIT, r->rx, 6))', 'true'),
    ('t3_abort_ignored', 'bool abort = wire_abort || (r->ops->abort_requested &&', 'bool abort = (wire_abort && false) || (r->ops->abort_requested &&'),
    ('t3_lost_not_counted', 'r->lost_lines++; fail(r, REC_LOG, REC_PHASE_NONE, 0);', 'fail(r, REC_LOG, REC_PHASE_NONE, 0);'),
    ('t3_partial_retained', 'discard(r, r->rx_used); r->rx_used = 0;', 'discard(r, r->rx_used);'),
]


def one(item):
    name, before, after = item
    source = (base.ROOT / base.SOURCE).read_text()
    if before and source.count(before) != 1:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='recover-t3-mut-') as td:
        exe = pathlib.Path(td) / 'core'
        try:
            tree = mutpar.build_in_tree(base.ROOT, td,
                {base.SOURCE: source.replace(before, after, 1) if before else source},
                [base.host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': COMPILE FAIL ' + str(exc.stderr), 1
        try:
            result = host32.run([str(exe)], cwd=str(tree), timeout=30, capture_output=True)
        except subprocess.TimeoutExpired:
            return name + ': runtime boundedness failure', 0
        return name + (': GREEN' if result.returncode == 0 else ': RED rc=' + str(result.returncode)), int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='recover-t3-') as td:
        exe = pathlib.Path(td) / 'core'
        subprocess.run(base.host_cmd(exe) + ["-DRECOVER_T3_CAPTURE"], check=True, cwd=base.ROOT)
        result = host32.run([str(exe)], cwd=base.ROOT, timeout=30, capture_output=True)
        if result.returncode:
            print(result.stdout.decode(), end='')
            return result.returncode
        text = result.stdout.decode()
        transcript = text.split('T3 WIRE BEGIN\n', 1)[1].split('T3 WIRE END\n', 1)[0]
        records = rshell_recover.Records(1)
        for line in transcript.splitlines():
            records.line(line)
        assert records.success(), records.data
        assert records.final['discarded_bytes'] == '2'
        print('recover T3 core + host serializer contract: PASS')
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
