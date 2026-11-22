#!/usr/bin/env python3
"""Check USER fd1 routing against the independent LAN terminal and log."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
from kboot_accept import build, mutant_settings
from kboot_run import require, serial, validate_root
from run import CriterionFailure

CHECK = dict(name='ppc-sh-output', tier=2, budget=900, needs=[],
             covers=['tools/ppc/ppc_sh_output.py', 'userland/ppc/outprobe.c'],
             inputs=['build/targets/ppc-powermac.mk', 'build/ppc-user.mk',
                     'platform/powermac/report_product.c', 'platform/powermac/kboot_console.c', 'platform/powermac/console_stream.c',
                     'fs/vfs_fd.c', 'tools/ppc/kboot_accept.py', 'tools/ppc/kboot_run.py',
                     'tools/ppc/sc_run.py', 'tools/ppc_lan_host.py',
                     'tools/ppc/link_sh_run.py', 'tools/ppc/link_run.py',
                     'tools/host_agent.py',
                     'tools/host_operator_socket.py', 'tools/host_rshell.py',
                     'tools/ppc/usb_run.py', 'userland/ppc/hello.c'])

def rsh_command(session, line):
    """Use only the product operator result, within one agent generation."""
    op = session.agent.operator
    generation = (session.agent.host_generation, session.agent.sess, session.agent.epoch)
    start = len(session.events)
    ident = op.submit('cui', line, 'accept', timeout_ms=20000)
    session.wait(lambda: ident in op.results or (op.pending is not None and
                 op.pending['state'] in ('done', 'truncated')), 'RSH result '+line, 25)
    result = op.results.get(ident) or op.take_result()
    require(generation == (session.agent.host_generation, session.agent.sess,
                           session.agent.epoch), 'RSH generation changed')
    events = [e for e in session.events[start:]
              if e['parts'][:2] == ['RSH', 'OUT'] and e['parts'][3] == ident]
    require(len(events) == 1, 'one RSH OUT')
    body = bytes.fromhex(events[0]['body'])
    require(result['state'] == 'done' and result['drop'] == 0 and
            result['result'] == body, 'complete RSH OUT')
    prefix = b'> '+line.encode()+b'\n'
    require(body.startswith(prefix) and body.endswith(b'\x04'), 'RSH framing')
    return body[len(prefix):-1]


def link_accept(elf, cpu, out, mutant=False, pressure=False):
    import link_sh_run as link

    class OutputSession(link.Session):
        def new_agent(self):
            super().new_agent()
            self.logs = []
            self.retries = 0
            if pressure:
                def busy(rid, entry, parts, out):
                    self.retries += 1
                    self.agent._answer(rid, entry, 503, b'', None, out)
                self.agent._service_log = busy
            # Install the in-process LOG observer before QEMU starts. Retain the
            # original logger and operator publisher, including raw payloads.
            publish = self.operator.publish_log
            def observed(record):
                self.logs.append(dict(record))
                publish(record)
            self.operator.publish_log = observed

    s = OutputSession(elf, cpu, out, 120)
    try:
        s.start(); s.wait(s.ready, 'RSH READY', 60)
        require('Loading /sys/shell.bin' not in s.screen('ready'), 'shell loading on framebuffer at READY')
        if pressure:
            s.wait(lambda: s.retries >= 2, 'foreground LOG retries before command')
        actual = rsh_command(s, '/bin/outprobe.bin')
        # sh prints one '\n' after an external program exits with success
        # (userland/shell/sh_exec.inc run_cmd_report), as on x86.
        expected = b'hello ok\nH\n'+(b'' if mutant else b'FD1 PROBE\n')+b'\n'
        require(actual == expected, 'fd1 OUT routing: '+repr(actual))
        alive = rsh_command(s, 'echo alive')
        if mutant:
            # The fd1-log mutant also routes the built-in echo to LOG.
            require(alive == b'', 'subsequent command (mutant): '+repr(alive))
            s.wait(lambda: b'alive\n' in b''.join(r['text'] for r in s.logs), 'alive LOG')
        else:
            require(alive == b'alive\n', 'subsequent command: '+repr(alive))
        if pressure:
            return
        s.wait(lambda: b'Loading /sys/shell.bin' in
               b''.join(r['text'] for r in s.logs), 'shell loading in LOG')
        if mutant:
            s.wait(lambda: b'FD1 PROBE\n' in b''.join(r['text'] for r in s.logs), 'probe LOG')
        s.settle(1)
        logs = b''.join(r['text'] for r in s.logs)
        require([r['seq'] for r in s.logs] == list(range(len(s.logs))), 'LOG sequence')
        require(len({r['boot'] for r in s.logs}) == 1 and
                all(r['drop_records'] == r['drop_bytes'] == 0 for r in s.logs), 'LOG loss/generation')
        require((b'FD1 PROBE\n' in logs) == mutant, 'fd1 LOG routing')
        escc = (out/'serial-a.log').read_bytes()
        require(b'Loading /sys/shell.bin' in escc, 'shell loading in ESCC')
        require('Loading /sys/shell.bin' not in s.screen('after-commands'), 'shell loading on framebuffer')
        for milestone in b'OF-CAPTURE OF-OFF MMU-D MEMORY TRAP MPIC CLOCK READY ALIVE'.split():
            marker = b'KBOOT ' + milestone + b' '
            require(marker not in escc and marker not in logs, 'product milestone '+milestone.decode())
        s.wait(lambda: validate_root(serial(out/'serial-a.log'), serial(out/'serial-b.log')),
               'rd0 registration and page accounting', 60)
        s.q.call('quit'); s.p.wait(timeout=5)
        require(s.p.returncode == 0, 'QEMU exit')
    finally:
        (out/'log.json').write_text(json.dumps([
            dict(r, text=r['text'].hex()) for r in s.logs], indent=2))
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--link', action='store_true')
    parser.add_argument('--serial', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='sh-output-', dir=os.environ['TMPDIR']))
    user = work/'user'
    bins = ' '.join(str(user/(name+'.bin')) for name in ('hello', 'outprobe'))
    hashes = set()
    for name in ('normal', 'fd1-log', 'ready-leak') if args.mutate else ('normal',):
        mutation = []
        if name == 'fd1-log':
            mutation = mutant_settings(work, name, 'platform/powermac/console_stream.c',
                                       '    console_write(buf, size, color);',
                                       '    (void)color; pm_log_write(buf, size);')
        if name == 'ready-leak':
            mutation = mutant_settings(work, name, 'platform/powermac/ppc_kernel.c',
                                       '    pm_report_ready(start);',
                                       '    pm_report_ready(start);\n'
                                       '    kprintf(KBOOT_ATTR, "KBOOT READY stage=pre-user\\r\\n");')
        elf = build(work/name, 'IMAGE=kboot-link-sh', 'KB_SH_OUT='+str(user),
                    'KB_ROOT_BINS='+bins, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        require(digest not in hashes, 'distinct '+name)
        hashes.add(digest)
        print(name, digest, flush=True)
        if not args.build_only:
            try:
                link_accept(elf, '7450', work/(name+'-guest'), name == 'fd1-log')
            except CriterionFailure as error:
                if name != 'ready-leak' or str(error) != 'product milestone READY':
                    raise
                print('RED runtime: product milestone READY')
            else:
                require(name != 'ready-leak', 'READY leak survived')
            if name == 'normal':
                link_accept(elf, '7450', work/'pressure-guest', pressure=True)
    print(f'{len(hashes)} distinct fd1 images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
