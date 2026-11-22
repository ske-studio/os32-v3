#!/usr/bin/env python3
"""PowerMac cold-boot USB input through the resident CUI and its foreground app."""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/ppc')]
from kboot_accept import build_media, mutant_settings
from ppc_shm import guest_available
from run import require
from usb_run import in2_observe


IN2_MUTATIONS = {
    'no-key': ('input/hal/usb_input.c', 'if (!input_capture_key(&key) && pause',
               'if (usage != 0x04 && !input_capture_key(&key) && pause'),
    'no-button': ('input/hal/usb_input.c', '    input_capture(&e);\n    return 1;', '    return 1;'),
    'no-local': ('platform/powermac/input_boot.c', 'return input_legacy_try(IL_LOCAL_CHAR);',
                 'return -1;'),
    'no-abort': ('input/hal/usb_input.c', 'ring3_abort_request();', '(void)0;'),
    'no-release': ('input/hal/in2_state.c', 'if (pointer_source_buttons(src)) input_capture(&e);',
                   '(void)src;'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross/'bin/powerpc-elf-gcc').is_file():
        print('SKIP: PowerPC toolchain unavailable')
        return 3
    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback unavailable')
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-in2-cui-')).resolve()
    print('evidence='+str(work), flush=True)

    user = work/'user'
    opts = ['IMAGE=kboot-link-sh', 'KBOOT_USB_KEY_TRACE=1', 'KB_SH_OUT='+str(user),
            'KB_ROOT_BINS='+str(user/'in2prob.bin')]
    # Count idle steps reached after an empty interruptible key read.
    anchor = '        p->wait_step();'
    observation = mutant_settings(work, 'wait-observer', 'kernel/input_wait.c', anchor,
        '        extern volatile unsigned in2_wait_steps;\n'
        '        if (!gui && mode == IL_KEY && interruptible) ++in2_wait_steps;\n' + anchor)
    with Path(observation[1].split('=', 1)[1]).open('a') as source:
        source.write('\nvolatile unsigned in2_wait_steps;\n')
    opts += [item.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for item in observation]
    hashes = set()
    for mode in ['normal'] + (list(IN2_MUTATIONS) if args.mutate else []):
        mutation = mutant_settings(work, mode, *IN2_MUTATIONS[mode]) if mode != 'normal' else []
        media = build_media(work/mode, *opts, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        require(digest not in hashes, 'identical ELF '+mode)
        hashes.add(digest)
        print(mode, digest, flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                in2_observe(media, cpu, work/(mode+'-'+cpu), mode)
    print('distinct in2 images='+str(len(hashes)), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
