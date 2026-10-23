/* Real kprintf -> real console -> serial capture, with rendering suppressed. */
#include "../../lib/kprintf_attr.c"
#include "../../kernel/console.c"
static char serial_output[4096];
static u32 serial_length;
int serial_putchar(char c)
{
    if (serial_length + 1 < sizeof(serial_output)) {
        serial_output[serial_length++] = c;
        serial_output[serial_length] = 0;
    }
    return 0;
}
int v86_is_active(void) { return 1; }
int con_sink_is_enabled(void) { return 0; }
void con_sink_push_print(const char *s, u32 n, u8 c) { (void)s; (void)n; (void)c; }
void bootlog_push(const char *s, u32 n) { (void)s; (void)n; }
u32 kstrlen(const char *s) { u32 n=0; while(s[n]) n++; return n; }
utf8_decode_t utf8_decode(const u8 *s) { return (utf8_decode_t){*s, 1}; }
u8 unicode_to_ank(u32 cp) { return (u8)cp; }
u16 unicode_to_jis(u32 cp) { (void)cp; return 0; }
static int equal(const char *a, const char *b)
{ while (*a && *a==*b) {a++;b++;} return *a==*b; }
static int contains(const char *a, const char *b)
{ for (;*a;a++) {u32 n=0;while(b[n] && a[n]==b[n])n++;if(!b[n])return 1;}return 0; }
static void clear_output(void) { serial_length=0;serial_output[0]=0;rshell_active=1; }
