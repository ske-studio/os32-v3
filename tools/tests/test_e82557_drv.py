#!/usr/bin/env python3
"""LB3 real driver injection. Record: tools/tests/e82557_drv_tdd.md"""
CHECK = dict(name='e82557-drv-host', tier=1,
    covers=['kernel/irq/tick.c', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'drivers/net/e82557/e82557.c', 'drivers/net/e82557/e82557_probe_reset_base.inc', 'drivers/net/e82557/e82557.h', 'drivers/net/e82557/e82557_regs.h', 'drivers/net/e82557/e82557_eeprom.c', 'drivers/net/e82557/e82557_eeprom.h', 'drivers/pci/pci_decode.c', 'drivers/pci/pci_decode.h', 'kernel/module/module.h', 'include/config.h', 'include/bootinfo.h', 'fs/vfs.h', 'tools/tests/e82557_drv_host.c', 'tools/tests/host32.py', 'drivers/net/e82557/e82557_desc.c', 'drivers/net/e82557/e82557_desc.h', 'drivers/pci/pci_bind_match.c', 'drivers/net/e82557/lan_module.c', 'mm/dma_pool.h', 'arch/x86/kernel/isr_handlers.c', 'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'include/pci_dev_contract.h', 'include/pci_bind_contract.h', 'platform/pc98/lan_match.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=5.33304)

from kernel_includes import include_flags
import re
import os
import pathlib
import subprocess
import sys
import tempfile
import host32
ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/e82557_drv_host.c"
TARGET_SRCS = ["drivers/net/e82557/e82557.c", "drivers/net/e82557/e82557_desc.c"]
CASES = ["pass", "kill", "mkdir", "same", "other", "seq_missing_old", "seq_ahead", "monotonic", "try_read", "seq_read", "try_write", "seq_write", "try_short", "seq_short", "try_back", "seq_back", "sync", "write_zero", "remove", "crc_missing", "bad_bar", "io_disabled", "reset_fail", "pre_wait", "post_wait", "ru_post_wait", "no_trace", "ee_fallback", "ee_badmac", "ee_detect_fail"] + [f"bad_try{i}" for i in range(9)] + [f"bad_seq{i}" for i in range(7)]
CASES += ["table", "dma_fail", "bm_fail", "bm_present", "stop_retry", "stop_fail", "cb_edge", "cb_late", "bm_present_ng"] + [f"{kind}{n}" for kind in ("cb_timeout", "cb_ng", "cu_pre", "cu_post") for n in range(1, 6)]
CASES += ["phy_" + n for n in ("latch", "late", "down", "off", "none", "timeout")]
CASES += ["nsc_" + n for n in ("rev", "id1", "id2", "same", "off", "reserved", "drop1", "drop5", "drop6", "timeout_pcr", "timeout_par", "timeout_read", "timeout_write")]
CASES += ["probe_irq_fail", "probe_ring_fail", "probe_ru_fail", "lb_timeout", "lb_corrupt", "swi_lost", "swi_irq0"]
CASES += ["rx_" + n for n in ("empty", "full", "p1", "ready", "idle", "copy_tick", "short", "long", "overrun", "bad")] + [f"rx_wrap{i}" for i in range(1,9)]
CASES += ["irq_" + n for n in ("none", "cx", "cna", "fr", "rnr", "swi", "mdi", "all", "dead", "again", "limit", "starting", "stopping", "recover", "failed")]
CASES += ["tx_" + n for n in ("done", "bad", "reserved", "pending", "generation", "timeout", "pre", "post")]
CASES += ["recover_" + n for n in ("full", "final", "stopfail", "r3fail")] + ["ru_timeout", "recovery_limit", "api_invalid"]
CASES += ["tx_send_done", "tx_send_bad", "tx_irq_cx", "tx_irq_cna", "tx_irq_bad"]
CASES += ["module_quiet", "module_stopfail", "module_bm_present"]
FLAGS = ["-m32", "-static", "-no-pie", "-fno-pie", "-fno-stack-protector", "-ffreestanding", "-nostdlib", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl="]
INCLUDES = include_flags(ROOT)
MIRROR = ["tools/tests/e82557_drv_host.c", "drivers/net/e82557/e82557.c", "drivers/net/e82557/e82557.h", "drivers/net/e82557/e82557_regs.h", "drivers/net/e82557/e82557_eeprom.c", "drivers/net/e82557/e82557_eeprom.h", "drivers/pci/pci_decode.c", "fs/vfs.h", "kernel/module/module.h", "include/bootinfo.h", "include/config.h"]
MIRROR += ["drivers/net/e82557/e82557_desc.c", "drivers/net/e82557/e82557_desc.h", "drivers/pci/pci_bind_match.c", "mm/dma_pool.h"]
MIRROR += ["include/pci_dev_contract.h", "include/pci_bind_contract.h",
           "include/lan_contract.h", "drivers/pci/pci_bind.h", "drivers/pci/pci.h",
           "drivers/pci/pci_decode.h", "include/types.h", "drivers/net/e82557/lan_module.h",
           "platform/pc98/lan_match.c"]
MIRROR += ["drivers/net/e82557/e82557_probe_reset_base.inc"]
MUTATIONS = [('drivers/net/e82557/e82557.c',
  'if (nic.tx == TX_ISSUED) tx_complete();',
  '/* send completion omitted */', 'send completion omitted'),
 ('drivers/net/e82557/e82557.c',
  'if (st & (E82557_STAT_CX | E82557_STAT_CNA)) tx_complete();',
  '/* IRQ completion omitted */', 'IRQ completion omitted'),
 ('drivers/net/e82557/e82557.c',
  'if (rc == VFS_ERR_NOTFOUND) return TRY_NONE;',
  'if (rc < 0) return TRY_NONE;',
  'read IO treated as missing'),
 ('drivers/net/e82557/e82557.c',
  'if (decision == TRY_SAME_CRC) {',
  'if (0 && decision == TRY_SAME_CRC) {',
  'same CRC retried'),
 ('drivers/net/e82557/e82557.c', '    seq++;', '    seq = 1;', 'persistent sequence reset'),
 ('drivers/net/e82557/e82557.c',
  'if (seq < oldseq) seq = oldseq;',
  'if (0 && seq < oldseq) seq = oldseq;',
  'old mark sequence ignored'),
 ('drivers/net/e82557/e82557.c',
  'if (seq == E82557_SEQ_MAX) return "seq-overflow";',
  'if (0 && seq == E82557_SEQ_MAX) return "seq-overflow";',
  'sequence wraps'),
 ('drivers/net/e82557/e82557.c',
  'if ((rc != 0 && rc != len) || vfs_sync() < 0) return 0;',
  'if (rc < 0 || vfs_sync() < 0) return 0;',
  'short write accepted'),
 ('drivers/net/e82557/e82557.c',
  'return rc == len && kstrncmp(buf, back, (u32)len) == 0;',
  'return rc == len && (1 || kstrncmp(buf, back, (u32)len) == 0);',
  'readback ignored'),
 ('drivers/net/e82557/e82557.c',
  'if (sysconfig_get_int(SYS_SYSTEM_CFG, "E82557", 1) == 0)',
  'if (0 && sysconfig_get_int(SYS_SYSTEM_CFG, "E82557", 1) == 0)',
  'kill switch ignored'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'outpd(base + E82557_PORT, E82557_PORT_SOFTWARE);',
  '/* omitted */',
  'software reset omitted'),
 ('drivers/net/e82557/e82557.c', 'if (!scb_wait(base)) {', 'if (0 && !scb_wait(base)) {', 'pre acceptance omitted'),
 ('drivers/net/e82557/e82557.c',
  'if (scb_wait(base)) return ISSUE_OK;',
  'if (1 || scb_wait(base)) return ISSUE_OK;',
  'post acceptance omitted'),
 ('drivers/net/e82557/e82557.c',
  'outp(base + E82557_SCB_COMMAND, e82557_scb_cmd(cuc, ruc));',
  'outpw(base + E82557_SCB_COMMAND, e82557_scb_cmd(cuc, ruc));',
  '16bit command'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'issued = scb_issue(base, E82557_CU_BASE, 0, 0, 1, &after);',
  'issued = ISSUE_OK;',
  'CU base omitted'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'issued = scb_issue(base, 0, E82557_RU_BASE, 0, 1, &after);',
  'issued = ISSUE_OK;',
  'RU base omitted'),
 ('drivers/net/e82557/e82557.c',
  '        outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);',
  '        /* reset omitted */',
  'stop reset omitted'),
 ('drivers/net/e82557/e82557.c', '        scb_mask(base, 1);', '        scb_mask(base, 0);', 'rst2 and R1 mask omitted'),
 ('drivers/net/e82557/e82557.c',
  'int stopped = stop_dma(base, &nic.stop, E82557_STOP_ATTEMPTS);\n    irq_restore(flags);',
  'int stopped = 1;\n    irq_restore(flags);',
  'cfg2 without selective reset'),
 ('drivers/net/e82557/e82557.c',
  '    e82557_cb_ia(nic.dma.va, nic.mac);\n'
  '    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);\n'
  '    if (why) goto done;\n'
  '    stage = "ru";',
  '    if (0) e82557_cb_ia(nic.dma.va, nic.mac);\n    stage = "ru";',
  'ia2 omitted'),
 ('drivers/net/e82557/e82557.c', '    if (changed) {', '    if (1 || changed) {', 'BM restoration unowned'),
 ('drivers/net/e82557/e82557.c',
  'if (registered) (void)irq_unregister(dev->irq_line, e82557_irq, NULL);\n'
  '                bm_restore(dev, changed_bm);\n'
  '                (void)dma_free(&nic.dma);',
  'bm_restore(dev, changed_bm);\n'
  '                (void)dma_free(&nic.dma);\n'
  '                if (registered) (void)irq_unregister(dev->irq_line, e82557_irq, NULL);',
  'unregister after free'),
 ('drivers/net/e82557/e82557.c', 'E82557_BARRIER(); /* publish:', '/* publish:', 'CB publication barrier omitted'),
 ('drivers/net/e82557/e82557.c', 'E82557_BARRIER(); /* acquire:', '/* acquire:', 'CB acquisition barrier omitted'),
 ('drivers/net/e82557/e82557.c',
  'E82557_BARRIER(); /* recv completion',
  '/* recv completion',
  'RX acquisition barrier omitted'),
 ('drivers/net/e82557/e82557.c', 'if (ack == 0xff)', 'if (st == 0xffff)', 'dead test includes low byte'),
 ('drivers/net/e82557/e82557.c',
  'outp(nic.base + E82557_SCB_ACK, ack);',
  'outp(nic.base + E82557_SCB_ACK, 0xff);',
  'IRQ ack fixed ff'),
 ('drivers/net/e82557/e82557.c', 'outp(nic.base + E82557_SCB_ACK, ack);', '/* no ack */', 'IRQ ack omitted'),
 ('drivers/net/e82557/e82557.c', 'pass < 2', 'pass < 1', 'second IRQ pass omitted'),
 ('drivers/net/e82557/e82557.c',
  'nic.state == E82557_OFF || nic.state == E82557_STARTING ||',
  'nic.state == E82557_OFF ||',
  'STARTING touches SCB'),
 ('drivers/net/e82557/e82557.c',
  'if (nic.state == E82557_RUNNING) {\n            if (st & (E82557_STAT_CX',
  'if (1) {\n            if (st & (E82557_STAT_CX',
  'RECOVER sets flags'),
 ('drivers/net/e82557/e82557.c',
  'if (!nic.swi_wait) (void)e82557_irq(0, NULL);\n            return; /* never issue',
  'if (1 || !nic.swi_wait) (void)e82557_irq(0, NULL);\n            return; /* never issue',
  'tick steals SWI'),
 ('drivers/net/e82557/e82557.c',
  'if (!nic.stats[E82557_ST_SWI]) why = "irq";',
  'if (0 && !nic.stats[E82557_ST_SWI]) why = "irq";',
  'SWI absent reported OK'),
 ('drivers/net/e82557/e82557.c',
  'if (rus == E82557_RUS_READY) {',
  'if (0 && rus == E82557_RUS_READY) {',
  'RU Ready guard omitted'),
 ('drivers/net/e82557/e82557.c', '    rx_scan(); /* stopped RU:', '    /* stopped RU:', 'RU stopped rescan omitted'),
 ('drivers/net/e82557/e82557.c',
  'if (nic.rx.ready == E82557_RFD_COUNT) return ISSUE_OK;',
  'if (0 && nic.rx.ready == E82557_RFD_COUNT) return ISSUE_OK;',
  'RU starts full ring'),
 ('drivers/net/e82557/e82557.c',
  'E82557_RU_START, rx_pa(), 1, &after);\n    if (rc',
  'E82557_RU_START, nic.dma.pa + E82557_RFD_OFF + nic.rx.cons * E82557_RFD_STRIDE, 1, &after);\n    if (rc',
  'RU pointer rx_cons'),
 ('drivers/net/e82557/e82557.c',
  'if (nic.rx.ready == E82557_RFD_COUNT) return ISSUE_OK;',
  'if (nic.rx.dev == nic.rx.cons) return ISSUE_OK;',
  'indices used for empty/full'),
 ('drivers/net/e82557/e82557.c',
  '    u16 st = inpw(nic.base + E82557_SCB_STATUS);\n    u32 rus',
  '    u16 st = 0;\n    u32 rus',
  'RU status not read'),
 ('drivers/net/e82557/e82557.c',
  'if (nic.tx == TX_RESERVED) { nic.recover_pending = 1; return; }',
  'if (0 && nic.tx == TX_RESERVED) { nic.recover_pending = 1; return; }',
  'recovery during reservation'),
 ('drivers/net/e82557/e82557.c',
  'if (nic.recover_pending && nic.tx != TX_RESERVED)',
  'if (0 && nic.recover_pending && nic.tx != TX_RESERVED)',
  'pending recovery never consumed'),
 ('drivers/net/e82557/e82557.c',
  'nic.gen != gen || nic.tx != TX_RESERVED',
  '0 || nic.tx != TX_RESERVED',
  'send generation unchecked'),
 ('drivers/net/e82557/e82557.c', '    nic.tx_issued = 0;', '    /* stale timestamp */', 'reservation keeps timestamp'),
 ('drivers/net/e82557/e82557.c',
  'nic.stats[(st & E82557_CB_OK) ? E82557_ST_TX_DMA_DONE : E82557_ST_TX_FAIL]++;',
  'nic.stats[E82557_ST_TX_DMA_DONE]++;',
  'anomalous TX OK ignored'),
 ('drivers/net/e82557/e82557.c', '    rx_scan(); /* R1 final scan:', '    /* R1 final scan:', 'R1 final scan omitted'),
 ('drivers/net/e82557/e82557.c',
  'nic.state != E82557_RUNNING && nic.state != E82557_RX_WAIT) {\n        rc',
  'nic.state != E82557_RUNNING) {\n        rc',
  'RX_WAIT recv disallowed'),
 ('drivers/net/e82557/e82557.c', 'nic.stats[drop ? E82557_ST_RX_DROP : E82557_ST_RX_OK]++;', 'if (!drop) nic.stats[E82557_ST_RX_OK]++;', 'rx_drop not counted'),
 ('drivers/net/e82557/e82557.c',
  'int drop = !(st & E82557_RFD_OK) ||',
  'int drop = (0 && !(st & E82557_RFD_OK)) ||',
  'bad RX accepted'),
 ('drivers/net/e82557/e82557.c', 'count < ETH_HLEN || count > E82557_FRAME_MAX;', '0;', 'RX length unchecked'),
 ('drivers/net/e82557/e82557.c',
  'if (!drop && count > cap) return OS32_ERR_NOSPC;',
  'if (0 && !drop && count > cap) return OS32_ERR_NOSPC;',
  'NOSPC consumes packet'),
 ('drivers/net/e82557/e82557.c',
  'if (ru_restart() != ISSUE_OK) recover_begin();',
  'if (ru_restart() != ISSUE_OK) recover_begin();\n'
  '            { int after; (void)scb_issue(nic.base, 0, E82557_RU_START, rx_pa(), 1, &after); }',
  'two issues per tick'),
 ('drivers/net/e82557/e82557.c', '> E82557_TX_WAIT_TICKS)', '>= E82557_TX_WAIT_TICKS)', 'TX timeout boundary'),
 ('drivers/net/e82557/e82557.c',
  'if (nic.stats[E82557_ST_RECOVER_TOTAL] > E82557_RECOVER_MAX)',
  'if (0 && nic.stats[E82557_ST_RECOVER_TOTAL] > E82557_RECOVER_MAX)',
  'recovery count unlimited'),
 ('drivers/net/e82557/e82557.c',
  'int e82557_loopback(int on) { (void)on; return OS32_ERR_NOSYS; }',
  'int e82557_loopback(int on) { (void)on; return 0; }',
  'loopback stub active'),
 ('drivers/net/e82557/e82557.c',
  'E82557_EE_COMPAT, &compat, &dummy)',
  'E82557_EE_PHY, &compat, &dummy)',
  'compat word not read'),
 ('drivers/net/e82557/e82557.c',
  'phy_prepare(base, &phy, width);',
  'phy_prepare(base, &phy, E82557_EE_WIDTH6);',
  'PHY EEPROM width fixed six'),
 ('drivers/net/e82557/e82557.c',
  'return mdi_read(base, p, E82557_PHY_BMSR, &p->bmsr_first) &&\n'
  '           mdi_read(base, p, E82557_PHY_BMSR, &p->bmsr);',
  'return mdi_read(base, p, E82557_PHY_BMSR, &p->bmsr);',
  'BMSR read once'),
 ('drivers/net/e82557/e82557.c',
  'p->old | E82557_NSC_PCR_SET',
  'p->old | (E82557_NSC_PCR_SET & ~0x0002)',
  'PCR bit 0x0002 omitted'),
 ('drivers/net/e82557/e82557.c',
  'p->old | E82557_NSC_PCR_SET',
  'p->old | (E82557_NSC_PCR_SET & ~0x0020)',
  'PCR bit 0x0020 omitted'),
 ('drivers/net/e82557/e82557.c',
  'p->old | E82557_NSC_PCR_SET',
  'p->old | (E82557_NSC_PCR_SET & ~0x0040)',
  'PCR bit 0x0040 omitted'),
 ('drivers/net/e82557/e82557.c',
  'p->old | E82557_NSC_PCR_SET',
  'p->old | (E82557_NSC_PCR_SET & ~0x0100)',
  'PCR bit 0x0100 omitted'),
 ('drivers/net/e82557/e82557.c',
  'p->old | E82557_NSC_PCR_SET',
  'p->old | (E82557_NSC_PCR_SET & ~0x0400)',
  'PCR bit 0x0400 omitted'),
 ('drivers/net/e82557/e82557.c', 'p->old | E82557_NSC_PCR_SET', 'E82557_NSC_PCR_SET', 'PCR other bits lost'),
 ('drivers/net/e82557/e82557.c', 'p->error || !p->nsc', 'p->error || (0 && !p->nsc)', 'PCR ID guard omitted'),
 ('drivers/net/e82557/e82557.c', 'if (p->value != p->old) {', 'if (1 || p->value != p->old) {', 'same PCR written'),
 ('drivers/net/e82557/e82557.c',
  '!sysconfig_get_int(SYS_SYSTEM_CFG, "E82557_PHYVENDOR", 1)',
  '0 && !sysconfig_get_int(SYS_SYSTEM_CFG, "E82557_PHYVENDOR", 1)',
  'PHYVENDOR ignored'),
 ('drivers/net/e82557/e82557.c',
  '(rb & E82557_NSC_PCR_VERIFY) != (p->value & E82557_NSC_PCR_VERIFY)',
  'rb != p->value',
  'reserved PCR bits verified'),
 ('drivers/net/e82557/e82557.c',
  'if (!mdi_read(base, p, E82557_NSC_PCR, &rb))',
  'if (0 && !mdi_read(base, p, E82557_NSC_PCR, &rb))',
  'PCR readback omitted'),
 ('drivers/net/e82557/e82557.c',
  '                                  E82557_LINK_WAIT_TICKS) != E82557_WAIT)',
  '                                  0) != E82557_WAIT)',
  'link deadline zero'),
 ('drivers/net/e82557/e82557_desc.c',
  'pa + rx_next(i) * E82557_RFD_STRIDE',
  'pa + i * E82557_RFD_STRIDE',
  'noncyclic ring'),
 ('drivers/net/e82557/e82557_desc.c', 'n + 1 == free ? E82557_RFD_EL : 0', 'E82557_RFD_EL', 'all RFD EL'),
 ('drivers/net/e82557/e82557_desc.c',
  'u32 free = E82557_RFD_COUNT - rx->ready;',
  'u32 free = E82557_RFD_COUNT;',
  'reown CPU-owned frames'),
 ('drivers/net/e82557/e82557_desc.c',
  'if (st & E82557_RFD_RNR) *rnr = 1;',
  'if (0 && st & E82557_RFD_RNR) *rnr = 1;',
  'RFD bit9 ignored'),
 ('drivers/net/e82557/e82557_desc.c',
  'e82557_wr16(rfd + E82557_RFD_SIZE_OFF, E82557_RFD_SIZE);',
  'e82557_wr16(rfd + E82557_RFD_SIZE_OFF, E82557_RFD_SIZE - 1);',
  'odd RFD size'),
 ('drivers/net/e82557/e82557_desc.c',
  '    e82557_wr16(rfd + E82557_DESC_COUNT, 0);\n    E82557_BARRIER(); /* recycle',
  '    /* actual not cleared */\n    E82557_BARRIER(); /* recycle',
  'EOF F not cleared'),
 ('drivers/net/e82557/e82557_desc.c',
  'E82557_BARRIER(); /* recycle zero',
  '/* recycle zero',
  'recycle zero barrier omitted'),
 ('drivers/net/e82557/e82557_desc.c', 'E82557_BARRIER(); /* full old', '/* full old', 'full EL barrier omitted'),
 ('drivers/net/e82557/e82557_desc.c',
  'E82557_BARRIER(); /* nonfull new',
  '/* nonfull new',
  'nonfull EL barrier omitted'),
 ('drivers/net/e82557/e82557_desc.c',
  'e82557_wr16(tail + E82557_DESC_CMD, 0);\n'
  '        E82557_BARRIER(); /* full old before new */\n'
  '        e82557_wr16(rfd + E82557_DESC_CMD, E82557_RFD_EL);',
  'e82557_wr16(rfd + E82557_DESC_CMD, E82557_RFD_EL);\n'
  '        E82557_BARRIER(); /* full old before new */\n'
  '        e82557_wr16(tail + E82557_DESC_CMD, 0);',
  'full EL order reversed'),
 ('drivers/net/e82557/e82557_desc.c',
  'e82557_wr16(rfd + E82557_DESC_CMD, E82557_RFD_EL);\n'
  '        E82557_BARRIER(); /* nonfull new before old */\n'
  '        e82557_wr16(tail + E82557_DESC_CMD, 0);',
  'e82557_wr16(tail + E82557_DESC_CMD, 0);\n'
  '        E82557_BARRIER(); /* nonfull new before old */\n'
  '        e82557_wr16(rfd + E82557_DESC_CMD, E82557_RFD_EL);',
  'nonfull EL order reversed'),
 ('drivers/net/e82557/e82557_regs.h',
  '#define E82557_RECOVER_RESETS  3u',
  '#define E82557_RECOVER_RESETS  4u',
  'recovery reset budget exceeded')]


# LB-1/LB-2 regression mutations retained across the LB-3 rewrite.
MUTATIONS += [
 ('drivers/net/e82557/e82557.c',
  '                (void)dma_mark_leaked(&nic.dma);',
  '                (void)dma_free(&nic.dma);',
  'free unproven DMA'),
 ('drivers/net/e82557/e82557.c',
  '                bm_restore(dev, changed_bm);\n                (void)dma_free(&nic.dma);',
  '                (void)dma_free(&nic.dma);\n                bm_restore(dev, changed_bm);',
  'free before BM restore'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'scb_mask(base, 1);\n    status',
  'scb_mask(base, 0);\n    status',
  'interrupt mask cleared'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'if (status & (E82557_CUS_MASK | E82557_RUS_MASK))',
  'if (0 && (status & (E82557_CUS_MASK | E82557_RUS_MASK)))',
  'idle guard removed'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> E82557_STAT_SHIFT));\n    status',
  'outp(base + E82557_SCB_ACK, 0xff);\n    status',
  'ack fixed ff'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'cpu_delay_us(E82557_RESET_US);\n    scb_mask',
  'cpu_delay_us(1);\n    scb_mask',
  'selective reset delay shortened'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'cpu_delay_us(E82557_RESET_US);\n    outpd',
  'cpu_delay_us(1);\n    outpd',
  'software reset delay shortened'),
 ('drivers/net/e82557/e82557.c',
  'if ((rc != 0 && rc != len) || vfs_sync() < 0) return 0;',
  'if ((rc != 0 && rc != len) || (0 && vfs_sync() < 0)) return 0;',
  'sync omitted'),
 ('drivers/net/e82557/e82557.c', 'return "previous";',
  'vfs_rm(E82557_TRY_PATH); return "previous";', 'same mark removed'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  'status = inpw(base + E82557_SCB_STATUS);\n        kprintf',
  '/* stale status */\n        kprintf', 'base timeout stale status'),
 ('drivers/net/e82557/e82557.c',
  'if (us == E82557_MDI_WAIT_US) return 0;',
  'if (0 && us == E82557_MDI_WAIT_US) return 0;', 'MDI wait unbounded'),
 ('drivers/net/e82557/e82557.c',
  '    phy_vendor(base, &phy);\n    nic_trace(E82557_TRACE_LINK);\n    phy_link_wait(base, &phy);',
  '    nic_trace(E82557_TRACE_LINK);\n    phy_link_wait(base, &phy);\n    phy_vendor(base, &phy);',
  'PCR after link wait'),
 ('drivers/net/e82557/e82557.c',
  '            if (!nic.swi_wait) (void)e82557_irq(0, NULL);\n            nic.state = E82557_RUNNING;\n            scb_mask(nic.base, 0);',
  '            nic.state = E82557_RUNNING;\n            scb_mask(nic.base, 0);\n            if (!nic.swi_wait) (void)e82557_irq(0, NULL);',
  'recovery drain after unmask'),
 ('drivers/net/e82557/e82557.c',
  'if (kstrcmp(why, "ng") == 0) why = "txng";',
  'why = "txng";', 'loopback failure reason overwritten'),
]


# Extraction boundary: successful reset/base must leave the overall probe declined
# until EEPROM, DMA, IRQ and normal operation have also completed.
MUTATIONS += [
 ('drivers/net/e82557/e82557.c',
  'if (rc != PCI_PROBE_OK) { result = rc; goto done; }',
  'result = rc; if (rc != PCI_PROBE_OK) { goto done; }',
  'reset success prematurely accepts probe'),
 ('drivers/net/e82557/e82557.c',
  'if (rc != PCI_PROBE_OK) { result = rc; goto done; }',
  'if (rc != PCI_PROBE_OK) { goto done; }',
  'reset quarantine lost'),
 ('drivers/net/e82557/e82557_probe_reset_base.inc',
  '*stage = "base";',
  '*stage = "rst";',
  'base diagnostic stage lost'),
]


def mutation_text(rel, old, new, label):
    text = (ROOT / rel).read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"mutation anchor count {text.count(old)}: {label}")
    return text.replace(old, new, 1)

def host_build(tmp, mutated=None):
    root = ROOT
    if mutated:
        root = pathlib.Path(tmp) / "tree"
        for rel in MIRROR:
            dst = root / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(mutated[1] if rel == mutated[0] else (ROOT / rel).read_text())
    # Use the actual resident machine table with the real driver probe seam.
    facade = (ROOT / "drivers/net/e82557/lan_module.c").read_text()
    table = re.search(r"const struct pci_driver \*const drivers\[\] = \{.*?\};", facade, re.S)
    if not table or "pci_bind_all(drivers, sizeof(drivers) / sizeof(drivers[0]));" not in facade:
        raise RuntimeError("LAN module bind table/call missing")
    (pathlib.Path(tmp) / "pci_table.h").write_text(
        '#define lan_module_probe e82557_probe\n#define e82557_driver platform_test_e82557_driver\n' +
        '#include "' + str(root / 'platform/pc98/lan_match.c') + '"\n' +
        '#undef e82557_driver\n#undef lan_module_probe\n' +
        "static const struct pci_driver *platform_test_selected(void) {\n" +
        table.group() + "\nreturn drivers[0];\n}\n" +
        "#define pci_drivers ((const struct pci_driver *const[]){platform_test_selected()})\n" +
        "#define PCI_DRIVER_COUNT (sizeof(pci_drivers) / sizeof(pci_drivers[0]))\n")
    exe = pathlib.Path(tmp) / "drv-host"
    subprocess.run(["gcc", *FLAGS,
                    *include_flags(root),
                    *INCLUDES, "-I" + str(tmp), str(root / HARNESS.relative_to(ROOT)),
                    "-o", str(exe)], check=True)
    return exe


def run_case(exe, case, only=0):
    return host32.run([str(exe), case, str(only)], capture_output=True, text=True, timeout=10)


def target_build(tmp):
    order = (ROOT / "platform/pc98/tick_order.h").read_text()
    positions = [order.index(name) for name in
                 ("PC98_TICK_NE2K", "PC98_TICK_E82557", "PC98_TICK_LINK")]
    facade = (ROOT / "drivers/net/e82557/lan_module.c").read_text()
    if positions != sorted(positions) or "tick_register(PC98_TICK_E82557, e82557_tick)" not in facade:
        raise RuntimeError("82557 tick registration missing/out of order")
    # The real registry dispatch is exercised by check-tick-host.
    if '#define E82557_BARRIER() __asm__ volatile("" ::: "memory")' not in (ROOT / "drivers/net/e82557/e82557_desc.h").read_text():
        raise RuntimeError("production compiler barrier missing")
    cross = pathlib.Path(os.environ.get("CROSS_DIR", "/home/hight/opt/cross")) / "bin/i386-elf-gcc"
    for rel in TARGET_SRCS:
        subprocess.run([str(cross), "-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
            "-fno-pie", "-fno-stack-protector", "-nostdlib", "-Os", "-Wall", "-Wextra", "-Werror",
            "-D__KERNEL_BUILD__", *INCLUDES,
            *include_flags(ROOT),
            "-c", str(ROOT / rel), "-o", str(pathlib.Path(tmp) / (pathlib.Path(rel).stem + ".o"))], check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    failed = 0
    for i, (rel, old, new, label) in enumerate(MUTATIONS, 1):
        exe = host_build(tmp, (rel, mutation_text(rel, old, new, label)))
        hits = [case for case in CASES if run_case(exe, case).returncode]
        print(f"MUTATION {i} {'RED' if hits else 'SURVIVED'} {label}: {','.join(hits[:3])}", flush=True)
        failed += not bool(hits)
    print(f"MUTATIONS {len(MUTATIONS)-failed}/{len(MUTATIONS)} RED", flush=True)
    return failed

def main():
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-ee-") as tmp:
        exe = host_build(tmp)
        if "--target" in args:
            target_build(tmp)
        cases = [a for a in args if not a.startswith("--")] or CASES
        failed = 0
        for case in cases:
            result = run_case(exe, case)
            print(f"CASE {case} rc={result.returncode}", flush=True)
            if result.returncode:
                print(result.stdout, end="")
                print(result.stderr, end="", file=sys.stderr)
                failed += 1
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        if "--mutate" in args:
            failed += mutate(tmp)
        return bool(failed)


if __name__ == "__main__":
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_e82557_drv.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
