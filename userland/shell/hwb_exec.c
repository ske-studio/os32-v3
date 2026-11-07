/* HW_BASIC_DESIGN §§2-4: shell execution, borrowed storage, backend effects only. */
#include "hwb.h"

/* PCI Local Bus 2.2 §6.2: header layout and BAR encoding. No sizing writes. */
#define HE_HEADER 0x0EU
#define HE_BAR0 0x10U
#define HE_BAR_COUNT 6U
#define HE_BRIDGE_BARS 2U
#define HE_CARDBUS_BARS 1U

/* Process-wide IDs keep interactive records separate from a suspended run. */
static u32 he_run_serial;
#define HE_HEADER_MASK 0x7FU
#define HE_IO_BIT 1U
#define HE_MEMORY_TYPE 6U
#define HE_MEMORY_64 4U
#define HE_IO_MASK (~3UL)
#define HE_MEMORY_MASK (~15UL)

static int he_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void he_name(const struct hwb_state *s, struct hwb_span a, char *name)
{
    u32 n = 0;
    while (a.begin < a.end && n < HWB_NAME_CHARS) name[n++] = s->program->source[a.begin++];
    name[n] = 0;
}
static int he_slot(const struct hwb_state *s, struct hwb_span a)
{
    char name[HWB_NAME_CHARS + 1U]; he_name(s, a, name);
    for (u32 n = 0; n < s->var_count; n++) if (he_equal(name, s->vars[n].name)) return (int)n;
    return -1;
}
static enum hwb_rc he_put_vars(struct hwb_variable *vars, u32 *count, const char *name,
                           enum hwb_value_kind kind, u32 value, u32 device, u32 bar)
{
    u32 n = 0;
    while (n < *count && !he_equal(vars[n].name, name)) n++;
    if (n == HWB_VARIABLES) return HWB_ARG;
    if (n < *count && vars[n].kind == HWB_ARRAY) return HWB_ARG;
    if (n == *count) (*count)++;
    struct hwb_variable *v = &vars[n];
    u32 k = 0; do { v->name[k] = name[k]; } while (name[k++]);
    v->kind = kind; v->value = value; v->device_slot = device; v->bar = bar;
    return HWB_OK;
}
static enum hwb_rc he_put(struct hwb_state *s, const char *name,
                            enum hwb_value_kind kind, u32 value, u32 device, u32 bar)
{
    return he_put_vars(s->vars, &s->var_count, name, kind, value, device, bar);
}
/* Resolve the destination before data access; retain the selected element even
 * when the read result changes a variable used by the subscript. */
static enum hwb_rc he_destination(struct hwb_state *s, const struct hwb_instruction *i,
                                  struct hwb_log_record *r, u32 **element)
{
    if (i->index.begin == i->index.end) {
        int slot = he_slot(s, i->name);
        if (slot < 0) return s->var_count == HWB_VARIABLES ? HWB_ARG : HWB_OK;
        return s->vars[slot].kind == HWB_ARRAY ? HWB_ARG : HWB_OK;
    }
    r->indexed = 1;
    if (hwb_eval(s, i->index, &r->index)) return HWB_ARG;
    int slot = he_slot(s, i->name);
    if (slot < 0 || s->vars[slot].kind != HWB_ARRAY) return HWB_ARG;
    struct hwb_variable *v = &s->vars[slot];
    if (r->index >= v->array_count) return HWB_ARG;
    *element = &s->array_values[v->array_start + r->index];
    return HWB_OK;
}
static enum hwb_rc he_assign(struct hwb_state *s, struct hwb_span a,
                              enum hwb_value_kind kind, u32 value, u32 device, u32 bar)
{
    char name[HWB_NAME_CHARS + 1U]; he_name(s, a, name);
    return he_put(s, name, kind, value, device, bar);
}
static enum hwb_rc he_point(struct hwb_state *s, int discard)
{
    u32 flag = 0;
    if (s->backend->point(s->backend->ctx, &flag)) return HWB_IO;
    return flag && !discard ? HWB_ABORT : HWB_OK;
}
static enum hwb_rc he_access(struct hwb_state *s, u32 action, u32 space,
                              u32 width, u32 bdf, u32 addr, u32 *value)
{
    struct hw_req req = {HW_VERSION, action, space, width, bdf, addr,
                         action == HW_WRITE ? *value : 0, 0};
    if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;
    if (req.status > HW_IO_ERROR) return HWB_IO;
    if (req.status) return (enum hwb_rc)req.status;
    if (action == HW_READ) *value = req.value;
    return HWB_OK;
}
static enum hwb_rc he_bar(struct hwb_state *s, u32 slot, u32 number,
                           u32 space, u32 *base)
{
    if (slot >= s->var_count || s->vars[slot].kind != HWB_DEVICE || number >= HE_BAR_COUNT)
        return HWB_ARG;
    u32 bdf = s->vars[slot].value, header = 0;
    enum hwb_rc rc = he_access(s, HW_READ, HW_PCI, 1, bdf, HE_HEADER, &header);
    if (rc) return rc;
    header &= HE_HEADER_MASK;
    u32 count = header == 0 ? HE_BAR_COUNT : header == 1 ? HE_BRIDGE_BARS :
                header == 2 ? HE_CARDBUS_BARS : 0;
    if (number >= count) return HWB_ARG;
    for (u32 n = 0; n <= number; n++) {
        u32 low = 0;
        rc = he_access(s, HW_READ, HW_PCI, 4, bdf, HE_BAR0 + n * 4U, &low);
        if (rc) return rc;
        int io = !!(low & HE_IO_BIT);
        u32 type = low & HE_MEMORY_TYPE;
        if (!io && type != 0 && type != 2U && type != HE_MEMORY_64) return HWB_ARG;
        if (!io && type == HE_MEMORY_64) {
            if (n + 1U >= count || number == n + 1U) return HWB_ARG;
            u32 high = 0;
            rc = he_access(s, HW_READ, HW_PCI, 4, bdf, HE_BAR0 + (n + 1U) * 4U, &high);
            if (rc) return rc;
            if (n == number && high) {
                s->backend->line(s->backend->ctx, "HW BAR above 32-bit physical address space rc=ARG");
                return HWB_ARG;
            }
            if (n != number) { n++; continue; }
        }
        if (n == number) {
            if ((space == HW_IO) != io) return HWB_ARG;
            *base = low & (io ? HE_IO_MASK : HE_MEMORY_MASK);
            return HWB_OK;
        }
    }
    return HWB_ARG;
}
static enum hwb_rc he_address(struct hwb_state *s, const struct hwb_instruction *i,
                               struct hwb_log_record *r)
{
    const struct hwb_address *a = &i->address;
    u32 offset, base = 0;
    enum hwb_rc rc = hwb_eval(s, a->offset, &offset);
    if (rc) return rc;
    if (a->kind == HWB_ADDR_PCI || a->kind == HWB_ADDR_BAR || a->kind == HWB_ADDR_WINDOW) {
        int slot = he_slot(s, a->device);
        if (slot < 0) return HWB_ARG;
        if (a->kind == HWB_ADDR_PCI) {
            if (s->vars[slot].kind != HWB_DEVICE) return HWB_ARG;
            r->bdf = s->vars[slot].value;
        } else {
            u32 bar;
            if (a->kind == HWB_ADDR_WINDOW) {
                if (s->vars[slot].kind != HWB_WINDOW_REF) return HWB_ARG;
                bar = s->vars[slot].bar; slot = (int)s->vars[slot].device_slot;
            } else { rc = hwb_eval(s, a->bar, &bar); if (rc) return rc; }
            rc = he_bar(s, (u32)slot, bar, i->space, &base);
            if (rc) return rc;
        }
    }
    if (offset > U32_MAX - base) return HWB_ARG;
    r->addr = base + offset;
    u32 limit = i->space == HW_PCI ? HW_PCI_CONFIG_BYTES - 1U :
                i->space == HW_IO ? U16_MAX : U32_MAX;
    if (r->addr > limit || i->width - 1U > limit - r->addr) return HWB_ARG;
    if (i->space != HW_IO && (r->addr & (i->width - 1U))) return HWB_ARG;
    return HWB_OK;
}
static int he_fits(u32 value, u32 width)
{
    return width == 4 || !(value >> (width * BITS_PER_BYTE));
}
static enum hwb_rc he_time(struct hwb_state *s, unsigned long long *now,
                            unsigned long long *previous)
{
    u32 lo, hi;
    if (s->backend->time_now(s->backend->ctx, &lo, &hi)) return HWB_IO;
    *now = ((unsigned long long)hi << 32) | lo;
    if (*now < *previous) return HWB_IO;
    *previous = *now; return HWB_OK;
}
/* One budget covers all waiting iterations and poll reads, including frozen clocks. */
static enum hwb_rc he_wait_poll(struct hwb_state *s, const struct hwb_instruction *i,
                                 struct hwb_log_record *r, struct hwb_poll_result *p)
{
    int poll = i->op == HWB_POLL;
    u32 duration = r->requested_us, interval = 0, mask = 0, wanted = 0;
    enum hwb_rc rc;
    if (poll) {
        if (hwb_eval(s, i->interval, &interval) || hwb_eval(s, i->mask, &mask) ||
            hwb_eval(s, i->value, &wanted)) return HWB_ARG;
        if (!interval || interval > duration || !he_fits(mask, i->width) ||
            !he_fits(wanted, i->width) || (wanted & ~mask)) return HWB_ARG;
    }
    unsigned long long now = 0, previous = 0, start, last = 0;
    rc = he_time(s, &now, &previous); if (rc) return rc;
    start = now;
    for (u32 n = 0; n < HWB_ITERATIONS; n++) {
        rc = he_point(s, 0); if (rc) return rc;
        rc = he_time(s, &now, &previous); if (rc) return rc;
        unsigned long long elapsed_us = now - start;
        p->elapsed_lo = r->elapsed_lo = (u32)elapsed_us;
        p->elapsed_hi = r->elapsed_hi = (u32)(elapsed_us >> 32);
        if (!poll && elapsed_us >= duration) return HWB_OK;
        /* Poll is immediate once, then never reads again at/after its deadline. */
        if (poll && p->reads && elapsed_us >= duration) return HWB_TIMEOUT;
        if (poll && (!p->reads || now - last >= interval)) {
            rc = he_point(s, 0); if (rc) return rc;
            rc = he_address(s, i, r); if (rc) return rc;
            rc = he_access(s, HW_READ, i->space, i->width, r->bdf, r->addr, &p->value);
            if (rc) return rc;
            p->reads++; r->value = p->value; last = now;
            if ((p->value & mask) == wanted) return HWB_OK;
        }
        s->backend->yield(s->backend->ctx);
    }
    return HWB_TIMEOUT;
}
/* BDF/ID components were syntax checked as literals, ID uses unprefixed hex. */
static u32 he_literal(const struct hwb_state *s, struct hwb_span a, u32 base)
{
    const char *p = s->program->source;
    if (a.end - a.begin >= 2U && p[a.begin] == '0' &&
        (p[a.begin + 1U] == 'x' || p[a.begin + 1U] == 'X')) { a.begin += 2U; base = 16; }
    u32 value = 0;
    while (a.begin < a.end) {
        char c = p[a.begin++];
        u32 d = c <= '9' ? (u32)(c - '0') : c <= 'F' ? (u32)(c - 'A' + 10) : (u32)(c - 'a' + 10);
        value = value * base + d;
    }
    return value;
}
static enum hwb_rc he_execute(struct hwb_state *s, const struct hwb_instruction *i,
                               struct hwb_log_record *r, u32 *next, u32 *element)
{
    u32 a = 0, b = 0, c = 0;
    enum hwb_rc rc;
    switch (i->op) {
    case HWB_DIM: {
        rc = hwb_eval(s, i->value, &a); if (rc) return rc;
        if (!a || a > HWB_ARRAY_ELEMENTS - s->array_used || he_slot(s, i->name) >= 0)
            return HWB_ARG;
        rc = he_assign(s, i->name, HWB_ARRAY, 0, 0, 0); if (rc) return rc;
        struct hwb_variable *v = &s->vars[s->var_count - 1U];
        v->array_start = s->array_used; v->array_count = a;
        for (u32 n = 0; n < a; n++) s->array_values[s->array_used + n] = 0;
        s->array_used += a; r->value = a;
        return HWB_OK;
    }
    case HWB_LET:
        a = r->value;
        if (element) { *element = a; return HWB_OK; }
        return he_assign(s, i->name, HWB_SCALAR, a, 0, 0);
    case HWB_DEVICE_BDF:
        a = he_literal(s, i->bus, 10); b = he_literal(s, i->dev, 10); c = he_literal(s, i->fn, 10);
        if (a > HW_BDF_BUS_MASK || b > HW_BDF_DEV_MASK || c > HW_BDF_FN_MASK) return HWB_ARG;
        return he_assign(s, i->name, HWB_DEVICE, HW_BDF(a,b,c), 0, 0);
    case HWB_DEVICE_ID: {
        a = he_literal(s, i->vendor, 16); b = he_literal(s, i->device, 16);
        if (a > U16_MAX || b > U16_MAX) return HWB_ARG;
        u32 candidates[HWB_VARIABLES];
        int total = s->backend->find_id(s->backend->ctx, a, b, 0, candidates, HWB_VARIABLES);
        if (total < 0) return HWB_IO;
        if (total == 1) return he_assign(s, i->name, HWB_DEVICE, candidates[0], 0, 0);
        for (u32 start = 0; start < (u32)total; start += HWB_VARIABLES) {
            if (start && s->backend->find_id(s->backend->ctx, a, b, start, candidates, HWB_VARIABLES) != total)
                return HWB_IO;
            for (u32 n = 0; n < HWB_VARIABLES && start + n < (u32)total; n++) {
                /* Candidate diagnostics are outside the instruction begin/end pair. */
                static const char hex[] = "0123456789ABCDEF";
                char text[] = "candidate bdf=0x00:0x00.0 rc=ARG";
                u32 bus = HW_BDF_BUS(candidates[n]), dev = HW_BDF_DEV(candidates[n]);
                text[16] = hex[bus >> 4]; text[17] = hex[bus & 15U];
                text[21] = hex[dev >> 4]; text[22] = hex[dev & 15U];
                text[24] = hex[HW_BDF_FN(candidates[n])];
                s->backend->line(s->backend->ctx, text);
            }
        }
        return HWB_ARG;
    }
    case HWB_WINDOW_BAR: {
        int slot = he_slot(s, i->address.device);
        if (slot < 0 || s->vars[slot].kind != HWB_DEVICE || hwb_eval(s, i->address.bar, &a) || a >= HE_BAR_COUNT)
            return HWB_ARG;
        return he_assign(s, i->name, HWB_WINDOW_REF, 0, (u32)slot, a);
    }
    case HWB_BLOCK32: {
        struct hw_block_req block = {
            .req = {.version=HW_VERSION, .action=i->width == 1 ? HW_BLOCK8 : i->width == 2 ? HW_BLOCK16 : HW_BLOCK32, .space=HW_PHYS,
                    .width=i->width, .addr=r->addr, .value=r->value},
            .count=r->count, .increment=i->increment
        };
        if (s->backend->access(s->backend->ctx, &block.req)) return HWB_IO;
        if (block.req.status > HW_IO_ERROR) return HWB_IO;
        if (block.req.status) return (enum hwb_rc)block.req.status;
        r->done = r->count; s->writes += r->count;
        return HWB_OK;
    }
    case HWB_FILL:
        for (u32 n = 0; n < r->count; n++) {
            rc = he_point(s, 0); if (rc) return rc;
            a = r->value;
            rc = he_access(s, HW_WRITE, i->space, i->width, 0,
                           r->addr + n * i->width, &a);
            if (rc) return rc;
            r->done++; s->writes++;
        }
        return HWB_OK;
    case HWB_READ: case HWB_WRITE:
        /* P9: address resolution precedes begin; only data access follows it. */
        a = r->value;
        rc = he_access(s, i->op == HWB_READ ? HW_READ : HW_WRITE,
                       i->space, i->width, r->bdf, r->addr, &a);
        if (rc) return rc;
        r->value = a;
        if (i->op == HWB_WRITE) { s->writes++; return HWB_OK; }
        if (element) { *element = a; return HWB_OK; }
        return he_assign(s, i->name, HWB_SCALAR, a, 0, 0);
    case HWB_PRINT:
        rc = hwb_eval(s, i->value, &a); if (rc) return rc;
        hwb_log_print(s->backend, r, s->program->source + i->text.begin, i->text.end - i->text.begin, a);
        return HWB_OK;
    case HWB_LOG:
        hwb_log_text(s->backend, r, s->program->source + i->text.begin, i->text.end - i->text.begin);
        return HWB_OK;
    case HWB_IF:
        if (hwb_eval(s, i->lhs, &a) || hwb_eval(s, i->rhs, &b)) return HWB_ARG;
        if ((a != b) != !!i->not_equal) return HWB_OK;
        /* fall through */
    case HWB_GOTO: case HWB_CALL:
        if (i->op == HWB_CALL && s->call_depth == HWB_CALL_DEPTH) return HWB_ARG;
        for (u32 n = 0; n < s->program->label_count; n++) {
            char name[HWB_NAME_CHARS + 1U]; he_name(s, i->name, name);
            if (he_equal(name, s->program->labels[n].name)) {
                if (i->op == HWB_CALL) s->call_stack[s->call_depth++] = *next;
                *next = s->program->labels[n].pc; return HWB_OK;
            }
        }
        return HWB_ARG;
    case HWB_RETURN:
        if (!s->call_depth) return HWB_ARG;
        *next = s->call_stack[--s->call_depth]; return HWB_OK;
    case HWB_END: *next = HWB_NO_PC; return HWB_OK;
    case HWB_LABEL: return HWB_OK;
    default: return HWB_ARG;
    }
}
static enum hwb_rc he_stop(struct hwb_state *s, enum hwb_rc rc)
{
    s->rc = rc; s->stopped = 1; hwb_log_finish(s); return rc;
}
void hwb_init(struct hwb_state *s, const struct hwb_backend *backend)
{
    *s = (struct hwb_state){0}; s->backend = backend; s->pc = HWB_NO_PC; s->stopped = 1;
}
void hwb_reset(struct hwb_state *s)
{
    for (u32 n = 0; n < HWB_VARIABLES; n++) s->vars[n] = (struct hwb_variable){0};
    s->var_count = 0; s->array_used = 0;
}
static u32 he_first(const struct hwb_program *g)
{
    for (u32 n = 0; n < g->source_bytes; n++) if (g->starts[n / 8U] & (1U << (n % 8U))) return n;
    return HWB_NO_PC;
}
enum hwb_rc hwb_load(struct hwb_state *s, struct hwb_program *g, char *source,
                       u32 capacity, const char *path, const struct hwb_initial *initial, u32 count)
{
    u32 bytes = 0, error_line;
    if (count > HWB_VARIABLES || (count && !initial) || !g || !source || !path ||
        g == s->program || (s->program && source == s->program->source)) return HWB_ARG;
    if (s->backend->read_file(s->backend->ctx, path, source, capacity, &bytes)) return HWB_IO;
    if (bytes > capacity || hwb_parse(g, source, bytes, &error_line)) return HWB_ARG;
    struct hwb_variable vars[HWB_VARIABLES] = {0};
    u32 var_count = 0;
    he_put_vars(vars, &var_count, "mode", HWB_SCALAR, 0, 0, 0);
    for (u32 n = 0; n < count; n++) {
        const char *name = initial[n].name;
        if (!name || !((*name >= 'a' && *name <= 'z') || (*name >= 'A' && *name <= 'Z') || *name == '_'))
            return HWB_ARG;
        u32 k = 0;
        while (name[k]) {
            char c = name[k++];
            if (k > HWB_NAME_CHARS || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                c == '_' || (c >= '0' && c <= '9'))) return HWB_ARG;
        }
        if (he_put_vars(vars, &var_count, name, HWB_SCALAR, initial[n].value, 0, 0)) return HWB_ARG;
    }
    enum hwb_rc rc = he_point(s, 1); if (rc) return rc;
    if (!s->stopped) he_stop(s, HWB_ABORT);
    hwb_reset(s);
    for (u32 n = 0; n < var_count; n++) s->vars[n] = vars[n];
    s->var_count = var_count; s->program = g; s->pc = he_first(g);
    s->run = ++he_run_serial; s->seq = s->lines = s->writes = s->call_depth = 0;
    s->stopped = 0; s->rc = HWB_OK;
    return HWB_OK;
}
enum hwb_rc hwb_run(struct hwb_state *s, struct hwb_program *g, char *source,
                      u32 capacity, const char *path, const struct hwb_initial *initial, u32 count)
{
    enum hwb_rc rc = hwb_load(s, g, source, capacity, path, initial, count);
    return rc ? rc : hwb_continue(s);
}
enum hwb_rc hwb_step(struct hwb_state *s)
{
    if (s->stopped) return s->rc;
    if (!s->program) return he_stop(s, HWB_ARG);
    if (s->pc == HWB_NO_PC) return he_stop(s, HWB_OK);
    enum hwb_rc rc = he_point(s, 0); /* every instruction, including label/goto */
    if (rc) return he_stop(s, rc);
    struct hwb_instruction i;
    rc = hwb_parse_instruction(s->program, s->pc, &i);
    if (rc) return he_stop(s, rc);
    s->seq++; s->lines++;
    struct hwb_log_record r = {.run=s->run, .seq=s->seq, .line=i.line, .op=i.op,
                               .space=i.space, .width=i.width};
    u32 *element = 0;
    char destination[HWB_NAME_CHARS + 1U];
    if (i.op == HWB_LET || i.op == HWB_READ || i.op == HWB_POLL) {
        he_name(s, i.name, destination); r.destination = destination;
        rc = he_destination(s, &i, &r, &element);
    }
    if (!rc && i.op == HWB_LET) rc = hwb_eval(s, i.value, &r.value);
    char target[HWB_NAME_CHARS + 1U];
    if (i.address.device.end > i.address.device.begin) {
        he_name(s, i.address.device, target); r.target = target;
    }
    if (i.op == HWB_READ || i.op == HWB_WRITE || i.op == HWB_FILL || i.op == HWB_BLOCK32 || i.op == HWB_POLL) {
        if (!rc) rc = he_address(s, &i, &r);
        /* Wire BDFs use only 16 bits; this sentinel is local to logging. */
        if (rc) r.bdf = U32_MAX;
    }
    if (!rc && (i.op == HWB_WRITE || i.op == HWB_FILL || i.op == HWB_BLOCK32)) {
        rc = hwb_eval(s, i.value, &r.value);
        if (!rc && !he_fits(r.value, i.width)) rc = HWB_ARG;
    }
    if (!rc && (i.op == HWB_FILL || i.op == HWB_BLOCK32)) {
        rc = hwb_eval(s, i.count, &r.count);
        /* Last element start plus its final byte must fit before any write. */
        if (!rc && (!r.count || r.count - 1U >
            (U32_MAX - r.addr - (i.width - 1U)) / i.width)) rc = HWB_ARG;
    }
    if (!rc && (i.op == HWB_WAIT || i.op == HWB_POLL)) {
        rc = hwb_eval(s, i.op == HWB_POLL ? i.timeout : i.value, &r.requested_us);
        if (!rc && (r.requested_us < HWB_US_MIN || r.requested_us > HWB_US_MAX)) rc = HWB_ARG;
    }
    if (!rc && i.op == HWB_BLOCK32 && r.count > HW_BLOCK_BYTES / i.width) rc = HWB_ARG;
    r.increment = i.increment;
    hwb_log_begin(s->backend, &r);
    u32 next = i.next_pc;
    struct hwb_poll_result poll = {0};
    if (!rc) {
        if (i.op == HWB_WAIT || i.op == HWB_POLL) rc = he_wait_poll(s, &i, &r, &poll);
        else rc = he_execute(s, &i, &r, &next, element);
    }
    if (i.op == HWB_POLL && poll.reads) {
        enum hwb_rc assigned = HWB_OK;
        if (element) *element = poll.value;
        else assigned = he_assign(s, i.name, HWB_SCALAR, poll.value, 0, 0);
        if (!rc) rc = assigned;
    }
    r.rc = rc;
    if (i.op == HWB_POLL) hwb_log_poll(s->backend, &r, &poll);
    else if (rc || (i.op != HWB_PRINT && i.op != HWB_LOG)) hwb_log_end(s->backend, &r);
    if (rc) return he_stop(s, rc);
    s->pc = next;
    if (next == HWB_NO_PC) return he_stop(s, HWB_OK);
    s->rc = HWB_OK; return HWB_OK;
}
enum hwb_rc hwb_continue(struct hwb_state *s)
{
    while (!s->stopped) hwb_step(s);
    return s->rc;
}
void hwb_abort(struct hwb_state *s)
{
    if (!s->stopped) he_stop(s, HWB_ABORT);
}
enum hwb_rc hwb_command(struct hwb_state *s, const char *source, u32 bytes)
{
    struct hwb_program command;
    u32 line;
    if (bytes > HWB_LINE_BYTES || hwb_parse(&command, source, bytes, &line) || command.instructions != 1U)
        return HWB_ARG;
    enum hwb_rc rc = he_point(s, 1); if (rc) return rc;
    const struct hwb_program *program = s->program;
    u32 pc = s->pc, depth = s->call_depth, run = s->run, seq = s->seq;
    u32 lines = s->lines, writes = s->writes, stopped = s->stopped;
    enum hwb_rc previous_rc = s->rc;
    s->program = &command; s->pc = he_first(&command); s->call_depth = 0;
    s->run = ++he_run_serial; s->seq = s->lines = s->writes = 0; s->stopped = 0;
    rc = hwb_step(s);
    if (!s->stopped) he_stop(s, rc);
    /* Variables and arrays persist; the loaded run retains its records and PC.
     * A single syntax-checked command cannot call a label, so its stack stays intact. */
    s->program = program; s->pc = pc; s->call_depth = depth;
    s->run = run; s->seq = seq; s->lines = lines; s->writes = writes;
    s->stopped = stopped; s->rc = previous_rc;
    return rc;
}
