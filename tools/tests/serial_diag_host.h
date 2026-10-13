#ifndef OS32_SERIAL_DIAG_HOST_H
#define OS32_SERIAL_DIAG_HOST_H
/* Shared freestanding serial sink. Track calls as well as bytes so a gate
 * violation cannot hide behind an empty fragment. */
static char host_serial[16384];
static unsigned int host_serial_len, host_serial_calls;
static int host_serial_gate;
#ifdef HOST_SERIAL_CUSTOM
extern void serial_puts_polled(const char *s);
extern int serial_gate_active(void);
#else
void serial_puts_polled(const char *s)
{
    host_serial_calls++;
    while (*s && host_serial_len + 1 < sizeof(host_serial))
        host_serial[host_serial_len++] = *s++;
    host_serial[host_serial_len] = 0;
}
int serial_gate_active(void) { return host_serial_gate; }
#endif
static void __attribute__((unused)) host_serial_reset(void)
{
    host_serial_len = host_serial_calls = 0;
    host_serial[0] = 0;
}
static const char * __attribute__((unused)) host_serial_find(const char *needle)
{
    for (unsigned int i = 0; i < host_serial_len; i++) {
        unsigned int j = 0;
        while (needle[j] && host_serial[i+j] == needle[j]) j++;
        if (!needle[j]) return host_serial+i;
    }
    return 0;
}
static int __attribute__((unused)) host_serial_count(const char *needle)
{
    int count = 0;
    for (unsigned int i = 0; i < host_serial_len; i++) {
        unsigned int j = 0;
        while (needle[j] && host_serial[i+j] == needle[j]) j++;
        if (!needle[j]) count++;
    }
    return count;
}
static unsigned int __attribute__((unused)) host_serial_value(const char *label)
{
    const char *p = host_serial_find(label);
    unsigned int n = 0;
    if (!p) return ~0U;
    while (*label++) p++;
    while (*p >= '0' && *p <= '9') n = n*10 + (*p++ - '0');
    return n;
}
static unsigned int __attribute__((unused)) host_serial_hex_value(const char *label)
{
    const char *p = host_serial_find(label);
    unsigned int n = 0;
    if (!p) return ~0U;
    while (*label++) p++;
    for (unsigned int i=0;i<8;i++) {
        unsigned int digit = *p >= 'a' && *p <= 'f' ? *p-'a'+10 : *p-'0';
        if (digit > 15) return ~0U;
        n = (n<<4) | digit; p++;
    }
    return n;
}
static int __attribute__((unused)) host_serial_transition(const char *label, unsigned int before, unsigned int after)
{
    const char *p = host_serial_find(label);
    unsigned int n = 0;
    if (!p) return 0;
    while (*label++) p++;
    while (*p >= '0' && *p <= '9') n = n*10 + (*p++ - '0');
    if (n != before || *p++ != '-' || *p++ != '>') return 0;
    n = 0;
    while (*p >= '0' && *p <= '9') n = n*10 + (*p++ - '0');
    return n == after;
}
#endif
