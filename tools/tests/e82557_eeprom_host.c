/* Real EEPROM implementation, with a clocked 93C46/93C66 model.
 * Contract/T/S are separate oracles. Record: tools/tests/e82557_eeprom_tdd.md */
/* Freestanding ILP32: this host has no 32-bit glibc. These helpers serve
 * the test oracle/log capture only; none are compiled into the kernel. */
#include <stdarg.h>
typedef unsigned long size_t;
#define stderr 2
static __attribute__((noreturn)) void exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static __attribute__((noreturn)) void abort(void) { exit(99); }
static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void *memset(void *dst, int v, size_t n)
{
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)v;
    return dst;
}
static int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    while (n--) { if (*p != *q) return *p - *q; p++; q++; }
    return 0;
}
static int strncmp(const char *a, const char *b, size_t n)
{
    while (n--) { if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return 0;
}
static int strcmp(const char *a, const char *b) { return strncmp(a, b, strlen(a) + 1); }
static int atoi(const char *s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }
static void fmt_char(char *dst, size_t cap, size_t *len, char c)
{
    if (*len + 1 < cap) dst[*len] = c;
    (*len)++;
}
static int vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap)
{
    size_t len = 0;
    while (*fmt) {
        if (*fmt != '%') { fmt_char(dst, cap, &len, *fmt++); continue; }
        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s) fmt_char(dst, cap, &len, *s++);
        } else {
            unsigned int v = va_arg(ap, unsigned int), radix = *fmt == 'x' ? 16 : 10;
            char digits[16]; int n = 0;
            if (*fmt == 'd' && (int)v < 0) { fmt_char(dst, cap, &len, '-'); v = 0u - v; }
            do { unsigned int d = v % radix; digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= radix; } while (v);
            while (width-- > n) fmt_char(dst, cap, &len, '0');
            while (n) fmt_char(dst, cap, &len, digits[--n]);
        }
        fmt++;
    }
    if (cap) dst[len < cap ? len : cap - 1] = 0;
    return (int)len;
}
static void fprintf(int fd, const char *fmt, ...)
{
    char buf[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(fd), "c"(buf), "d"(strlen(buf)) : "memory");
}
__asm__(".global _start\n_start:\n"
        "mov (%esp), %eax\nlea 4(%esp), %edx\npush %edx\npush %eax\ncall main\n"
        "mov %eax, %ebx\nmov $1, %eax\nint $0x80\n");
#include "../../drivers/pci_decode.c"
#include "../../drivers/e82557_eeprom.c"
_Static_assert(sizeof(u32) == 4, "EEPROM model requires target-width u32");

static int failed, only; /* 0 all, 1 timing T, 2 sequence S */
#define CK_KIND(kind, x) do { if ((!only || only == (kind)) && !(x)) { \
    fprintf(stderr, "FAIL %s:%d %s: %s\n", __func__, __LINE__, \
            (kind)==1?"T":(kind)==2?"S":"contract", #x); failed++; } } while (0)
#define CHECK(x) CK_KIND(3, x)
#define T(x) CK_KIND(1, x)
#define S(x) CK_KIND(2, x)
#define MAX_EVENTS 100000
struct event { char kind; unsigned int value; };
static struct event actual[MAX_EVENTS], expected[MAX_EVENTS];
static int na, ne;
struct model {
    int width, falling, stuck, clocks, addr, prefix, do_bit, transactions;
    u16 words[256], current;
    u8 pin;
    unsigned int now, rise, fall, off, last_rise;
    int have_fall, have_rise, timing_errors, writes, reads;
};
static struct model m;
static void record(char kind, unsigned int value)
{
    if (na >= MAX_EVENTS) { fprintf(stderr, "model bounded-operation guard\n"); exit(20); }
    actual[na++] = (struct event){kind, value};
}
static void expect(char kind, unsigned int value)
{
    if (ne >= MAX_EVENTS) abort();
    expected[ne++] = (struct event){kind, value};
}
static void timing(int good) { if (!good) m.timing_errors++; }
static void model_write(void *ctx, u8 pin)
{
    struct model *q = ctx;
    u8 old = q->pin;
    record('W', pin);
    q->writes++;
    if (!(pin & E82557_EE_CS) && (pin & E82557_EE_SK)) {
        fprintf(stderr, "model fatal: SK with CS low\n"); exit(22);
    }
    if ((pin & E82557_EE_CS) && !(old & E82557_EE_CS)) {
        timing(q->now - q->off >= E82557_EE_CS_LOW_US);
        q->transactions++;
        q->clocks = q->addr = q->prefix = 0;
        q->do_bit = 1;
        q->have_rise = 0;
        q->fall = q->now;
        q->have_fall = 1;
    }
    if ((pin & E82557_EE_SK) && !(old & E82557_EE_SK)) {
        timing((pin & E82557_EE_CS) && (old & E82557_EE_CS));
        if (q->have_fall) timing(q->now - q->fall >= E82557_EE_LOW_US);
        if (q->have_rise) timing(q->now - q->last_rise >= E82557_EE_PERIOD_US);
        q->rise = q->last_rise = q->now;
        q->have_rise = 1;
        q->clocks++;
        if (q->clocks <= 3) {
            q->prefix = (q->prefix << 1) | !!(pin & E82557_EE_DI);
            if (q->clocks == 3 && q->prefix != 6) {
                fprintf(stderr, "model fatal: non-READ opcode %d\n", q->prefix); exit(21);
            }
        } else if (q->clocks <= 3 + q->width) {
            q->addr = (q->addr << 1) | !!(pin & E82557_EE_DI);
            if (q->clocks == 3 + q->width) {
                q->current = q->words[q->addr];
                if (!q->falling) q->do_bit = 0;
            }
        } else {
            int bit = q->clocks - (3 + q->width) - 1;
            q->do_bit = bit < 16 ? !!(q->current & (1u << (15 - bit))) : 1;
        }
    }
    if (!(pin & E82557_EE_SK) && (old & E82557_EE_SK)) {
        timing(q->now - q->rise >= E82557_EE_HIGH_US);
        q->fall = q->now;
        q->have_fall = 1;
        if (q->falling && q->clocks == 3 + q->width) q->do_bit = 0;
    }
    if (!(pin & E82557_EE_CS) && (old & E82557_EE_CS)) q->off = q->now;
    q->pin = pin;
}
static u8 model_read(void *ctx)
{
    struct model *q = ctx;
    int bit = q->stuck < 0 ? q->do_bit : q->stuck;
    record('R', 0);
    q->reads++;
    return bit ? E82557_EE_DO : 0;
}
static void model_delay(void *ctx, u32 us)
{
    struct model *q = ctx;
    record('D', us);
    q->now += us;
}
static struct e82557_ee_pins pins(void)
{
    struct e82557_ee_pins p = { &m, model_write, model_read, model_delay, E82557_EE_STEP_US };
    return p;
}
static void reset_model(int width, int falling, int stuck)
{
    unsigned int i;
    memset(&m, 0, sizeof(m));
    m.width = width; m.falling = falling; m.stuck = stuck; m.do_bit = 1;
    for (i = 0; i < (1u << width); i++) {
        m.words[i] = (u16)(i * 313u + 0x8137u);
        if (i == 0) m.words[i] = 0x0000;
        if (i == 1) m.words[i] = 0x124C;
        if (i == 2) m.words[i] = 0x5634;
    }
    na = ne = 0;
}
/* S: independent construction from the specified W/D/R grammar.
 * auto emits only prefix + k zero address bits, then CS-off (no data).
 * k=6/8 success, 1/7 rejected width, 8 nowin; fixed emits all 16 data bits. */
static void wd(unsigned int value, unsigned int step)
{
    expect('W', value); expect('D', step);
}
static void expected_transaction(int width, int addr, int autodetect, int k, unsigned int step)
{
    int i, n = autodetect ? k : width;
    wd(0, step); wd(2, step);
    for (i = 0; i < 3 + n; i++) {
        int bit = i < 3 ? (i < 2) : (autodetect ? 0 : !!(addr & (1u << (width - 1 - (i - 3)))));
        unsigned int pin = 2 | (bit ? 4 : 0);
        wd(pin, step); wd(pin | 1, step); wd(pin, step);
        if (i >= 3) expect('R', 0);
    }
    if (!autodetect) {
        for (i = 0; i < 16; i++) {
            wd(3, step); expect('R', 0); wd(2, step);
        }
    }
    wd(0, step);
}
static void verify(void)
{
    int i, same = na == ne;
    for (i = 0; same && i < na; i++)
        if (actual[i].kind != expected[i].kind || actual[i].value != expected[i].value) same = 0;
    S(same);
    T(m.timing_errors == 0);
    T(m.pin == 0);
    T(m.now - m.off >= E82557_EE_CS_LOW_US);
}

static struct pci_dev device(void)
{
    struct pci_dev d;
    memset(&d, 0xA5, sizeof(d)); /* padding must have no meaning */
    d.bus = 0; d.dev = 11; d.fn = 0;
    d.vendor = 0x8086; d.device = 0x1229; d.class = 2; d.subclass = 0;
    d.bar[1] = 0x6001; d.command = 0x0147;
    return d;
}
static void window_case(int index)
{
    const u32 raw[] = {0x6001, 1, 0, 0x20410008, 0x6004, 0x6005, 0xFFE1, 0x10001, 0xFFFFFFE1, 0x6001};
    const int why[] = {0, E82557_BAR_ZERO, E82557_BAR_KIND, E82557_BAR_KIND, E82557_BAR_KIND,
        E82557_BAR_ALIGN, 0, E82557_BAR_RANGE, E82557_BAR_RANGE, E82557_CMD_IO};
    const char *names[] = {"ok", "bar-zero", "bar-kind", "bar-kind", "bar-kind", "bar-align",
        "ok", "bar-range", "bar-range", "cmd-io"};
    struct pci_dev d = device();
    u16 base = 0x1234;
    int reason = -1;
    d.bar[1] = raw[index];
    if (index == 9) d.command = 0x146;
    CHECK(e82557_io_window(&d, &base, &reason) == (why[index] == 0));
    CHECK(reason == why[index]);
    CHECK(strcmp(e82557_window_name(reason), names[index]) == 0);
    CHECK(base == (why[index] == 0 ? (raw[index] & ~3u) : 0x1234));
}
static void frame_case(void)
{
    u32 bits = 0; int n = 0;
    CHECK(e82557_ee_frame(6, 2, &bits, &n));
    CHECK(bits == 0x182 && n == 9); /* 110 000010 */
    CHECK(e82557_ee_frame(8, 0xA5, &bits, &n));
    CHECK(bits == 0x6A5 && n == 11);
    CHECK(!e82557_ee_frame(7, 0, &bits, &n));
    CHECK(!e82557_ee_frame(6, 64, &bits, &n));
}
static void auto_case(int width, int falling, int stuck)
{
    struct e82557_ee_pins p = pins();
    u16 word = 0xDEAD; int w = -1, dummy = -2;
    int k = stuck == 0 ? 1 : stuck == 1 ? 8 : width;
    int rc = stuck == 1 ? E82557_NOWIN : (k == 6 || k == 8) ? E82557_OK : E82557_WIDTH;
    reset_model(width, falling, stuck);
    int result = e82557_ee_read_word(&p, E82557_EE_AUTO, &w, 0, &word, &dummy);
    CHECK(result == rc);
    CHECK(w == (stuck == 1 ? 0 : k));
    CHECK(dummy == (stuck == 1 ? 1 : 0));
    expected_transaction(8, 0, 1, k, 4);
    verify();
}
static void read_case(int width, int falling, int stuck, int fill)
{
    struct e82557_ee_pins p = pins();
    u16 buf[5] = {0xCDEF, 0xCDEF, 0xCDEF, 0xCDEF, 0xCDEF};
    u8 mac[6];
    int i, dummy, w = width;
    reset_model(width, falling, stuck);
    if (fill >= 0) for (i = 0; i < 3; i++) m.words[i] = (u16)fill;
    for (i = 0; i < 3; i++) {
        u16 want = stuck < 0 ? m.words[i] : stuck ? 0xFFFF : 0;
        int rc = e82557_ee_read_word(&p, E82557_EE_FIXED, &w, (u16)i,
                                    &buf[i + 1], &dummy);
        CHECK(rc == E82557_OK);
        CHECK(dummy == (stuck == 1 ? 1 : 0));
        CHECK(w == width && buf[i + 1] == want);
        expected_transaction(width, i, 0, 0, 4);
    }
    CHECK(m.transactions == 3);
    CHECK(buf[0] == 0xCDEF && buf[4] == 0xCDEF);
    e82557_ee_mac(buf + 1, mac);
    if (stuck == 1 || fill == 0xFFFF) CHECK(e82557_mac_form(mac) == E82557_FF);
    else if (stuck == 0 || fill == 0) CHECK(e82557_mac_form(mac) == E82557_ZERO);
    else { const u8 expected_mac[] = {0, 0, 0x4C, 0x12, 0x34, 0x56};
        CHECK(memcmp(mac, expected_mac, 6) == 0); CHECK(e82557_mac_form(mac) == E82557_OK); }
    verify();
}
static void interpretation_case(void)
{
    u8 zero[6] = {0}, ff[6] = {255,255,255,255,255,255}, multi[6] = {1,0,0,0,0,0};
    u8 local[6] = {2,0,0,0,0,0};
    CHECK(e82557_mac_form(zero) == E82557_ZERO); CHECK(e82557_mac_form(ff) == E82557_FF);
    CHECK(e82557_mac_form(multi) == E82557_MCAST); CHECK(e82557_mac_form(local) == E82557_OK);
}
int main(int argc, char **argv)
{
    const char *c;
    if (argc < 2) return 2;
    c = argv[1]; if (argc > 2) only = atoi(argv[2]);
    if (!strncmp(c,"window",6)) window_case(atoi(c+6));
    else if (!strcmp(c,"frame")) frame_case();
    else if (!strcmp(c,"auto6rise")) auto_case(6,0,-1);
    else if (!strcmp(c,"auto6fall")) auto_case(6,1,-1);
    else if (!strcmp(c,"auto8rise")) auto_case(8,0,-1);
    else if (!strcmp(c,"auto8fall")) auto_case(8,1,-1);
    else if (!strcmp(c,"nowin")) auto_case(6,1,1);
    else if (!strcmp(c,"width1")) auto_case(6,1,0);
    else if (!strcmp(c,"width7")) auto_case(7,1,-1);
    else if (!strcmp(c,"read6rise")) read_case(6,0,-1,-1);
    else if (!strcmp(c,"read6fall")) read_case(6,1,-1,-1);
    else if (!strcmp(c,"read8rise")) read_case(8,0,-1,-1);
    else if (!strcmp(c,"read8fall")) read_case(8,1,-1,-1);
    else if (!strcmp(c,"erased")) read_case(6,1,-1,0xFFFF);
    else if (!strcmp(c,"empty")) read_case(6,1,-1,0);
    else if (!strcmp(c,"stuck1fixed")) read_case(6,1,1,-1);
    else if (!strcmp(c,"stuck0fixed")) read_case(6,1,0,-1);
    else if (!strcmp(c,"interpret")) interpretation_case();
    else return 2;
    return !!failed;
}
