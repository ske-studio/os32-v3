"""PowerMac HW_OP §5 6a. Real mac99 DSI through rfi, ASM and window cleanup."""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile
from kboot_accept import ROOT, build_media


def substitute(text, anchor, replacement):
    assert text.count(anchor) == 1, anchor
    return text.replace(anchor, replacement)


def build(out, mutant):
    boot = 'platform/powermac/ppc_kernel.c'
    window = 'platform/powermac/hwb_window.c'
    trap = 'arch/ppc/kernel_trap.c'
    diag = (ROOT / 'tools/ppc/hwb_window_guest.c').read_text()
    source = (ROOT / boot).read_text()
    source = substitute(source, 'void platform_boot_run(void)', diag + '\nvoid platform_boot_run(void)')
    source = substitute(source, '        pm_exec_boot();', '        hwb_window_report();\n        pm_exec_boot();')
    (out / 'boot.c').write_text(source)
    source = (ROOT / window).read_text()
    point = '    rc = HW_IO_ERROR;'
    injection = '''
    extern volatile u32 hwb_inject;
    if (hwb_inject) {
        hwb_inject = 0;
        if (paging_pte_restore(PPC_HWB_WINDOW_VA + PAGE_SIZE, 0)) arch_cpu_stop();
    }
'''
    source = substitute(source, point, injection + point)
    if mutant:
        # Lose the transfer error after the guard has consumed the actual DSI.
        source = substitute(source, '!t->run(va, operand, count, increment)',
                                    't->run(va, operand, count, increment) >= 0')
    (out / 'window.c').write_text(source)
    source = (ROOT / trap).read_text()
    point = '            f->srr0 = probe.resume;'
    source = substitute(source, point, '''            if (probe.data == PROBE_TRANSFER) {
                extern volatile u32 hwb_observed[];
                hwb_observed[9] = f->srr0;
                hwb_observed[10] = f->dar;
                hwb_observed[11] = probe.resume;
            }
''' + point)
    (out / 'trap.c').write_text(source)
    return build_media(out / 'kernel', '-j6', 'IMAGE=kboot-link-sh',
                       'KB_MUT_SRC=' + boot, 'KB_MUT_FILE=' + str(out/'boot.c'),
                       'KB_MUT_EXTRA_SRC=' + window, 'KB_MUT_EXTRA_FILE=' + str(out/'window.c'),
                       'KB_MUT_THIRD_SRC=' + trap, 'KB_MUT_THIRD_FILE=' + str(out/'trap.c'))


def observe(media, out, mutant):
    from link_sh_run import Session
    from usb_run import rsh_command
    s = Session(media, '7450', out, 240)
    try:
        s.start()
        s.wait(s.ready, 'shell prompt after DSI cleanup', 60)
        expected = [0 if mutant else 4, 0, 0, 0, 17, 91, 91, 0, 17,
                    s.symbols['ppc_hwb_write32_insn'], 0xfe401000,
                    s.symbols['ppc_hwb_write32_resume']]
        actual = [s.word('hwb_observed', i*4) for i in range(len(expected))]
        assert actual == expected, (actual, expected)
        assert rsh_command(s, 'echo hwb-ready').strip() == b'hwb-ready'
        s.wait(s.ready, 'next shell prompt')
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    os.environ.setdefault('SOURCE_DATE_EPOCH','946684800')
    work = Path(tempfile.mkdtemp(prefix='ppc-hwb-')).resolve()
    hashes = set()
    for mutant in range(1 + args.mutate):
        out = work / ('lost-error' if mutant else 'normal')
        out.mkdir()
        media = build(out, mutant)
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        print(out.name, digest, flush=True)
        if not args.build_only:
            observe(media, out / 'guest', mutant)
    print(f'{len(hashes)} distinct HW_OP DSI images')


if __name__ == '__main__':
    main()
