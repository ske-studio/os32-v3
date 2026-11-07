/* KDBG_PROBE_DESIGN stage C: literals only, separate from HW variables. */
#include "hwb.h"
#include "os32_kapi_shared.h"

struct pp { const char *s; u32 p, end; };
static struct hwb_span pp_token(struct pp *p)
{
    while (p->p < p->end && (p->s[p->p] == ' ' || p->s[p->p] == '\t')) p->p++;
    struct hwb_span t = {p->p, p->p};
    while (p->p < p->end && p->s[p->p] != ' ' && p->s[p->p] != '\t') p->p++;
    t.end = p->p; return t;
}
static int pp_word(struct pp *p, const char *word)
{
    struct pp copy = *p;
    struct hwb_span t = pp_token(&copy);
    u32 n = t.begin;
    while (n < t.end && *word && p->s[n] == *word) { n++; word++; }
    if (n != t.end || *word) return 0;
    *p = copy; return 1;
}
static int pp_number(struct pp *p, u32 *v)
{
    struct hwb_span t = pp_token(p);
    u32 n = t.begin, base = 10, value = 0;
    if (t.end - n > 2 && p->s[n] == '0' && (p->s[n+1] == 'x' || p->s[n+1] == 'X')) { base = 16; n += 2; }
    if (n == t.end) return 0;
    for (; n < t.end; n++) {
        char c = p->s[n];
        u32 d = c >= '0' && c <= '9' ? (u32)(c-'0') :
            c >= 'a' && c <= 'f' ? (u32)(c-'a'+10) :
            c >= 'A' && c <= 'F' ? (u32)(c-'A'+10) : base;
        if (d >= base || value > (U32_MAX-d)/base) return 0;
        value = value*base+d;
    }
    *v = value; return 1;
}
int hwb_probe_parse(const char *source, u32 begin, u32 end, struct probe_wire *w)
{
    struct pp p = {source, begin, end};
    *w = (struct probe_wire){.version=PROBE_VERSION, .size=PROBE_WIRE_BYTES};
    if (pp_word(&p,"reset")) w->action = PROBE_CMD_RESET;
    else if (pp_word(&p,"list")) w->action = PROBE_CMD_LIST;
    else if (pp_word(&p,"stop")) w->action = PROBE_CMD_STOP;
    else if (pp_word(&p,"drain")) w->action = PROBE_CMD_DRAIN;
    else if (pp_word(&p,"arm")) { w->action = PROBE_CMD_ARM; if (pp_word(&p,"serial")) w->flags = PROBE_SERIAL; }
    else if (pp_word(&p,"add")) {
        w->action = PROBE_CMD_ADD;
        struct hwb_span name = pp_token(&p);
        u32 bytes = name.end-name.begin;
        if (!bytes || bytes >= sizeof(w->name)) return 0;
        for (u32 n=0; n<bytes; n++) {
            char c = source[name.begin+n];
            if (!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='.')) return 0;
            w->name[n] = c;
        }
        static const char *const reads[] = {"ioread8","ioread16","ioread32","mmioread8","mmioread16","mmioread32","idxio8","idxmmio8"};
        u32 k;
        for (k=0; k<8; k++) if (pp_word(&p,reads[k])) break;
        if (k==8) return 0;
        w->kind = k<3 ? PROBE_IO : k<6 ? PROBE_MMIO : k==6 ? PROBE_IDXIO : PROBE_IDXMMIO;
        w->width = k<6 ? 1U << (k%3U) : 1;
        if (k<6 && !pp_word(&p,k<3 ? "port" : "phys")) return 0;
        if (!pp_number(&p,&w->address)) return 0;
        if (k>=6 && (!pp_number(&p,&w->data_address) || !pp_number(&p,&w->index))) return 0;
    } else return 0;
    return pp_token(&p).begin == end;
}
static enum hwb_rc pp_rc(i32 rc)
{
    switch (rc) {
    case PROBE_OK: return HWB_OK;
    case PROBE_ARG: return HWB_ARG;
    case PROBE_NOSYS: return HWB_NOSYS;
    case PROBE_WINDOW: return HWB_WINDOW;
    default: return HWB_IO;
    }
}
static enum hwb_rc pp_call(struct hwb_state *s, struct probe_wire *w)
{
    if (!s->backend->probe) return HWB_NOSYS;
    int rc = s->backend->probe(s->backend->ctx,w);
    if (rc) return rc == OS32_ERR_NOSYS ? HWB_NOSYS : HWB_IO;
    if (w->size != PROBE_WIRE_BYTES || w->version != PROBE_VERSION) return HWB_IO;
    return HWB_OK;
}
enum hwb_rc hwb_probe_execute(struct hwb_state *s, const struct hwb_instruction *i)
{
    u32 cursor = 0;
    enum hwb_rc result = HWB_OK;
    struct hwb_log_record r = {.run=s->run,.seq=s->seq,.line=i->line,.op=HWB_PROBE};
    for (;;) {
        struct probe_wire w = i->probe;
        if (w.action == PROBE_CMD_LIST) w.id = cursor++;
        enum hwb_rc rc = pp_call(s,&w);
        if (rc) { r.rc=rc; hwb_log_end(s->backend,&r); return rc; }
        hwb_log_probe(s->backend,&r,&w);
        if (!result && w.status) result = pp_rc(w.status);
        /* Consume every returned item, including failures and SKIP. */
        if ((w.action != PROBE_CMD_LIST && w.action != PROBE_CMD_DRAIN) || !w.more) break;
        r.seq = ++s->seq;
    }
    if (i->probe.action == PROBE_CMD_ADD || i->probe.action == PROBE_CMD_DRAIN) {
        struct probe_wire status = {.version=PROBE_VERSION,.size=PROBE_WIRE_BYTES,.action=PROBE_CMD_STATUS};
        enum hwb_rc rc = pp_call(s,&status);
        if (rc) return rc;
        r.seq = ++s->seq; hwb_log_probe(s->backend,&r,&status);
        if (i->probe.action == PROBE_CMD_DRAIN && !result)
            result = status.dropped || status.overflow ? HWB_IO : pp_rc(status.status);
    }
    return result;
}
