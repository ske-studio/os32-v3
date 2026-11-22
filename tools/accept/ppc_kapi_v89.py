#!/usr/bin/env python3
"""KAPI-V89-PPC — KAPI_CAPACITY_V89_DESIGN §1, §2 and §4 items 4–5, boot mapping and USER ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build_media
from link_sh_run import Session
from ppc_shm import guest_available
from run import require
from usb_run import rsh_command


ABI = json.loads((ROOT / 'sdk/kapi.json').read_text())
FIRST_RESERVED = len(ABI['api'])
LAST_RESERVED = ABI['func_capacity'] - 1


def replace(source, anchor, value):
    if source.count(anchor) != 1:
        raise ValueError(f'expected one anchor: {anchor!r}')
    return source.replace(anchor, value)


def fixture(folder, case, gate):
    """Build private translation units with the existing kboot substitution rules."""
    sources = {}

    def edit(path, anchor, value):
        source = sources.get(path)
        if source is None:
            source = (ROOT / path).read_text()
        sources[path] = replace(source, anchor, value)

    boot = 'platform/powermac/ppc_kernel.c'
    stop = '    if (!shared_user) kboot_stop("KAPI trampoline");'
    base = '(u32)user_gate' if gate else 'exec_tramp_page_addr()'
    edit(boot, stop, stop + '''
    u32 v89_base = BASE;
    for (int offset = -1; offset <= 2; ++offset) {
        u32 va = v89_base + offset * PAGE_SIZE;
        u32 pte = paging_pte_raw(va);
        kprintf(7, "V89 page=%d user=%u rw=%u identity=%u\\r\\n", offset,
                !!(pte & PTE_PRESENT && pte & PTE_USER), !!(pte & PTE_RW),
                (pte & ~(PAGE_SIZE - 1u)) == va);
    }'''.replace('BASE', base))
    if case == 'map-failure':
        edit(boot, stop, r'''    if (!shared_user) {
        extern u32 exec_entry_calls;
        u32 user_pages = 0;
        for (u32 i = 0; i < 2; ++i)
            user_pages += !!(paging_pte_raw(exec_tramp_page_addr() + i * PAGE_SIZE) & PTE_USER);
        for (u32 off = 0; off < MEM_SHM_SIZE; off += PAGE_SIZE)
            user_pages += !!(paging_pte_raw(MEM_SHM_BASE + off) & PTE_USER);
        kprintf(7, "V89 user-pages=%u\r\n", user_pages);
        kprintf(7, "V89 entries=%u\r\n", exec_entry_calls);
        kboot_stop("KAPI trampoline");
    }''')
    if case == 'gate-count':
        # Back both candidate pages with fixture storage to keep the neighbour private.
        edit(boot, 'user_gate[PAGE_SIZE]', 'user_gate[2 * PAGE_SIZE]')
        edit(boot, '(u32)user_gate, 1)', '(u32)user_gate, 2)')
    if case in ('missing-page', 'map-failure'):
        path = 'mm/paging_map.inc'
        if case == 'map-failure':
            anchor = 'prepare_tables(MEM_SHM_BASE >> PAGE_SHIFT, MEM_SHM_SIZE / PAGE_SIZE)'
            # The right operand of || runs only after trampoline preparation succeeds.
            edit(path, anchor, r'(kprintf(7, "V89 inject SHM prepare failure\r\n"), -1)')
            edit('mm/paging.c', '#include "paging.h"', '#include "paging.h"\n#include "kprintf.h"')
        else:
            anchor = '        set_page_noflush(frame, frame, PAGE_RO | PTE_USER);'
            edit(path, anchor, '        if (i == 1) continue;\n' + anchor)
        edit('mm/paging.c', '#include "paging_map.inc"', '#include "v89-map.inc"')
        (folder / 'v89-map.inc').write_text(sources.pop(path))
    if not gate:
        launch = '        arch_entry_enter(&ctx->entry, want_ring3 ? ctx->as->pd_phys : 0);'
        edit('exec/exec_launch.inc', launch, r'''        kprintf(7, "V89 enter\r\n");
        if (want_ring3) {
            u32 reads = 0, writes = 0, identity = 0;
            unsigned flags = irq_save();
            for (int i = 0; i < 4; ++i) {
                u32 va = ring3_tramp_page + (i - 1) * PAGE_SIZE;
                u32 pa;
                if (!as_va_to_pa_read(ctx->as->pd_phys, va, &pa)) {
                    reads |= 1u << i;
                    if (pa == va) identity |= 1u << i;
                }
                if (!as_va_to_pa(ctx->as->pd_phys, va, &pa)) writes |= 1u << i;
            }
            irq_restore(flags);
            kprintf(7, "V89 child read=%u write=%u identity=%u\r\n", reads, writes, identity);
        }
''' + launch)
        bound = '    if (slot >= (u32)KAPI_FUNC_COUNT) {'
        check = bound
        if case == 'reject-last':
            check = bound.replace('KAPI_FUNC_COUNT)', 'KAPI_FUNC_COUNT - 1)')
        elif case == 'accept-first':
            check = bound.replace('>=', '>')
        edit('exec/exec_syscall.inc', '#define RING3_ARG_WINDOW',
             'static volatile u32 v89_boundary;\n#define RING3_ARG_WINDOW')
        edit('exec/exec_syscall.inc', bound, check + '\n        v89_boundary = slot;')
        # Keep reserved slots away from argument metadata even when the tested
        # comparison is relaxed. Only the product rejection emits the marker.
        anchor = '    wrapptr = exec_kapi_table()'
        edit('exec/exec_syscall.inc', anchor, r"""    if (slot >= KAPI_FUNC_COUNT) {
        v89_boundary = ~slot;
        result = OS32_ERR_INVAL;
        goto syscall_complete;
    }
""" + anchor)
        done = '    arch_syscall_result(&call, result);'
        edit('exec/exec_syscall.inc', done, done + '''
    static int v89_follow;
    if (slot == 0 || v89_follow)
        kprintf(7, "V89 slot=%u result=%d heap=%u shm=%u\\r\\n",
                slot, (int)result, kapi->sbrk_heap_limit, kapi->shm_base);
    v89_follow = slot == 0;''')
        if case == 'wrong-data':
            edit('exec/exec_launch.inc', '[KAPI_DATA_IDX_SHM_BASE] = kapi->shm_base;',
                 '[KAPI_DATA_IDX_SHM_BASE] = is_shell ? kapi->shm_base : sbrk_end;')
        for path in tuple(sources):
            if path.endswith('.inc'):
                name = Path(path).name
                edit('exec/exec.c', f'#include "{name}"', f'#include "v89-{name}"')
                (folder / ('v89-' + name)).write_text(sources.pop(path))
    if case == 'wrong-stub':
        edit('arch/ppc/syscall_code.c', '0x38000000u | i)', '0x38000000u | (i == 297 ? 7 : i))')
    if case == 'wrong-branch':
        edit('arch/ppc/syscall_code.c', '(l.ls_off + 8u) -',
             '(l.ls_off + (i == 297 ? 12u : 8u)) -')
    settings = []
    for key, (path, source) in zip(('KB_MUT', 'KB_MUT_EXTRA', 'KB_MUT_THIRD'), sources.items(), strict=False):
        target = folder / Path(path).name
        target.write_text(source)
        settings += [f'{key}_SRC={path}', f'{key}_FILE={target}']
    assert len(settings) == len(sources) * 2
    return settings


class Mismatch(Exception):
    pass


def expect(condition, observation):
    if not condition:
        raise Mismatch(observation)


EXPECTED_RED = {
    'missing-page': 'page 1',
    'gate-count': 'page 1',
    'wrong-stub': 'stub immediate',
    'wrong-branch': 'stub branch',
    'wrong-data': 'published data',
    'reject-last': f'slot {FIRST_RESERVED - 1} rejected',
    'accept-first': f'slot {FIRST_RESERVED} rejection',
}


def observe(media, folder, case, cpu):
    session = Session(media, cpu, folder, 150)
    log = folder / 'serial-a.log'

    def serial():
        return log.read_bytes().replace(b'\r', b'') if log.exists() else b''

    verdict = None
    try:
        session.start()
        if case == 'map-failure':
            def stopped():
                regs = session.q.call('human-monitor-command', {'command-line': 'info registers'})
                pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                addr = session.symbols['arch_cpu_stop']
                end = min(v for v in session.symbols.values() if v > addr)
                return pc and addr <= int(pc[1], 16) < end
            session.wait(stopped, 'KAPI boot failure stop', 60)
            lines = serial().splitlines()
            marker = b'V89 inject SHM prepare failure'
            fatal = b'FATAL kboot KAPI trampoline'
            require(lines.count(marker) == 1 and lines.count(fatal) == 1 and
                    lines.index(marker) < lines.index(fatal), 'SHM failure injection before stop')
            require(lines.count(b'V89 user-pages=0') == 1, 'USER PTE after prepare failure')
            require(lines.count(b'V89 entries=0') == 1 and b'V89 enter' not in lines,
                    'user entry after mapping failure')
        else:
            session.wait(lambda: b'V89 page=2 ' in serial() and
                         re.search(rb'V89 page=2 .*?\n', serial()), 'KAPI page observations', 60)
            pages = re.findall(rb'^V89 page=(-?\d+) user=(\d) rw=(\d) identity=(\d)$', serial(), re.M)
            count = 1 if case in ('gate', 'gate-count') else 2
            require(len(pages) == 4, 'four surrounding PTEs')
            for offset, row in zip(range(-1, 3), pages):
                index, user, writable, identity = map(int, row)
                public = 0 <= offset < count
                require(index == offset, 'page observation order')
                expect(user == public and (not public or (identity and not writable)),
                       f'page {offset}')
            if count == 2:
                session.wait(session.ready, 'CUI shell ready', 60)
                start = len(serial())
                body = rsh_command(session, '/bin/v89probe.bin')
                trace = serial()[start:]
                require(b'V89 child read=6 write=0 identity=6\n' in trace, 'child AS permissions or frame')
                for reason in ('stub immediate', 'stub branch', 'stub entry', 'common sc'):
                    expect(body.strip() != reason.encode(), reason)
                expect(session.word('v89_boundary') != FIRST_RESERVED - 1,
                       f'slot {FIRST_RESERVED - 1} rejected')
                result = re.fullmatch(rb'V89 return=(-?\d+) heap=(\d+) shm=(\d+)\n+', body)
                require(result and rsh_command(session, 'echo $?') == b'0\n', repr(body))
                pair = re.search(rb'V89 slot=0 [^\n]+\nV89 slot=297 result=(-?\d+) heap=(\d+) shm=(\d+)\n', trace)
                require(pair, 'gfx_init and io_pump dispatch results')
                value, heap, shm = map(int, pair.groups())
                require(value == -9 and heap and shm and heap != shm, repr(pair.groups()))
                expect(tuple(map(int, result.groups())) == (value, heap, shm), 'published data')
                for argument, slot in (('a', FIRST_RESERVED), ('b', LAST_RESERVED)):
                    body = rsh_command(session, '/bin/v89probe.bin ' + argument)
                    boundary = session.word('v89_boundary')
                    rejected = boundary == slot
                    past_bound = boundary == (slot ^ 0xffffffff)
                    require(rejected or past_bound,
                            f'missing dispatch evidence for slot {slot}')
                    expect(rejected and not past_bound,
                           f'slot {slot} rejection')
                    require(b'[Process crashed]' in body.splitlines(), repr(body))
                    rsh_command(session, 'echo $?', b'139\n')
                    follow = rsh_command(session, '/bin/v89probe.bin')
                    require(re.fullmatch(rb'V89 return=-9 heap=\d+ shm=\d+\n+', follow), repr(follow))
                    rsh_command(session, 'echo $?', b'0\n')
    except Mismatch as exc:
        if exc.args != (EXPECTED_RED.get(case),):
            raise
        verdict = f'RED {exc}'
    else:
        require(case not in EXPECTED_RED, 'mutation survived: ' + case)
        verdict = 'GREEN'
    finally:
        try:
            if verdict:
                session.q.call('quit')
                require(session.p.wait(timeout=5) == 0, 'QEMU exit')
        finally:
            session.close()
    print(verdict, case, cpu, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not guest_available():
        print('SKIP: PPC cross tools, mac99 QEMU or loopback unavailable')
        return 3
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='v89-ppc-')).resolve()
    user = work / 'user'
    bins = ' '.join(str(user / (name + '.bin')) for name in ('hello', 'fault', 'v89probe'))
    cases = ['exec', 'gate', 'map-failure']
    if args.mutate:
        cases += list(EXPECTED_RED)
    seen = set()
    for case in cases:
        folder = work / case
        folder.mkdir()
        gate = case in ('gate', 'gate-count')
        options = ['IMAGE=kboot-core'] if gate else [
            'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user), 'KB_ROOT_BINS=' + bins, 'kb-user']
        media = build_media(folder / 'kernel', *fixture(folder, case, gate), *options, 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        require(digest not in seen, 'duplicate image ' + case)
        seen.add(digest)
        print(f'{case} sha256={digest} {media.elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, folder / cpu, case, cpu)
    print(f'{len(seen)} distinct KAPI v89 images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
