/* Standalone, read-only MacIO ATA probe. See docs/ppc/ata.md §§2–5. */
#include "ofw.h"
#include "kstring.h"

enum {
    ATA_COUNT = 0x20, ATA_LBA_LOW = 0x30, ATA_LBA_MID = 0x40,
    ATA_LBA_HIGH = 0x50, ATA_DEVICE = 0x60, ATA_STATUS = 0x70,
    ATA_ALT_STATUS = 0x160, ATA_NIEN = 0x02, ATA_SELECT = 0xa0,
    ATA_SLAVE = 0x10, ATA_LBA = 0x40,
    ATA_ERR = 0x01, ATA_DRQ = 0x08, ATA_DF = 0x20,
    ATA_DRDY = 0x40, ATA_BSY = 0x80,
    ATA_READ_SECTORS = 0x20, ATA_IDENTIFY = 0xec,
    ATA_WORDS = 256, ATA_DUMP_BYTES = 64,
    KEYLARGO_FCR1 = 0x3c, CHANNEL_WAIT_SECONDS = 30,
    ATA_WAIT_SECONDS = 5, ATA_MAX_TB_HZ = 400000000,
    OF_PAGE = 4096, OF_TREE_DEPTH = 32, OF_TREE_NODES = 4096,
    PCI_ENTRY_CELLS = 5, PCI_BAR0 = 0x10, PCI_MEM32 = 0x02000000,
    PCI_SPACE_MASK = 0x03000000
};

static struct pm_escc serial;
static int serial_ok;
static int screen_out;   /* OF stdout ihandle: the G4 screen (read by camera) */
static cell prop[PM_PROP_CAP / sizeof(cell)];
static char path[PM_PATH_CAP];
static u16 data[ATA_WORDS];
static unsigned nodes_left = OF_TREE_NODES;
static u32 wait_ticks;
static unsigned channels;

static void put(const char *text)
{
    u32 n = kstrlen(text);
    if (screen_out) ofw_write(screen_out, text, n);
    if (serial_ok && pm_escc_write(&serial, text, n)) serial_ok = 0;
}

static void hex(u32 value, unsigned digits)
{
    static const char alphabet[] = "0123456789abcdef";
    char text[9];
    text[digits] = 0;
    while (digits) {
        text[--digits] = alphabet[value & 15];
        value >>= 4;
    }
    put(text);
}

static void decimal(u32 value)
{
    char text[11];
    char *end = text + sizeof(text) - 1;
    *end = 0;
    do {
        *--end = '0' + value % 10;
        value /= 10;
    } while (value);
    put(end);
}

static void address_line(const char *name, cell value)
{
    put(name); put("=0x"); hex(value, 8);
    put(" ("); decimal(value); put(")\r\n");
}

static int get(int node, const char *key)
{
    int n = ofw_getprop(node, key, prop, sizeof(prop));
    return n >= 0 && n <= (int)sizeof(prop) ? n : -1;
}

static int valid_node(int node)
{
    return node != 0 && node != -1;
}

static int matches(int n, const char *value)
{
    const char *p = (const char *)prop;
    unsigned len = kstrlen(value);
    for (int start = 0; start < n;) {
        int end = start;
        while (end < n && p[end]) ++end;
        if ((unsigned)(end - start) == len && !kstrncmp(p + start, value, len))
            return 1;
        start = end + 1;
    }
    return 0;
}

static int ata_node(int node)
{
    int n = get(node, "name");
    const char *name = (const char *)prop;
    if (matches(n, "ATA") || matches(n, "ide") ||
        (n >= 3 && !kstrncmp(name, "ata", 3)))
        return 1;
    n = get(node, "device_type");
    if (matches(n, "ata") || matches(n, "ide")) return 1;
    n = get(node, "compatible");
    return matches(n, "keylargo-ata") || matches(n, "heathrow-ata") ||
           matches(n, "ohare-ata");
}

static void property(int node, const char *key, int strings)
{
    put("  "); put(key); put("=");
    int n = get(node, key);
    if (n < 0) {
        put("<unavailable>");
    } else if (!strings && n % sizeof(cell) == 0) {
        for (unsigned i = 0; i < n / sizeof(cell); ++i) {
            if (i) put(" ");
            hex(prop[i], 8);
        }
    } else {
        const u8 *p = (const u8 *)prop;
        for (int i = 0; i < n; ++i) {
            if (strings && !p[i] && i == n - 1) break;
            if (strings && (!p[i] || (p[i] >= 0x20 && p[i] <= 0x7e))) {
                char ch[] = {p[i] ? (char)p[i] : '|', 0};
                put(ch);
            } else {
                if (strings) put("\\x");
                hex(p[i], 2);
            }
        }
    }
    put("\r\n");
}

static cell taskfile(int node, cell *fcr)
{
    int parent = ofw_parent(node);
    if (!valid_node(parent) || !matches(get(parent, "name"), "mac-io"))
        return 0;
    int n = get(node, "reg");
    if (n < 2 * (int)sizeof(cell) || n % (2 * sizeof(cell))) return 0;
    cell offset = prop[0], size = prop[1];
    if (n >= 4 * (int)sizeof(cell))
        address_line("dbdma_offset", prop[2]);
    if ((offset & 1) || size <= ATA_ALT_STATUS) return 0;

    n = get(parent, "assigned-addresses");
    if (n <= 0 || n % (PCI_ENTRY_CELLS * sizeof(cell))) return 0;
    cell base = 0;
    for (unsigned i = 0; i < n / sizeof(cell); i += PCI_ENTRY_CELLS) {
        if ((prop[i] & 0xff) != PCI_BAR0) continue;
        if (base || (prop[i] & PCI_SPACE_MASK) != PCI_MEM32 ||
            prop[i + 1] || prop[i + 3] || !prop[i + 2] ||
            offset > prop[i + 4] || size > prop[i + 4] - offset ||
            prop[i + 4] - 1 > (cell)-1 - prop[i + 2])
            return 0;
        base = prop[i + 2];
    }
    if (base && !(base & 3) &&
        get(parent, "vendor-id") == sizeof(cell) && prop[0] == 0x106b &&
        get(parent, "device-id") == sizeof(cell) && prop[0] == 0x22)
        *fcr = base + KEYLARGO_FCR1;
    return base ? base + offset : 0;
}

static int map_io(const struct ofw_env *env, cell base, cell last_offset)
{
    if (env->real_mode) return 0;
    cell first = base & ~(OF_PAGE - 1U);
    cell last = (base + last_offset) & ~(OF_PAGE - 1U);
    cell args[5], *p = args;
    if (env->ac == 2) *p++ = 0;
    *p++ = first;
    *p++ = first;
    *p++ = last - first + OF_PAGE;
    *p++ = PM_MAP_MODE_IO;
    return ofw_call_method("map", env->mmu, env->ac + 3, 0, args);
}

static u32 timebase(void)
{
    u32 low;
    __asm__ volatile("mftb %0" : "=r"(low));
    return low;
}

/* Four alternate-status reads plus at least 400 ns for device selection
 * and command issue. Time Base is already validated by the caller. */
static void settle(cell base)
{
    u32 start = timebase();
    for (unsigned i = 0; i < 4; ++i) ppc_load8(base + ATA_ALT_STATUS);
    u32 ticks = wait_ticks / (ATA_WAIT_SECONDS * 2500000U) + 1;
    while (timebase() - start < ticks) {}
}

static unsigned wait_status(cell base, unsigned mask, unsigned want, int command)
{
    u32 start = timebase();
    unsigned status;
    do {
        status = ppc_load8(base + ATA_ALT_STATUS);
        if (status == 0xff || status == 0 ||
            (!(status & ATA_BSY) && command && (status & (ATA_ERR | ATA_DF))) ||
            (status & mask) == want)
            break;
    } while (timebase() - start < wait_ticks);
    return status;
}

static void status_line(const char *phase, unsigned status)
{
    static const char *const names[] = {
        "ERR", "IDX", "CORR", "DRQ", "DSC", "DF", "DRDY", "BSY"
    };
    put(phase); put(" status=0x"); hex(status, 2); put(" [");
    int first = 1;
    for (int bit = 7; bit >= 0; --bit) {
        if (status & (1U << bit)) {
            if (!first) put(" ");
            put(names[bit]);
            first = 0;
        }
    }
    if (!status) put("none");
    put("]\r\n");
}

/* MacIO data is LE16. IDENTIFY strings store the first character in the
 * high byte of each word, while sector data starts with the low byte. */
static int transfer(cell base, unsigned command, unsigned *result)
{
    ppc_store8(base + ATA_STATUS, command);
    settle(base);
    unsigned status = wait_status(base, ATA_BSY | ATA_DRQ, ATA_DRQ, 1);
    if (status == 0xff ||
        (status & (ATA_BSY | ATA_DRQ | ATA_ERR | ATA_DF)) != ATA_DRQ) {
        *result = ppc_load8(base + ATA_STATUS);
        return 0;
    }
    for (unsigned i = 0; i < ATA_WORDS; ++i) {
        u32 word;
        __asm__ volatile("lhbrx %0,0,%1; eieio" : "=r"(word) : "r"(base) : "memory");
        data[i] = word;
    }
    settle(base);
    status = wait_status(base, ATA_BSY | ATA_DRQ, 0, 1);
    *result = ppc_load8(base + ATA_STATUS);
    return status && *result && !((status | *result) &
                                    (ATA_BSY | ATA_DRQ | ATA_ERR | ATA_DF));
}

static void identify_string(const char *key, unsigned first, unsigned words)
{
    char text[41];
    unsigned n = 0;
    for (unsigned i = 0; i < words; ++i) {
        u16 word = data[first + i];
        text[n++] = word >> 8;
        text[n++] = word;
    }
    while (n && (text[n - 1] == ' ' || !text[n - 1])) --n;
    for (unsigned i = 0; i < n; ++i)
        if ((u8)text[i] < 0x20 || (u8)text[i] > 0x7e) text[i] = '?';
    text[n] = 0;
    put(key); put(text); put("\r\n");
}

static void identify_summary(void)
{
    identify_string("model=", 27, 20);
    identify_string("serial=", 10, 10);
    identify_string("firmware=", 23, 4);
    put("lba28_sectors=0x");
    hex((data[49] & 0x0200) ? data[61] : 0, 4);
    hex((data[49] & 0x0200) ? data[60] : 0, 4);
    put(" lba48_sectors=0x");
    int lba48 = (data[83] & 0xc400) == 0x4400;
    for (unsigned i = 104; i > 100; --i)
        hex(lba48 ? data[i - 1] : 0, 4);
    put("\r\n");
    put("capabilities=0x"); hex(data[49], 4);
    put(data[49] & 0x0100 ? " dma=yes" : " dma=no");
    put(" pio_word51=0x"); hex(data[51], 4);
    put(" pio3_4_mask="); put((data[53] & 2) ? "0x" : "unknown");
    if (data[53] & 2) hex(data[64] & 3, 1);
    put(" mwdma_word63=0x"); hex(data[63], 4);
    put(" udma_word88="); put((data[53] & 4) ? "0x" : "unknown");
    if (data[53] & 4) hex(data[88], 4);
    put("\r\n");
}

static int packet_signature(cell base)
{
    unsigned mid = ppc_load8(base + ATA_LBA_MID);
    unsigned high = ppc_load8(base + ATA_LBA_HIGH);
    return mid == 0x14 && high == 0xeb;
}

static int probe_device(cell base, unsigned unit)
{
    put("device="); hex(unit, 1); put("\r\n");
    ppc_store8(base + ATA_DEVICE, ATA_SELECT | (unit ? ATA_SLAVE : 0));
    settle(base);
    unsigned status = wait_status(base, ATA_BSY | ATA_DRQ | ATA_DRDY, ATA_DRDY, 0);
    status_line("select", status);
    if (!status || status == 0xff) {
        put(!status && packet_signature(base) ? "presence=atapi\r\n" :
                                               "presence=absent\r\n");
        return 1;
    }
    if (status & (ATA_BSY | ATA_DRQ)) {
        put("presence=unknown channel-not-idle\r\n");
        return 0;
    }
    /* ATAPI devices need not assert DRDY. Never send IDENTIFY PACKET. */
    if (!(status & ATA_DRDY) || (status & ATA_DF)) {
        put(packet_signature(base) ? "presence=atapi\r\n" :
                                     "presence=unknown not-ready\r\n");
        return 1;
    }

    int ok = transfer(base, ATA_IDENTIFY, &status);
    status_line("identify", status);
    if (!ok) {
        put((status & (ATA_BSY | ATA_DRQ | ATA_ERR)) == ATA_ERR &&
            packet_signature(base) ? "presence=atapi\r\n" :
                                     "presence=unknown identify-failed\r\n");
        return status == 0xff || !(status & (ATA_BSY | ATA_DRQ));
    }
    put("presence=ata\r\n");
    identify_summary();
    if (!(data[49] & 0x0200) || !(data[60] | data[61])) {
        put("lba0=skipped no-lba28\r\n");
        return 1;
    }
    if ((data[106] & 0xd000) == 0x5000 &&
        (data[117] != ATA_WORDS || data[118])) {
        put("lba0=skipped logical-sector-size\r\n");
        return 1;
    }

    ppc_store8(base + ATA_DEVICE, ATA_SELECT | ATA_LBA | (unit ? ATA_SLAVE : 0));
    settle(base);
    ppc_store8(base + ATA_COUNT, 1);
    ppc_store8(base + ATA_LBA_LOW, 0);
    ppc_store8(base + ATA_LBA_MID, 0);
    ppc_store8(base + ATA_LBA_HIGH, 0);
    ok = transfer(base, ATA_READ_SECTORS, &status);
    status_line("read", status);
    if (!ok)
        return status == 0xff || !(status & (ATA_BSY | ATA_DRQ));
    put("lba0=");
    for (unsigned i = 0; i < ATA_DUMP_BYTES / 2; ++i) {
        hex(data[i] & 0xff, 2);
        hex(data[i] >> 8, 2);
    }
    put(" tail=");
    hex(data[ATA_WORDS - 1] & 0xff, 2);
    hex(data[ATA_WORDS - 1] >> 8, 2);
    put(data[ATA_WORDS - 1] == 0xaa55 ? " sig55aa=yes\r\n" : " sig55aa=no\r\n");
    return 1;
}

static void probe_channel(const struct ofw_env *env, int node)
{
    int n = ofw_package_to_path(node, path, sizeof(path));
    put("ATA node=");
    if (n > 0 && n < (int)sizeof(path)) {
        path[n] = 0;
        put(path);
    } else {
        put("<unknown>");
    }
    put("\r\n");
    property(node, "name", 1);
    property(node, "device_type", 1);
    property(node, "compatible", 1);
    property(node, "reg", 0);
    property(node, "interrupts", 0);
    property(node, "AAPL,interrupts", 0);
    cell fcr = 0;
    cell base = taskfile(node, &fcr);
    if (!base) {
        put("channel=skipped unsupported-window\r\n");
        return;
    }
    if (!wait_ticks) {
        put("channel=skipped timebase-unavailable\r\n");
        return;
    }
    if (map_io(env, base, ATA_ALT_STATUS)) {
        put("channel=skipped of-map-failed\r\n");
        return;
    }
    ppc_disable();
    ++channels;
    address_line("taskfile", base);
    if (fcr && !map_io(env, fcr, 3)) {
        u32 word;
        __asm__ volatile("lwbrx %0,0,%1; eieio" : "=r"(word) : "r"(fcr) : "memory");
        put("keylargo fcr1=0x"); hex(word, 8);
        put(" EIDE0_ENABLE="); decimal((word >> 23) & 1);
        put(" EIDE0_RESET="); decimal((word >> 24) & 1); put("\r\n");
    }
    u32 second_ticks = wait_ticks / ATA_WAIT_SECONDS;
    u32 start = timebase();
    unsigned status = ppc_load8(base + ATA_ALT_STATUS);
    status_line("channel", status);
    for (unsigned sec = 1; sec <= CHANNEL_WAIT_SECONDS &&
         status != 0xff && (status & ATA_BSY); ++sec) {
        while (timebase() - start < second_ticks) {}
        start += second_ticks;
        unsigned previous = status;
        status = ppc_load8(base + ATA_ALT_STATUS);
        if (previous != status || sec % 5 == 0) {
            put("t="); decimal(sec); status_line("", status);
        }
    }
    if (status != 0xff && (status & ATA_BSY)) {
        put("BSY stuck\r\n");
        return;
    }
    if (status != 0xff && (status & ATA_DRQ))
        status = wait_status(base, ATA_BSY | ATA_DRQ, 0, 0);
    if (status != 0xff && (status & (ATA_BSY | ATA_DRQ))) return;
    ppc_store8(base + ATA_ALT_STATUS, ATA_NIEN);
    if (probe_device(base, 0)) probe_device(base, 1);
}

static void walk(const struct ofw_env *env, int node, unsigned depth)
{
    while (valid_node(node) && depth < OF_TREE_DEPTH && nodes_left) {
        --nodes_left;
        if (ata_node(node)) probe_channel(env, node);
        walk(env, ofw_child(node), depth + 1);
        node = ofw_peer(node);
    }
}

void powermac_boot(ppc_ci ci)
{
    struct ofw_env env;
    ofw_init(ci);
    if (ofw_setup(&env)) ppc_stop();
    screen_out = env.out;
    serial_ok = !pm_escc_prepare(&env, &serial);
    if (!serial_ok) put("ATA probe: ESCC unavailable, screen only\r\n");
    int cpus = ofw_finddevice("/cpus");
    for (int cpu = valid_node(cpus) ? ofw_child(cpus) : 0;
         valid_node(cpu); cpu = ofw_peer(cpu)) {
        if (get(cpu, "timebase-frequency") == sizeof(cell) &&
            prop[0] && prop[0] <= ATA_MAX_TB_HZ) {
            wait_ticks = prop[0] * ATA_WAIT_SECONDS;
            break;
        }
    }
    ppc_disable();
    put("ATA probe begin\r\n");
    int root = ofw_finddevice(PM_ROOT);
    if (valid_node(root)) walk(&env, ofw_child(root), 0);
    put("ATA probe done channels=0x"); hex(channels, 4); put("\r\n");
    if (serial_ok) pm_escc_drain(&serial);
    ppc_stop();
}
