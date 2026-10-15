/* Foreground probe diagnostics. Never called under irq_save, from IRQ or tick. */
#include "nic_trace.h"
#include "drivers/e82557.h"
#include "include/bootinfo.h"
#include "include/kprintf.h"
#include "fs/vfs.h"
#include "generated/kdebug_import_macros.h"

#define NIC_TRACE_CAP 512U /* all LB2b stages (STAT three times), u32 seq/tick at full width */
static char trace_buf[NIC_TRACE_CAP];
static u32 trace_len;
static int trace_off;

static void append(const char *s)
{
    while (*s && trace_len < NIC_TRACE_CAP - 1)
        trace_buf[trace_len++] = *s++;
    if (*s) trace_off = 1;
    trace_buf[trace_len] = 0;
}

static int begin(void)
{
    char mark[E82557_TRY_CAP];
    BootImageInfo image;
    int n = vfs_read(E82557_TRY_PATH, mark, sizeof(mark));
    u32 crc = 0, seq = 0;
    if (n < 11 || n >= (int)sizeof(mark) || mark[8] != ' ' ||
        mark[n - 1] != '\n' || boot_image_info(&image) < 0 || !image.crc_valid)
        return 0;
    for (int i = 0; i < 8; i++) {
        u32 digit = (u8)mark[i];
        if (digit >= '0' && digit <= '9') digit -= '0';
        else if (digit >= 'a' && digit <= 'f') digit -= 'a' - 10;
        else return 0;
        crc = (crc << 4) | digit;
    }
    for (int i = 9; i < n - 1; i++) {
        u32 digit = (u8)mark[i] - '0';
        if (digit > 9 || seq > (0xffffffffU - digit) / 10) return 0;
        seq = seq * 10 + digit;
    }
    if (!seq || crc != image.image_crc) return 0;
    mark[n - 1] = 0;
    append("seq=");
    append(mark + 9);
    append(" crc=");
    mark[8] = 0;
    append(mark);
    append("\n");
    return 1;
}

void nic_trace(u32 stage)
{
    static const char *const names[] = {
        "try", "win", "rst", "base", "eeprom", "dma", "cfg", "ia", "tx", "stop", "phy", "link", "tx2", "stat", "done"
    };
    char number[11];
    if (trace_off) return;
    if (stage == E82557_TRACE_TRY) trace_len = 0;
    if (stage < E82557_TRACE_TRY || stage > E82557_TRACE_DONE ||
        (!trace_len && !begin())) goto failed;
    append("LB2b ");
    append(names[stage - E82557_TRACE_TRY]);
    append(" tick=");
    kutoa_dec(tick_count, number, sizeof(number));
    append(number);
    append("\n");
    if (trace_off) goto failed;
    int rc = vfs_write(E82557_LOG_PATH, trace_buf, trace_len);
    if ((rc != 0 && rc != (int)trace_len) || vfs_sync() != VFS_OK) goto failed;
    return;
failed:
    trace_off = 1;
    /* The void export reports through the kernel logger before probe's final line. */
    kprintf(0x07, "[e82557] trace=off\n");
}
