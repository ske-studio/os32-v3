/* R5 only: EEPROM observation, no reset, DMA, IRQ, or config writes.
 * Remove the diagnostic output and dump before the full L-B driver. */
#include "e82557.h"
#include "e82557_eeprom.h"
#ifndef E82557_HOST_TEST
#include "io.h"
#include "kernel/cpu_calibrate.h"
#include "serial.h"
#include "kprintf.h"
#endif

static void ee_write(void *ctx, u8 value)
{
    outp(*(u16 *)ctx + E82557_EE_CTRL, value);
}
static u8 ee_read(void *ctx)
{
    return inp(*(u16 *)ctx + E82557_EE_CTRL);
}
static void ee_delay(void *ctx, u32 us)
{
    (void)ctx;
    cpu_delay_us(us);
}
static void ee_log(const char *line) { kprintf(E82557_LOG_ATTR, "%s\n", line); }
static void ee_marker(const char *line)
{
    if (!serial_gate_active()) { serial_puts_polled(line); serial_puts_polled("\r\n"); }
    ee_log(line);
}

static int e82557_probe(const struct pci_dev *dev)
{
    /* R5 only; shared scratch is safe in the cooperative, single-task boot. */
    static u16 words[E82557_EE_DUMP_WORDS];
    struct e82557_observation obs[E82557_R5_TRIES];
    char line[E82557_R5_LINE_CAP];
    u16 base;
    int why, method, n = 0, row;
    struct e82557_ee_pins pins = { &base, ee_write, ee_read, ee_delay, E82557_EE_STEP_US };
    if (!e82557_io_window(dev, &base, &why)) {
        kprintf(E82557_LOG_ATTR, "[e82557] %u:%u.%u skip(%s)\n",
                dev->bus, dev->dev, dev->fn, e82557_window_name(why));
        return PCI_PROBE_DECLINE;
    }
    e82557_r5_begin_line(line, sizeof(line), base);
    ee_marker(line);
    while ((method = e82557_r5_next(obs, n)) != E82557_STOP) {
        e82557_r5_observe(&pins, method, words, &obs[n]);
        n++;
        e82557_r5_observation_line(line, sizeof(line), &obs[n - 1], n);
        ee_log(line);
    }
    ee_marker("[e82557] R5 end");
    e82557_r5_final_line(line, sizeof(line), obs, n);
    ee_log(line);
    /* A failed width re-detection has no final image; never dump stale words. */
    if (obs[n - 1].width == E82557_EE_WIDTH6 || obs[n - 1].width == E82557_EE_WIDTH8) {
        for (row = 0; row < E82557_EE_DUMP_WORDS; row += E82557_EE_DUMP_ROW) {
            e82557_r5_dump_line(line, sizeof(line), words, row, obs[n - 1].width);
            ee_log(line);
        }
    }
    return PCI_PROBE_DECLINE;
}

const struct pci_driver e82557_driver = {
    sizeof(struct pci_driver), E82557_VENDOR, E82557_DEVICE,
    PCI_CLASS_NETWORK, PCI_SUB_NET_ETHERNET, "e82557", e82557_probe
};
