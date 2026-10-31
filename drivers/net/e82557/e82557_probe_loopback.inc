static const char *probe_loopback(const char **stage, u16 *lb_len)
{
    u16 base = nic.base;
    u8 cfg[E82557_CFG_LEN], frame[E82557_HELLO_LEN];
    const char *why;
    *stage = "cfg";
    nic_trace(E82557_TRACE_CFG);
    e82557_config_bytes(cfg, E82557_CFG_LOOPBACK);
    e82557_cb_config(nic.dma.va, cfg);
    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);
    if (why) return why;
    *stage = "ia";
    nic_trace(E82557_TRACE_IA);
    e82557_cb_ia(nic.dma.va, nic.mac);
    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);
    if (why) return why;
    *stage = "ring";
    nic_trace(E82557_TRACE_RING);
    e82557_rx_init(rx_ring(), nic.dma.pa + E82557_RFD_OFF, &nic.rx);
    if (probe_ru() != ISSUE_OK) return "timeout";
    *stage = "lb";
    nic_trace(E82557_TRACE_LB);
    hello_build(frame, nic.mac);
    (void)e82557_tcb_build((u8 *)nic.dma.va + E82557_TCB_OFF, frame,
                           E82557_HELLO_LEN, E82557_TX_THRESH, E82557_TBD_NONE, 0);
    why = cb_run(base, &nic.dma, E82557_TCB_OFF, &nic.stop);
    if (why) {
        if (kstrcmp(why, "ng") == 0) why = "txng";
        return why;
    }
    for (u32 ms = 0; !(e82557_rd16(rx_ring()) & E82557_RFD_C); ms++) {
        if (ms == E82557_LB_WAIT_MS) return "timeout";
        cpu_delay_us(E82557_CB_POLL_US);
    }
    E82557_BARRIER(); /* loopback completion acquisition */
    *stage = "cmp";
    nic_trace(E82557_TRACE_CMP);
    *lb_len = e82557_rd16(rx_ring() + E82557_DESC_COUNT) & E82557_RFD_COUNT_MASK;
    if (!(e82557_rd16(rx_ring()) & E82557_RFD_OK) || *lb_len != E82557_HELLO_LEN) {
        return "ng";
    }
    E82557_BARRIER();
    E82557_DESC_ACCESS(rx_ring() + E82557_RFD_HEADER, *lb_len, 0);
    for (u32 i = 0; i < *lb_len; i++) if (rx_ring()[E82557_RFD_HEADER + i] != frame[i]) {
        return "ng";
    }
    *stage = "rst2";
    nic_trace(E82557_TRACE_RST2);
    unsigned int flags = irq_save();
    int stopped = stop_dma(base, &nic.stop, E82557_STOP_ATTEMPTS);
    irq_restore(flags);
    if (!stopped) return "idle";
    return NULL;
}
