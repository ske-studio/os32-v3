static int probe_reset_base(u16 base, const char **stage, const char **why)
{
    u16 status;
    int after = 0, issued;
    unsigned int flags;
    *stage = "rst";
    nic_trace(E82557_TRACE_RST);
    outpd(base + E82557_PORT, E82557_PORT_SOFTWARE);
    cpu_delay_us(E82557_RESET_US);
    outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);
    cpu_delay_us(E82557_RESET_US);
    scb_mask(base, 1);
    status = inpw(base + E82557_SCB_STATUS);
    outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> E82557_STAT_SHIFT));
    status = inpw(base + E82557_SCB_STATUS);
    if (status & (E82557_CUS_MASK | E82557_RUS_MASK)) {
        pci_bind_set_reason(PCI_BIND_RESET_FAILED);
        *why = "idle"; return PCI_PROBE_QUARANTINE;
    }
    *stage = "base";
    nic_trace(E82557_TRACE_BASE);
    flags = irq_save();
    issued = scb_issue(base, E82557_CU_BASE, 0, 0, 1, &after);
    if (issued == ISSUE_OK) issued = scb_issue(base, 0, E82557_RU_BASE, 0, 1, &after);
    irq_restore(flags);
    if (issued != ISSUE_OK) {
        status = inpw(base + E82557_SCB_STATUS);
        kprintf(E82557_LOG_ATTR, "[e82557] rst=ok base=timeout scb=%04x\n", status);
        *why = after ? "post" : "pre"; return PCI_PROBE_DECLINE;
    }
    return PCI_PROBE_OK;
}
