"""Run the PowerMac backend and common formatter against synthetic snapshots."""
import subprocess
from ver_test_utils import ROOT, arguments, execute


def main():
    args = arguments()
    pm = ROOT / 'platform/powermac'
    source = (pm / 'ver_backend.c').read_text()
    files = {'platform_ver.h': (pm / 'platform_ver.h').read_text(),
             'formatter.c': (ROOT / 'sys/kern/ver_format.c').read_text(),
             'ppc_trap.h': '#include "sys/arch/ppc/ppc_trap.h"\n',
             'mpic.h': 'u32 pm_mpic_source_count(void);\n'}
    cases = [(None, None, None)]
    if args.mutate:
        cases += [('if (s->pvr_valid)', 'if (1)', 'out.count == 4'),
                  ('if (!s->boot.crc_valid) return;', '', 'out.count == 5'),
                  ('; active', '; inactive', 'equal(out->lines[index].text, text)')]
    for old, new, failure in cases:
        files['backend.c'] = source
        files['formatter.c'] = (ROOT / 'sys/kern/ver_format.c').read_text()
        if old:
            target = 'formatter.c' if old == '; active' else 'backend.c'
            assert files[target].count(old) == 1
            files[target] = files[target].replace(old, new)
        try:
            result = execute((ROOT / 'tools/tests/ppc_ver_host.c').read_text(),
                             args.runner, files, assembly='sys/kern/kstring_asm.asm')
        except subprocess.CalledProcessError as exc:
            print(exc.stderr)
            raise
        if old:
            assert result.returncode == 1 and 'FAIL: ' + failure in result.stdout
            print('RED:', old)
        else:
            assert result.returncode == 0
            print('GREEN: PowerMac version rows')


if __name__ == '__main__':
    main()
