/* Actual foreground trace: persistence ordering, identity and failure latch. */
#include "../../kdebug/nic_trace.c"
static u32 writes, syncs, logs, read_mode, fail_write, fail_sync, bad;
static int full_width;
static char saved[NIC_TRACE_CAP];

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
u32 kdi_get_tick_count(void) { return full_width ? 0xffffffffu : 123; }
int boot_image_info(void *out)
{
    BootImageInfo *info = out;
    info->crc_valid = 1;
    info->image_crc = 0x1234abcd;
    return 0;
}
int vfs_read(const char *path, void *out, u32 cap)
{
    const char *mark = "1234abcd 42\n";
    if (!equal(path, E82557_TRY_PATH)) bad++;
    if (full_width) mark = "1234abcd 4294967295\n";
    if (read_mode == 1) return -1;
    if (read_mode == 2) mark = "1234abce 42\n";
    if (read_mode == 3) mark = "1234abcd 4294967296\n";
    if (read_mode == 4) mark = "1234abcd 0\n";
    if (read_mode == 5) mark = "1234abcd 42";
    if (read_mode == 6) mark = "1234abcd 4x\n";
    u32 n = 0;
    while (mark[n] && n < cap) { ((char *)out)[n] = mark[n]; n++; }
    return (int)n;
}
int vfs_write(const char *path, const void *data, u32 n)
{
    if (!equal(path, E82557_LOG_PATH) || n >= sizeof(saved)) bad++;
    writes++;
    for (u32 i = 0; i < n; i++) saved[i] = ((const char *)data)[i];
    saved[n] = 0;
    return fail_write == 2 ? (int)n - 1 : fail_write ? -1 : 0;
}
int vfs_sync(void)
{
    syncs++;
    if (syncs != writes) bad++;
    return fail_sync ? -1 : 0;
}
int kutoa_dec(u32 value, char *buf, int cap)
{
    (void)value; (void)cap;
    if (full_width) {
        const char digits[]="4294967295";
        for (u32 i=0;i<sizeof(digits);i++) buf[i]=digits[i];
        return 10;
    }
    buf[0] = '1'; buf[1] = '2'; buf[2] = '3'; buf[3] = 0;
    return 3;
}
void kprintf(u8 color, const char *fmt, ...)
{
    (void)color;
    if (!equal(fmt, "[e82557] trace=off\n")) bad++;
    logs++;
}
void _start(void)
{
    nic_trace(E82557_TRACE_TRY);
    if (!equal(saved, "seq=42 crc=1234abcd\nLB2 try tick=123\n")) bad++;
    nic_trace(E82557_TRACE_DONE);
    if (!equal(saved, "seq=42 crc=1234abcd\nLB2 try tick=123\nLB2 done tick=123\n") ||
        writes != 2 || syncs != 2 || logs) bad++;
    nic_trace(E82557_TRACE_TRY);
    if (!equal(saved, "seq=42 crc=1234abcd\nLB2 try tick=123\n")) bad++;
    full_width=1; writes=syncs=logs=0;
    for (u32 stage=E82557_TRACE_TRY;stage<=E82557_TRACE_DONE;stage++) nic_trace(stage);
    if (trace_off || logs || writes!=E82557_TRACE_DONE || syncs!=writes) bad++;
    full_width=0;
    for (u32 scenario = 1; scenario <= 11; scenario++) {
        trace_len = 0; trace_off = 0; writes = syncs = logs = 0;
        read_mode = scenario <= 6 ? scenario : 0;
        fail_write = scenario == 11 ? 2 : scenario == 7; fail_sync = scenario == 8;
        if (scenario == 10) trace_len = NIC_TRACE_CAP - 1;
        nic_trace(scenario == 9 ? E82557_TRACE_DONE + 1 :
                  scenario == 10 ? E82557_TRACE_DONE : E82557_TRACE_TRY);
        u32 previous = writes;
        nic_trace(E82557_TRACE_DONE);
        if (logs != 1 || writes != previous || !trace_off ||
            (scenario <= 6 && writes) || (fail_write && syncs) ||
            (fail_sync && syncs != 1)) bad++;
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(bad ? 1 : 0) : "memory");
    for (;;) { }
}
