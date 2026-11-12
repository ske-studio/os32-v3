#!/usr/bin/env python3
"""USB 1C cooperative core, simulated device/HC contract and runtime mutants."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import host32
ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['usb_xfer', 'usb_req', 'usb_service', 'usb_cfgparse', 'usb_enum',
           'usb_explore', 'usb_hub', 'usb_detach', 'usb_classes']
CHECK = dict(name='usb-core', tier=1, needs=[], budget=60,
             covers=['drivers/usb/usb_*.[ch]', 'drivers/usb/ohci_regs.h',
                     'include/usb_platform.h', 'include/endian_dma.h', 'include/endian_le.h',
                     'include/types.h', 'include/callconv.h', 'arch/*/arch_endian_dma.h',
                     'lib/kstring.h', 'tools/tests/usb_fake_dev.c', 'tools/tests/test_usb_core.py',
                     'tools/tests/host32.py', 'tools/tests/cc_cache.py'])
FLAGS = ['-std=gnu11', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
         '-Wvla', '-Werror=implicit-function-declaration', '-O2', '-m32',
         '-Iinclude', '-I.', '-Iarch/x86', '-nostdlib', '-fno-pie', '-no-pie']
def command(args, **kw):
    return subprocess.run(args, cwd=ROOT, check=True, **kw)
def build(tmp, swap=False, mutation=None):
    paths=[]
    for name in SOURCES:
        src=ROOT / 'drivers/usb' / (name+'.c')
        if mutation and mutation[0]==name:
            text=src.read_text(); old,new=mutation[1:]
            assert text.count(old)==1, (name,old,text.count(old))
            src=tmp/(name+'.c'); src.write_text(text.replace(old,new))
        paths.append(str(src))
    exe=tmp/'core'
    command(['gcc', *FLAGS, *(['-DUSB_TEST_SWAP'] if swap else []), *paths,
             'tools/tests/usb_fake_dev.c', '-o', str(exe)])
    return exe
def execute(exe, cases=range(1,32), red=False):
    failures=[]
    for case in cases:
        p=host32.run([str(exe),str(case)],capture_output=True,timeout=20)
        if p.returncode:
            failures.append(case)
            if not red: print(p.stdout,p.stderr)
    if not red: assert not failures, failures
    return failures
MUTATIONS = [
 ('usb_req','if (usb_busy) return -USB_E_CTX;', 'if (0) return -USB_E_CTX;'),
 ('usb_cfgparse','if (!valid) { d->ifs[current].num = 255; current = -1; }', 'if (!valid && 0) { d->ifs[current].num = 255; current = -1; }'),

 ('usb_explore','if (!usb_port_op(w, USB_RH_STATUS)) return; /* fresh after free */', '(void)w;'),
 ('usb_explore','if (!usb_port_op(w, USB_RH_STATUS)) return; /* fresh after reset */', '(void)w;'),

 ('usb_enum','d->addr = e->old_addr; e->deadline_ms = usbp_ms() + 10; w->phase = 1;', 'if (w->error) { e->step = EX_ERR; return 0; } d->addr = e->old_addr; e->deadline_ms = usbp_ms() + 10; w->phase = 1;'),
 ('usb_explore','if (!rc) return;', 'if (!rc) { next_port(w, 0); return; }'),
 ('usb_enum','e->step = EX_RE1; w->reset_phase = 0; /* A2 recovery */', 'e->step = EX_ERR; w->reset_phase = 0;'),
 ('usb_enum','e->deadline_ms = usbp_ms() + 500;', 'e->deadline_ms = usbp_ms();'),
 ('usb_explore','if (e->step == EX_RE1 && !e->re_retry++)', 'if (0)'),
 ('usb_explore','!(status & (1u << USB_PORT_ENABLE)) && *restart(w)', '(status & (1u << USB_PORT_ENABLE)) && *restart(w)'),
 ('usb_detach','d->state = DEV_DETACHED; c->step = FD_D1; return 0;', 'd->state = DEV_DETACHED; c->step = FD_D2; return 0;'),
 ('usb_enum','USB_DESC_CONFIG << 8, 0, w->cfg_length, e->cfgbuf)', 'USB_DESC_CONFIG << 8, 0, 9, e->cfgbuf)'),
 ('usb_explore','switch (e->step) {', 'if (hc->needs_explore) { e->step = EX_X0; } switch (e->step) {'),
 ('usb_xfer','while (*p && hc->state == HC_RUNNING) {', 'while (*p) {'),

 ('usb_enum','e->deadline_ms = usbp_ms() + 10;', 'e->deadline_ms = usbp_ms();'),
 ('usb_explore','e->step = e->child ? EX_A1 : EX_ERR;', 'e->step = e->child ? EX_A2 : EX_ERR;'),
 ('usb_explore','if (!usb_port_op(w, USB_RH_CLEAR_ENABLE)) return;', 'if (0) return;'),
 ('usb_enum','if (w->error) cfg_failed(w);', 'if (w->error) e->step = EX_ERR;'),
 ('usb_enum','le16_wr((u8 *)e->cfgbuf->va + 2, w->cfg_length);', '(void)w->cfg_length;'),
 ('usb_enum','if (rc == -USB_E_BUSY) return 0;', 'if (rc == -USB_E_BUSY) { w->iface++; return 0; }'),
 ('usb_enum','if (rc < 0) { w->attach_failed = 1; return 0; }', 'if (rc < 0) { e->step = EX_ERR; return 0; }'),
 ('usb_xfer','return x->unsetup == UNSETUP_DONE;', 'return usb_xfer_drained(x);'),
 ('usb_xfer','x->curr_set ^= 1;', 'x->curr_set ^= 0;'),
 ('usb_xfer','x->did_dma_delay = 0;', 'x->did_dma_delay = 1;'),
 ('usb_xfer','x->did_dma_delay = 1; x->state = XS_DMA_DELAY;', 'x->did_dma_delay = 1; x->state = XS_CB_PENDING;'),
 ('usb_service','if (hc->state != HC_RUNNING) continue; /* delay fault */', 'if (0) continue;'),
 ('usb_req','if (x->dev->hc->state == HC_DEAD) return -USB_E_HC;', 'if (x->dev->hc->state == HC_DEAD) return 0;'),
 ('usb_hub','usb_free_device_begin(s->free_child);', 'return 1;'),
]
def main(mutate):
    start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='usb-core-') as d:
        tmp=Path(d)
        for swap in (False,True):
            execute(build(tmp,swap)); print('GREEN runtime', 'SWAP' if swap else 'LE')
        if mutate:
            for mutation in MUTATIONS:
                failures=execute(build(tmp,mutation=mutation),red=True)
                assert failures, ('SURVIVED',mutation)
                print('RED runtime',mutation[0],mutation[1],failures)
    print('usb-core rc=0 seconds=%.2f' % (time.monotonic()-start))
def run(ctx):
    return subprocess.call([sys.executable,'-B',str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,
                           env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)),close_fds=False)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mutate',action='store_true')
    main(p.parse_args().mutate)
