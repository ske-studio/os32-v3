"""PowerMac HW_OP §5 stages 4/5a/5b. mac99 OHCI, CUI stop and port results."""
import argparse
import hashlib
import json
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
    for op in ('wait', 'poll'):
        reload_script = work/f'{op}.sh'
        reload_script.write_text(f'hw load /hw/{op}.hwb\necho loaded-{op} $?\n')
        manifest[f'/hw/{op}.sh'] = str(reload_script)
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
            'req->width, 1, 0, 1, 0,', 'req->width, 0, 0, 1, 0,')
    if mode == 'port-ok':
        settings = mutant_settings(work, mode, 'kdebug/hwb_access.c',
            'req->status = platform_hwb_io(req);', 'req->status = HW_OK;')
    # Record the first two actual POINT returns starting at the enabled edge.
    # One additional reload after ABORT issues the next real POINT.
    abort = (ROOT/'exec/exec_abort.inc').read_text()
    mark = '    return !!pending;'
    assert abort.count(mark) == 1
    abort = 'volatile u32 hw_points[3];\n' + abort.replace(mark, '''
    if (pending || hw_points[0]) {
        if (hw_points[0] < 2) {
            hw_points[1 + hw_points[0]] = !!pending;
            ++hw_points[0];
        }
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


def observe(media, directory, mode):
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
            for op in ('wait', 'poll'):
                s.keys(f'hw run /hw/{op}.hwb\n')
                s.wait(lambda: s.word('hwb_active') and f'op={op}' in s.screen('running'),
                       'active '+op)
                s.key('e')
                s.q.call('send-key', {'keys': [{'type':'qcode','data':'ctrl'},
                                               {'type':'qcode','data':'pause'}]})
                s.wait(lambda: not s.word('hwb_active') and 'op=end rc=ABORT' in s.screen('stop'),
                       op+' abort result')
                s.keys(f'cho retained-{op}\n')
                s.wait(lambda: any(row.strip(' \x00') == 'retained-'+op for row in
                                   s.screen('retained').splitlines()), 'queued ordinary input')
                s.keys(f'source /hw/{op}.sh\n')
                s.wait(lambda: s.word('hw_points') == 2 and not s.word('hwb_active') and
                       any(row.strip(' \x00') == f'loaded-{op} 0' for row in
                           s.screen('reload').splitlines()), 'successful reload and second POINT')
                assert s.word('hw_points', 4) == 1
                assert s.word('hw_points', 8) == int(mode == 'sticky')
                if mode == 'sticky':
                    break
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
            observe(media, work/(mode+'-guest'), mode)
    print(f'{len(hashes)} distinct HW command images')


if __name__ == '__main__':
    main()
