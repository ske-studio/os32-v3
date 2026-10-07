"""E11-9: actual generated wrapper and saved caller policy (ILP32)."""
import argparse
from test_kapi_ranges import ROOT, run
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
p.add_argument('--mutate', action='store_true')
a = p.parse_args()
run(a.runner, fixture_name='tvram_auth_host.c')
if a.mutate:
    for name, old, new in (
        ('authorization omitted', 'if (!exec_tvram_read_allowed())', 'if (0)'),
        ('denied code not cleared', '*code = 0;', '*code = 1;'),
        ('denied attr not cleared', '*attr = 0;', '*attr = 1;'),
    ):
        run(a.runner, (name, 'generated', old, new, '*code ==' if 'attr' not in name else '*attr =='), fixture_name='tvram_auth_host.c')

    source = (ROOT/'exec/exec.c').read_text()
    start = source.index('int exec_tvram_read_allowed(void)')
    body = source[start:source.index('\n}', start)+2]
    for old, new in (
        ('ring3_wm_depth || ', ''),
        (' || con_sink_is_enabled()', ''),
        (' || slot->gui', ''),
        ('slot->state != APP_STATE_RUNNING', '0'),
        ('appslot_cur() == APP_ID_SHELL && ', ''),
        ('!slot->cpl3 &&', ''),
        ('res_owner_get() == APP_ID_SHELL', '1'),
        ('slot->cpl3 && ', ''),
        ('caller_access_get_user(&caller)', '((void)caller, 1)'),
    ):
        assert old in body
        changed = body.replace(old, new)
        run(a.runner, (old, 'disk', body, changed, ''), fixture_name='tvram_auth_host.c')
