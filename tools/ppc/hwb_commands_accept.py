"""PowerMac HW_OP §5 stages 4/5a/5b/7d. mac99 OHCI, CUI stop and port results."""
import argparse
import hashlib
import json
from itertools import product
import os
from pathlib import Path
import re
import tempfile

from kboot_accept import ROOT, build_media, mutant_settings

SCRIPTS = ROOT / 'userland/tests/hw'


def build(work, mode):
    manifest = json.loads((ROOT/'build/ppc-ram-root.json').read_text())
    manifest['/hw/ohci.hwb'] = str(SCRIPTS/'mac99/ohci.hwb')
    manifest['/hw/wait.hwb'] = str(SCRIPTS/'np21w_wait.hwb')
    poll = work/'poll.hwb'
    poll.write_text('device d = id 106b:003f\npoll 32 pci d 0 0xffff 0 10000000 1000 -> x\nend\n')
    manifest['/hw/poll.hwb'] = str(poll)
    for op, via in product(('wait', 'poll'), ('lan', 'usb')):
        reload_script = work/f'{via}{op}.sh'
        reload_script.write_text(f'hw load /hw/{op}.hwb\necho loaded-{via}-{op} $?\n')
        manifest[f'/hw/{via}{op}.sh'] = str(reload_script)
    for bits in (8, 16, 32):
        for op in ('read', 'write'):
            manifest[f'/hw/{op}{bits}.hwb'] = str(SCRIPTS/f'mac99/port_{op}{bits}.hwb')
    manifest_path = work/'manifest.json'
    manifest_path.write_text(json.dumps(manifest))
    cfg = work/'system.cfg'
    cfg.write_text('KDEBUG=1\n')
    settings = []
    if mode == 'native-read':
        settings = mutant_settings(work, mode, 'platform/powermac/hwb_resource.c',
            'req->width, r->little, store, 1, 0,', 'req->width, 0, store, 1, 0,')
    if mode == 'port-ok':
        settings = mutant_settings(work, mode, 'kdebug/hwb_access.c',
            'req->status = platform_hwb_io(req);', 'req->status = HW_OK;')
    # Record the first two actual POINT returns starting at the enabled edge.
    # One additional reload after ABORT issues the next real POINT.
    abort = (ROOT/'exec/exec_abort.inc').read_text()
    mark = '    return !!pending;'
    assert abort.count(mark) == 1
    abort = 'volatile u32 hw_points[9];\n' + abort.replace(mark, '''
    u32 n = hw_points[0];
    if (n < 8 && ((n & 1) || pending)) {
        hw_points[1 + n] = !!pending;
        hw_points[0] = n + 1;
    }
''' + mark)
    if mode == 'sticky':
        anchor = 'u32 pending = g_hwb_abort_pending;\n    g_hwb_abort_pending = 0;'
        assert abort.count(anchor) == 1
        abort = abort.replace(anchor, 'u32 pending = g_hwb_abort_pending;')
    inc = work/(mode+'.inc')
    inc.write_text(abort)
    observation = mutant_settings(work, 'observe-'+mode, 'exec/exec.c',
                                 '#include "exec_abort.inc"', f'#include "{inc}"')
    settings.extend(arg.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for arg in observation)
    signature = 'u32 pm_hwb_cfg_read(u32 bdf, u32 offset, u32 width, u32 *value)'
    reads = mutant_settings(work, 'observe-pci-'+mode, 'platform/powermac/pci_backend.c',
        signature + '\n{ return pm_pci_access(&root, bdf << 8, offset, width, 0, value); }',
        'volatile u32 hw_pci_reads;\n' + signature + """
{
    u32 rc = pm_pci_access(&root, bdf << 8, offset, width, 0, value);
    if (!rc && offset == 0 && width == 4 && *value == 0x003f106b) hw_pci_reads++;
    return rc;
}""")
    settings.extend(arg.replace('KB_MUT_', 'KB_MUT_THIRD_', 1) for arg in reads)
    return build_media(work/mode, '-j4', 'IMAGE=kboot-link-sh', 'FEATURE_KDEBUG=1',
                       'KB_SH_OUT='+str(work/'user'), 'ROOT_MANIFEST='+str(manifest_path),
                       'PPC_SYSTEM_CFG='+str(cfg), *settings)


def ohci(s, mode):
    from usb_run import rsh_command
    body = rsh_command(s, 'hw run /hw/ohci.hwb').decode()
    assert 'op=end rc=OK' in body
    assert rsh_command(s, 'echo $?').strip() == b'0'
    serial = (s.out/'serial-a.log').read_text(errors='replace')
    values = [int(x, 16) for x in re.findall(r'tag="CFG_VALUE" val=(0x[0-9A-F]+)', body)]
    offsets = [int(x, 16) for x in re.findall(r'tag="CFG_OFFSET" val=(0x[0-9A-F]+)', body)]
    assert offsets == [0, 8, 16] and len(values) == 3
    assert values[0] == 0x003f106b and values[1] >> 8 == 0x0c0310 and values[2] & ~15
    addr = int(re.search(r'op=mmioread32[^\n]*?addr=(0x[0-9A-F]+)', body)[1], 16)
    dev = next((int(b, 16), int(d, 16), int(f)) for b, d, f, pa in
               re.findall(r'USB OHCI BAR0 bdf=([0-9a-f]+):([0-9a-f]+)\.(\d+) pa=([0-9a-f]+)', serial)
               if int(pa, 16) == addr)
    assert set(re.findall(r'op=pciread32[^\n]*?bdf=(\d+:\d+\.\d+)', body)) == {'%d:%d.%d' % dev}
    rev = int(re.search(r'ohci %d:%d\.%d ports=\d+ rev=([0-9a-f]+)' % dev, serial)[1], 16)
    read = int(re.search(r'tag="HC_REVISION" val=(0x[0-9A-F]+)', body)[1], 16)
    assert (read & 255 == rev) == (mode != 'native-read')


def observe(media, directory, mode, sticky_op=None):
    from link_sh_run import Session
    from usb_run import rsh_command
    s = Session(media, '7450', directory, 300)
    try:
        s.start()
        s.wait(s.ready, 'initial resident shell prompt', 60)
        assert s.word('g_hwb_abort_pending') == 0
        if mode in ('normal', 'native-read'):
            ohci(s, mode)
        if mode in ('normal', 'port-ok'):
            for bits in (8, 16, 32):
                for op in ('read', 'write'):
                    body = rsh_command(s, f'hw run /hw/{op}{bits}.hwb')
                    expected = b'OK' if mode == 'port-ok' else b'NOSYS'
                    assert b'op=end rc='+expected in body
                    assert rsh_command(s, 'echo $?').strip() == (b'0' if mode == 'port-ok' else b'3')
        if mode in ('normal', 'sticky'):
            s.close_rshell()
            runs = set()
            cases = [(op, via) for via in ('lan', 'usb') for op in ('wait', 'poll')]
            if mode == 'sticky':
                cases = [(sticky_op, 'lan')]
            for index, (op, via) in enumerate(cases):
                before = 2 * index
                assert s.word('hw_points') == before
                s.keys(f'hw run /hw/{op}.hwb\n')

                def started():
                    if s.word('hwb_active'):
                        found = re.findall(rf'HW run=(\d+) seq=[^\n]* op={op}\b',
                                           s.screen('running'))
                        runs.update(found)
                    return len(runs) > index

                s.wait(started, 'active '+op)
                ident = max(runs, key=int)
                if op == 'poll':
                    reads = s.word('hw_pci_reads')
                    s.wait(lambda: s.word('hw_pci_reads') != reads,
                           'completed OHCI configuration read during poll')
                s.key('e')
                if via == 'lan':
                    from host_rshell import PPC_DIAG
                    s.agent.notifier.notify_number(PPC_DIAG, 3, b'\x03')
                else:
                    s.q.call('send-key', {'keys': [{'type':'qcode','data':'ctrl'},
                                                   {'type':'qcode','data':'pause'}]})
                end = f'HW run={ident} op=end rc=ABORT'
                def returned():
                    screen = s.screen('stop')
                    return (not s.word('hwb_active') and end in screen and
                            re.search(r'(?:OS32:[^\n]*\$|sh>)', screen.split(end, 1)[1]))
                s.wait(returned, op+' abort and prompt')
                label = f'retained-{via}-{op}'
                s.keys(f'cho {label}\n')
                s.wait(lambda: any(row.strip(' \x00') == label for row in
                                   s.screen('retained').splitlines()), 'queued ordinary input')
                s.keys(f'source /hw/{via}{op}.sh\n')
                s.wait(lambda: s.word('hw_points') == before + 2 and not s.word('hwb_active') and
                       any(row.strip(' \x00') == f'loaded-{via}-{op} 0' for row in
                           s.screen('reload').splitlines()), 'successful reload and second POINT')
                assert s.word('hw_points', 4 * (before + 1)) == 1
                assert s.word('hw_points', 4 * (before + 2)) == int(mode == 'sticky')
        else:
            s.wait(s.ready, 'next shell prompt')
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='hw-commands-')).resolve()
    hashes = set()
    for mode in ['normal'] + (['native-read', 'sticky', 'port-ok'] if args.mutate else []):
        media = build(work, mode)
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert digest not in hashes, mode
        hashes.add(digest)
        print(mode, digest, flush=True)
        if not args.build_only:
            if mode == 'sticky':
                for op in ('wait', 'poll'):
                    observe(media, work/(mode+'-'+op+'-guest'), mode, op)
            else:
                observe(media, work/(mode+'-guest'), mode)
                if mode == 'normal':
                    from link_sh_run import accept_reset
                    accept_reset(media, work/'normal-reset', 180, 'cuda')
    print(f'{len(hashes)} distinct HW command images')


if __name__ == '__main__':
    main()
