/* Real EEPROM implementation + probe, with a clocked 93C46/93C66 model.
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
static void *memchr(const void *src, int v, size_t n)
{
    const unsigned char *p = src;
    while (n--) { if (*p == (unsigned char)v) return (void *)p; p++; }
    return (void *)0;
}
static int strncmp(const char *a, const char *b, size_t n)
{
    while (n--) { if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return 0;
}
static int strcmp(const char *a, const char *b) { return strncmp(a, b, strlen(a) + 1); }
static char *strstr(const char *s, const char *part)
{
    size_t n = strlen(part);
    do { if (!strncmp(s, part, n)) return (char *)s; } while (*s++);
    return (char *)0;
}
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
static int snprintf(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap; int n;
    va_start(ap, fmt); n = vsnprintf(dst, cap, fmt, ap); va_end(ap);
    return n;
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
#include "../../drivers/pci_bind_match.c"
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
    int width, falling, stuck, clocks, addr, prefix, do_bit, transactions, lose_after;
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
        if (q->lose_after && q->transactions > q->lose_after) q->stuck = 1;
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
    unsigned int i, sum = 0;
    memset(&m, 0, sizeof(m));
    m.width = width; m.falling = falling; m.stuck = stuck; m.do_bit = 1;
    for (i = 0; i < (1u << width); i++) {
        m.words[i] = (u16)(i * 313u + 0x8137u);
        if (i == 0) m.words[i] = 0x0000;
        if (i == 1) m.words[i] = 0x124C;
        if (i == 2) m.words[i] = 0x5634;
        if (i + 1 < (1u << width)) sum += m.words[i];
    }
    m.words[(1 << width) - 1] = (u16)(0xBABA - sum);
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

/* Probe is compiled unchanged except its platform headers. Its I/O callbacks
 * go through this model; no host in/out instruction is executed. */
static int port_errors, output_count, serial_count, gate, begin_seen, end_seen;
static int last_write_end, serial_end_after_cleanup;
static char logs[40][E82557_R5_LINE_CAP];
static u8 inp(u16 port) { if (port != 0x600E) port_errors++; return model_read(&m); }
static void outp(u16 port, u8 value) { if (port != 0x600E) port_errors++; model_write(&m, value); }
static void cpu_delay_us(u32 us) { model_delay(&m, us); }
static int serial_gate_active(void) { return gate; }
static void serial_puts_polled(const char *line)
{
    serial_count++;
    if (strstr(line, "R5 begin")) { CHECK(m.writes == 0 && m.reads == 0); begin_seen++; }
    if (strstr(line, "R5 end")) { end_seen++; serial_end_after_cleanup = m.pin == 0 && m.writes > 0; }
}
static void kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    (void)attr;
    CHECK(output_count < 40);
    if (output_count >= 40) return;
    va_start(ap, fmt);
    vsnprintf(logs[output_count++], E82557_R5_LINE_CAP, fmt, ap);
    va_end(ap);
    last_write_end = m.pin == 0;
}
#define E82557_HOST_TEST
#include "../../drivers/e82557.c"

/* Execute the actual empty enumeration path too (NP21/W regression). */
#define __KPRINTF_H
static int fake_count;
static struct pci_dev fake_devs[2];
int pci_count(void) { return fake_count; }
int pci_get(u32 idx, void *out)
{
    if (idx >= (u32)fake_count) return -1;
    *(struct pci_dev *)out = fake_devs[idx];
    return 0;
}
#include "../../drivers/pci_bind.c"

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
    if (why[index]) {
        reset_model(6, 0, -1);
        CHECK(e82557_probe(&d) == PCI_PROBE_DECLINE);
        CHECK(m.writes == 0 && m.reads == 0 && na == 0);
        CHECK(strstr(logs[0], names[index]) != NULL);
    }
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
    CHECK(e82557_ee_read_word(&p, E82557_EE_AUTO, &w, 0, &word, &dummy) == rc);
    CHECK(w == (stuck == 1 ? 0 : k));
    CHECK(dummy == (stuck == 1 ? 1 : 0));
    expected_transaction(8, 0, 1, k, 4);
    verify();
}
static void read_case(int width, int falling, int stuck, int fill)
{
    struct e82557_ee_pins p = pins();
    u16 buf[66], sum = 0, want_sum = 0;
    int corrupt = fill == -2;
    u8 mac[6]; int i, dummy;
    reset_model(width, falling, stuck);
    if (fill >= 0) for (i = 0; i < (1 << width); i++) m.words[i] = (u16)fill;
    if (corrupt) m.words[(1 << width) - 1]++;
    for (i = 0; i < 66; i++) buf[i] = 0xCDEF;
    dummy = e82557_ee_read_all(&p, width, buf + 1, 64, &sum);
    CHECK(dummy == (stuck == 1 ? 1 : 0));
    for (i = 0; i < (1 << width); i++) {
        u16 want = stuck < 0 ? m.words[i] : stuck ? 0xFFFF : 0;
        want_sum = (u16)(want_sum + want);
        if (i < 64) CHECK(buf[i + 1] == want);
        expected_transaction(width, i, 0, 0, 4);
    }
    CHECK(buf[0] == 0xCDEF && buf[65] == 0xCDEF);
    CHECK(sum == want_sum);
    e82557_ee_mac(buf + 1, mac);
    if (stuck == 1 || fill == 0xFFFF) CHECK(e82557_mac_form(mac) == E82557_FF);
    else if (stuck == 0 || fill == 0) CHECK(e82557_mac_form(mac) == E82557_ZERO);
    else { const u8 expected_mac[] = {0, 0, 0x4C, 0x12, 0x34, 0x56};
        CHECK(memcmp(mac, expected_mac, 6) == 0); CHECK(e82557_mac_form(mac) == E82557_OK);
        CHECK(e82557_ee_checksum_ok(sum) == !corrupt); }
    verify();
}
static void interpretation_case(void)
{
    u8 zero[6] = {0}, ff[6] = {255,255,255,255,255,255}, multi[6] = {1,0,0,0,0,0};
    u8 local[6] = {2,0,0,0,0,0};
    CHECK(e82557_mac_form(zero) == E82557_ZERO); CHECK(e82557_mac_form(ff) == E82557_FF);
    CHECK(e82557_mac_form(multi) == E82557_MCAST); CHECK(e82557_mac_form(local) == E82557_OK);
    CHECK(e82557_ee_checksum_ok(0xBABA)); CHECK(!e82557_ee_checksum_ok(0xBABB));
}
/* Six table rows, plus both S(A) detection failures, executed through real
 * observe/next/verdict rather than a test-side copy of the scheduler. */
static void story_case(int story)
{
    struct e82557_observation obs[3];
    struct e82557_ee_pins p = pins();
    const int methods[][3] = {{E82557_A,0,0},{E82557_A,E82557_SA,0},
        {E82557_A,E82557_SA,0},{E82557_A,E82557_F,0},
        {E82557_A,E82557_F,E82557_SF},{E82557_A,E82557_F,E82557_SF},
        {E82557_A,E82557_SA,0},{E82557_A,E82557_SA,0}};
    const int counts[] = {1,2,2,2,3,3,2,2};
    const int forms[] = {E82557_OK,E82557_OK,E82557_MCAST,E82557_OK,E82557_OK,
        E82557_ZERO,E82557_NOWIN,E82557_WIDTH};
    u16 buf[64]; int n = 0, method, i, should_pass = story==0||story==1||story==3||story==4;
    while ((method = e82557_r5_next(obs, n)) != E82557_STOP) {
        int stuck = -1, width = 6, fill = -1;
        unsigned int step = method == E82557_SA || method == E82557_SF ? 16 : 4;
        CHECK(n < counts[story]);
        if (n >= 3 || n >= counts[story]) break;
        CHECK(method == methods[story][n]);
        if (story == 0) { /* default valid NEC image */ }
        else if (n == 0) { if (story >= 3 && story <= 5) stuck = 1; else fill = 0xFFFF; }
        else if (story == 2) fill = 0x0101;
        else if (story == 4 && n == 1) fill = 0;
        else if (story == 5) fill = 0;
        else if (story == 6) stuck = 1;
        else if (story == 7) width = 7;
        reset_model(width, 1, stuck);
        if (fill >= 0) for (i = 0; i < (1 << width); i++) m.words[i] = (u16)fill;
        e82557_r5_observe(&p, method, buf, &obs[n]);
        if (method == E82557_A || method == E82557_SA)
            expected_transaction(8, 0, 1, stuck == 1 ? 8 : width, step);
        if (stuck != 1 || method == E82557_F || method == E82557_SF) {
            if (width == 6 || width == 8) for (i = 0; i < (1 << width); i++)
                expected_transaction(width, i, 0, 0, step);
        }
        verify(); n++;
    }
    CHECK(n == counts[story]);
    CHECK(obs[n-1].form == forms[story]);
    CHECK(e82557_r5_verdict(obs, n) == should_pass);
    { char line[192], want[64];
      e82557_r5_final_line(line, sizeof(line), obs, n);
      if (should_pass) CHECK(strstr(line, "R5=PASS") != NULL);
      else { snprintf(want, sizeof(want), "R5=FAIL(%s) tries=%d", ee_form(forms[story]), n);
          CHECK(strstr(line, want) != NULL); }
    }
}
static void format_case(void)
{
    struct e82557_observation o = {E82557_SA,8,0,E82557_OK,0,{0,0,0x4C,0x01,0xA0,0xFF}};
    char line[192], tiny[192]; u16 words[64]; int i, cap;
    e82557_r5_observation_line(line, sizeof(line), &o, 2);
    CHECK(strcmp(line, "[e82557] try2 S(A) d16: addr=8 n=256 dummy=0 sum=bad mac=00:00:4c:01:a0:ff form=ok oui=NEC") == 0);
    e82557_r5_final_line(line, sizeof(line), &o, 1);
    CHECK(strcmp(line, "[e82557] R5=PASS try=1 addr=8 mac=00:00:4c:01:a0:ff sum=bad") == 0);
    for (i = E82557_ZERO; i <= E82557_WIDTH; i++) {
        o.form = i; o.width = i == E82557_NOWIN ? 0 : i == E82557_WIDTH ? 7 : 6;
        e82557_r5_final_line(line, sizeof(line), &o, 1);
        CHECK(strstr(line, ee_form(i)) != NULL && strstr(line, "tries=1") != NULL);
    }
    o.form = E82557_NOWIN; o.width = 0;
    e82557_r5_observation_line(line, sizeof(line), &o, 2);
    CHECK(strcmp(line, "[e82557] try2 S(A) d16: addr=- n=- dummy=- sum=- mac=- form=nowin oui=?") == 0);
    o.width = 7; o.form = E82557_WIDTH;
    e82557_r5_observation_line(line, sizeof(line), &o, 2);
    CHECK(strstr(line, "addr=7 n=- dummy=0 sum=- mac=- form=width") != NULL);
    for (i = 0; i < 64; i++) words[i] = (u16)(i * 0x101);
    for (i = 0; i < 64; i += 16) {
        char prefix[30]; snprintf(prefix, sizeof(prefix), "[e82557] ee %02x:", i);
        e82557_r5_dump_line(line, sizeof(line), words, i, 8);
        CHECK(strncmp(line, prefix, strlen(prefix)) == 0);
        CHECK(strstr(line, " dump=64/256") != NULL);
        CHECK(strlen(line) == strlen(prefix) + 16 * 5 + strlen(" dump=64/256"));
    }
    e82557_r5_dump_line(line, sizeof(line), words, 0, 6);
    CHECK(strstr(line, "dump=") == NULL && strstr(line, " 0000 0101 0202") != NULL);
    e82557_r5_begin_line(line, sizeof(line), 0x6000);
    CHECK(strcmp(line, "[e82557] R5 begin io=0x6000") == 0);
    /* Every builder honors cap, including cap=0 and cap=1. */
    for (cap = 0; cap < 120; cap++) {
        memset(tiny, 'Z', sizeof(tiny)); e82557_r5_observation_line(tiny + 1, cap, &o, 3);
        CHECK(tiny[0] == 'Z' && tiny[cap+1] == 'Z'); if (cap) CHECK(memchr(tiny+1, 0, cap));
        memset(tiny, 'Z', sizeof(tiny)); e82557_r5_final_line(tiny + 1, cap, &o, 1);
        CHECK(tiny[0] == 'Z' && tiny[cap+1] == 'Z'); if (cap) CHECK(memchr(tiny+1, 0, cap));
        memset(tiny, 'Z', sizeof(tiny)); e82557_r5_dump_line(tiny + 1, cap, words, 48, 8);
        CHECK(tiny[0] == 'Z' && tiny[cap+1] == 'Z'); if (cap) CHECK(memchr(tiny+1, 0, cap));
        memset(tiny, 'Z', sizeof(tiny)); e82557_r5_begin_line(tiny + 1, cap, 0x6000);
        CHECK(tiny[0] == 'Z' && tiny[cap+1] == 'Z'); if (cap) CHECK(memchr(tiny+1, 0, cap));
    }
    CHECK(!e82557_r5_verdict(&o, 0));
}
static void probe_case(int gated)
{
    struct pci_dev d = device(); int i;
    reset_model(6, 1, -1); gate = gated;
    CHECK(e82557_probe(&d) == PCI_PROBE_DECLINE);
    CHECK(port_errors == 0 && last_write_end && m.pin == 0);
    CHECK(output_count == 8); /* begin + observation + end + final + four dump rows */
    CHECK(strstr(logs[0], "R5 begin io=0x6000"));
    CHECK(strstr(logs[1], "try1 A d4: addr=6 n=64 dummy=0 sum=ok mac=00:00:4c:12:34:56 form=ok oui=NEC"));
    CHECK(strstr(logs[2], "R5 end")); CHECK(strstr(logs[3], "R5=PASS try=1"));
    if (gated) CHECK(serial_count == 0);
    else CHECK(begin_seen == 1 && end_seen == 1 && serial_end_after_cleanup);
    expected_transaction(8, 0, 1, 6, 4);
    for (i = 0; i < 64; i++) expected_transaction(6, i, 0, 0, 4);
    verify();
}
static void probe_failure_case(int lose, int stuck)
{
    struct pci_dev d = device();
    int i, pass;
    reset_model(6, 1, stuck);
    if (lose) { m.lose_after = 65; for (i = 0; i < 64; i++) m.words[i] = 0xFFFF; }
    CHECK(e82557_probe(&d) == PCI_PROBE_DECLINE);
    CHECK(port_errors == 0 && serial_end_after_cleanup);
    if (lose) {
        CHECK(output_count == 5); /* begin, two tries, end, FAIL; no stale dump */
        CHECK(strstr(logs[2], "try2 S(A) d16: addr=-"));
        CHECK(strstr(logs[4], "R5=FAIL(nowin) tries=2"));
        expected_transaction(8, 0, 1, 6, 4);
        for (i = 0; i < 64; i++) expected_transaction(6, i, 0, 0, 4);
        expected_transaction(8, 0, 1, 8, 16);
    } else {
        CHECK(output_count == 10); /* begin + 3 observations + end + FAIL + 4 rows */
        CHECK(strstr(logs[5], stuck ? "R5=FAIL(ff) tries=3" : "R5=FAIL(zero) tries=3"));
        expected_transaction(8, 0, 1, stuck ? 8 : 1, 4);
        for (pass = 0; pass < 2; pass++) for (i = 0; i < 64; i++)
            expected_transaction(6, i, 0, 0, pass ? 16 : 4);
    }
    verify();
}
static void binding_case(void)
{
    const struct pci_driver *table[] = {&e82557_driver};
    struct pci_bind_info info;
    struct pci_dev d = device(); int i;
    const u16 vendors[] = {0x1023, 0x1102}; /* Trident, CA0106 */
    const u16 devices[] = {0x9660, 0x0007};
    reset_model(6, 0, -1);
    /* NP21/W has no enumerated devices: actual binder must produce no I/O/output. */
    fake_count = 0;
    CHECK(pci_bind_all(table, 1) == 0);
    CHECK(pci_bind_info_get(0, &info) == -1);
    CHECK(output_count == 0 && m.writes == 0);
    for (i = 0; i < 2; i++) {
        d.vendor = vendors[i]; d.device = devices[i]; d.class = i ? 4 : 3;
        memset(&info, 0, sizeof(info));
        CHECK(pci_bind_one(table, 1, &d, &info) == PCI_PROBE_DECLINE);
        CHECK(info.result == PCI_BIND_NONE && info.reason == PCI_BIND_NO_DRIVER);
    }
    CHECK(output_count == 0 && m.writes == 0 && m.reads == 0);
    fake_count = 2;
    for (i = 0; i < 2; i++) {
        fake_devs[i] = d; fake_devs[i].vendor = vendors[i]; fake_devs[i].device = devices[i];
        fake_devs[i].class = i ? 4 : 3;
    }
    CHECK(pci_bind_all(table, 0) == 0);
    { struct pci_bind_info before[2], after;
      for (i = 0; i < 2; i++) CHECK(pci_bind_info_get(i, &before[i]) == 0);
      CHECK(pci_bind_all(table, 1) == 0);
      for (i = 0; i < 2; i++) {
          CHECK(pci_bind_info_get(i, &after) == 0);
          CHECK(memcmp(&before[i], &after, sizeof(after)) == 0);
      }
    }
    CHECK(m.writes == 0 && m.reads == 0);
    CHECK(output_count == 2); /* same existing bind summary, once per run */
    d = device();
    CHECK(pci_bind_match(&e82557_driver, &d));
    CHECK(pci_bind_one(table, 1, &d, &info) == PCI_PROBE_DECLINE);
    CHECK(info.result == PCI_BIND_DECLINED);
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
    else if (!strcmp(c,"badchecksum")) read_case(8,1,-1,-2);
    else if (!strcmp(c,"empty")) read_case(6,1,-1,0);
    else if (!strcmp(c,"stuck1fixed")) read_case(6,1,1,-1);
    else if (!strcmp(c,"stuck0fixed")) read_case(6,1,0,-1);
    else if (!strcmp(c,"interpret")) interpretation_case();
    else if (!strncmp(c,"story",5)) story_case(atoi(c+5));
    else if (!strcmp(c,"format")) format_case();
    else if (!strcmp(c,"probe")) probe_case(0);
    else if (!strcmp(c,"gated")) probe_case(1);
    else if (!strcmp(c,"proberelearn")) probe_failure_case(1,-1);
    else if (!strcmp(c,"probeff")) probe_failure_case(0,1);
    else if (!strcmp(c,"probezero")) probe_failure_case(0,0);
    else if (!strcmp(c,"binding")) binding_case();
    else return 2;
    return !!failed;
}
