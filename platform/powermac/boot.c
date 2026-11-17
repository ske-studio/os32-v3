/* Independent ppc-boot: firmware owns console and mapping until the direct
 * ESCC phase. No firmware call is made after successful direct output. */
#include "ofw.h"
#include "rh_observe.h"
#include "kstring.h"
static void append(char *text, unsigned cap, const char *part)
{
    unsigned n = kstrlen(text);
    kstrncpy(text+n,part,cap-n);
}
static void fail(int out, const char *stage, int rc)
{
    char text[PM_FAIL_CAP], digits[U32_DEC_BUFSIZE];
    unsigned n = 0, value = rc < 0 ? 0U-(unsigned)rc : (unsigned)rc;
    kstrncpy(text,"FAIL:",sizeof(text));
    append(text,sizeof(text),stage);
    append(text,sizeof(text),":");
    if (rc < 0) append(text,sizeof(text),"-");
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    unsigned pos = kstrlen(text);
    while (n) text[pos++] = digits[--n];
    text[pos] = 0;
    append(text,sizeof(text),"\r\n");
    ofw_write(out,text,kstrlen(text));
}
void powermac_boot(ppc_ci ci)
{
    struct ofw_env e;
    struct pm_escc s;
    ofw_init(ci);
    int rc = ofw_setup(&e);
    if (rc) {
        if (rc == -2) fail(e.out,"environment",rc);
        ofw_exit();
        ppc_stop();
    }
    pm_rh_of_mark(0);
    int fb_rc = pm_rh_capture(e.out);
    if (fb_rc < 0) ppc_stop();
    pm_rh_of_mark(1);
    static const char greeting[] = "OF: os32 ppc-boot\r\n";
    if (ofw_write(e.out,greeting,sizeof(greeting)-1) != sizeof(greeting)-1) {
        fail(e.out,"stdout",-1); ppc_stop();
    }
    rc = pm_escc_prepare(&e,&s);
    if (rc) {
        pm_rh_of_mark(PM_RH_ESCC_PREPARE);
        fail(e.out,rc == -1 ? "tree" : rc == -2 ? "open" : "map",rc);
        ppc_stop();
    }
    pm_rh_of_mark(2);
    ppc_disable();
    ppc_sync();
    static const char direct[] = "ESCC-A: os32 ppc-boot\r\n";
    rc = pm_escc_write(&s,direct,sizeof(direct)-1);
    if (rc) { fail(e.out,"tx",rc); ppc_stop(); }
    rc = pm_escc_drain(&s);
    if (rc) { fail(e.out,"drain",rc); ppc_stop(); }
    ppc_sync();
    ppc_stop();
}
