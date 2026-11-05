/* HW_BASIC_DESIGN §2: source slices only; no allocation or backend effects. */
#include "hwb.h"

struct hp { const char *s; u32 p, end; int bad; };
static int hp_space(char c) { return c == ' ' || c == '\t'; }
static int hp_first(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int hp_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void hp_skip(struct hp *p)
{
    while (p->p < p->end && hp_space(p->s[p->p])) p->p++;
}
static int hp_equal(const char *s, struct hwb_span a, const char *b)
{
    u32 i = a.begin;
    while (i < a.end && *b && s[i] == *b) { i++; b++; }
    return i == a.end && !*b;
}
static struct hwb_span hp_name(struct hp *p)
{
    hp_skip(p);
    struct hwb_span a = {p->p, p->p};
    if (p->p < p->end && hp_first(p->s[p->p])) {
        do { p->p++; } while (p->p < p->end &&
            (hp_first(p->s[p->p]) || (p->s[p->p] >= '0' && p->s[p->p] <= '9')));
    }
    a.end = p->p;
    if (a.end == a.begin || a.end - a.begin > HWB_NAME_CHARS) p->bad = 1;
    return a;
}
static int hp_take(struct hp *p, const char *s)
{
    hp_skip(p);
    u32 i = p->p;
    while (*s && i < p->end && p->s[i] == *s) { i++; s++; }
    if (*s) return 0;
    p->p = i;
    return 1;
}
static void hp_need(struct hp *p, const char *s) { if (!hp_take(p, s)) p->bad = 1; }
static void hp_word(struct hp *p, const char *s)
{
    struct hwb_span a = hp_name(p);
    if (!hp_equal(p->s, a, s)) p->bad = 1;
}
static struct hwb_span hp_number(struct hp *p, u32 base, u32 *value)
{
    hp_skip(p);
    struct hwb_span a = {p->p, p->p};
    if (p->p + 1 < p->end && p->s[p->p] == '0' &&
        (p->s[p->p + 1] == 'x' || p->s[p->p + 1] == 'X')) {
        base = 16; p->p += 2;
    }
    u32 start = p->p, v = 0;
    while (p->p < p->end) {
        int d = hp_digit(p->s[p->p]);
        if (d < 0 || (u32)d >= base) break;
        if (v > (U32_MAX - (u32)d) / base) p->bad = 1;
        v = v * base + (u32)d; p->p++;
    }
    if (p->p == start || (p->p < p->end && hp_first(p->s[p->p]))) p->bad = 1;
    a.end = p->p; *value = v;
    return a;
}
/* Precedence climbing: | ^ & shifts addition, then primary. In syntax mode
 * variable values/types and arithmetic faults are deferred to execution. */
static u32 hp_expr(struct hp *p, const struct hwb_state *state, u32 level);
static u32 hp_primary(struct hp *p, const struct hwb_state *state)
{
    hp_skip(p);
    if (hp_take(p, "(")) {
        u32 v = hp_expr(p, state, 0);
        hp_need(p, ")"); return v;
    }
    if (p->p < p->end && hp_first(p->s[p->p])) {
        struct hwb_span a = hp_name(p);
        if (!state) return 0;
        for (u32 i = 0; i < state->var_count && i < HWB_VARIABLES; i++) {
            if (hp_equal(p->s, a, state->vars[i].name)) {
                if (state->vars[i].kind != HWB_SCALAR) p->bad = 1;
                return state->vars[i].value;
            }
        }
        p->bad = 1; return 0;
    }
    u32 v;
    hp_number(p, 10, &v); return v;
}
static u32 hp_expr(struct hp *p, const struct hwb_state *state, u32 level)
{
    if (level == 5) return hp_primary(p, state);
    u32 v = hp_expr(p, state, level + 1);
    while (!p->bad) {
        hp_skip(p);
        u32 op = 0;
        if (level == 0 && hp_take(p, "|")) op = '|';
        if (level == 1 && hp_take(p, "^")) op = '^';
        if (level == 2 && hp_take(p, "&")) op = '&';
        if (level == 3) {
            if (hp_take(p, "<<")) op = '<';
            else if (hp_take(p, ">>")) op = '>';
        }
        if (level == 4) {
            if (hp_take(p, "+")) op = '+';
            /* The read destination arrow is a delimiter, not subtraction. */
            else if (p->p + 1 < p->end && p->s[p->p] == '-' && p->s[p->p+1] == '>') break;
            else if (hp_take(p, "-")) op = '-';
        }
        if (!op) break;
        u32 b = hp_expr(p, state, level + 1);
        if (!state) continue;
        if (op == '+') { if (b > U32_MAX - v) p->bad = 1; v += b; }
        if (op == '-') { if (v < b) p->bad = 1; v -= b; }
        if (op == '&') v &= b;
        if (op == '|') v |= b;
        if (op == '^') v ^= b;
        if (op == '<' || op == '>') {
            if (b >= 32U) { p->bad = 1; b = 0; }
            v = op == '<' ? v << b : v >> b;
        }
    }
    return v;
}
static struct hwb_span hp_operand(struct hp *p, int whole)
{
    /* Adjacent primary operands ("1(2)", "(1)2") are not arguments.
     * Separators before the first operand may also be =, + or an opener. */
    if (p->p && p->p < p->end && !hp_space(p->s[p->p]) &&
        (p->s[p->p-1] == ')' || p->s[p->p-1] == '"' || hp_first(p->s[p->p-1]) ||
         (p->s[p->p-1] >= '0' && p->s[p->p-1] <= '9'))) p->bad = 1;
    hp_skip(p);
    struct hwb_span a = {p->p, p->p};
    if (whole) hp_expr(p, 0, 0);
    else hp_primary(p, 0);
    a.end = p->p;
    while (a.end > a.begin && hp_space(p->s[a.end-1])) a.end--;
    return a;
}
enum hwb_rc hwb_eval(const struct hwb_state *state, struct hwb_span expr, u32 *value)
{
    if (!state || !state->program || !state->program->source || !value ||
        expr.begin >= expr.end || expr.end > state->program->source_bytes) return HWB_ARG;
    struct hp p = {state->program->source, expr.begin, expr.end, 0};
    u32 v = hp_expr(&p, state, 0);
    hp_skip(&p);
    if (p.bad || p.p != p.end) return HWB_ARG;
    *value = v; return HWB_OK;
}
static struct hwb_span hp_text(struct hp *p)
{
    hp_need(p, "\"");
    struct hwb_span a = {p->p, p->p};
    while (p->p < p->end && p->s[p->p] != '"') p->p++;
    a.end = p->p; hp_need(p, "\""); return a;
}
static u32 hp_width(struct hp *p)
{
    hp_skip(p);
    u32 begin = p->p, v;
    hp_number(p, 10, &v);
    /* Width spelling is decimal, exactly 8/16/32. */
    struct hwb_span a = {begin, p->p};
    if (hp_equal(p->s, a, "8")) return 1;
    if (hp_equal(p->s, a, "16")) return 2;
    if (hp_equal(p->s, a, "32")) return 4;
    p->bad = 1; return 0;
}
static void hp_address(struct hp *p, struct hwb_instruction *i, int pci)
{
    struct hwb_address *a = &i->address;
    if (pci) {
        a->kind = HWB_ADDR_PCI; a->device = hp_name(p);
        a->offset = hp_operand(p, 0); return;
    }
    struct hwb_span word = hp_name(p);
    if (hp_equal(p->s, word, "pci")) {
        hp_address(p, i, 1); return;
    }
    if (hp_equal(p->s, word, "phys") || hp_equal(p->s, word, "port")) {
        a->kind = hp_equal(p->s, word, "phys") ? HWB_ADDR_PHYS : HWB_ADDR_PORT;
        a->offset = hp_operand(p, 0);
        return;
    }
    if (hp_equal(p->s, word, "bar")) {
        a->kind = HWB_ADDR_BAR; a->device = hp_name(p);
        a->bar = hp_operand(p, 0);
    } else { a->kind = HWB_ADDR_WINDOW; a->device = word; }
    hp_need(p, "+"); a->offset = hp_operand(p, 0);
}
/* Decode one physical line. Comments start only outside literal text. */
static int hp_decode(const struct hwb_program *g, u32 pc, struct hwb_instruction *i, u32 line)
{
    *i = (struct hwb_instruction){0};
    i->pc = pc; i->line = line;
    u32 end = pc;
    while (end < g->source_bytes && g->source[end] != '\n') end++;
    i->next_pc = HWB_NO_PC;
    if (end > pc && g->source[end-1] == '\r') end--;
    int quoted = 0;
    for (u32 n = pc; n < end; n++) {
        if (g->source[n] == '"') quoted = !quoted;
        if (g->source[n] == '#' && !quoted) { end = n; break; }
    }
    struct hp p = {g->source, pc, end, 0};
    /* Commands with widths contain digits; scan command independently. */
    hp_skip(&p);
    struct hwb_span cmd = {p.p, p.p};
    while (p.p < p.end && !hp_space(p.s[p.p])) p.p++;
    cmd.end = p.p;
#define CMD(word) hp_equal(p.s, cmd, (word))
    if (CMD("let")) {
        i->op = HWB_LET; i->name = hp_name(&p); hp_need(&p, "="); i->value = hp_operand(&p, 1);
    } else if (CMD("device")) {
        i->name = hp_name(&p); hp_need(&p, "=");
        struct hwb_span kind = hp_name(&p);
        u32 v;
        if (hp_equal(p.s, kind, "bdf")) {
            i->op = HWB_DEVICE_BDF;
            i->bus = hp_number(&p, 10, &v); hp_need(&p, ":");
            i->dev = hp_number(&p, 10, &v); hp_need(&p, "."); i->fn = hp_number(&p, 10, &v);
        } else if (hp_equal(p.s, kind, "id")) {
            i->op = HWB_DEVICE_ID;
            i->vendor = hp_number(&p, 16, &v); hp_need(&p, ":"); i->device = hp_number(&p, 16, &v);
        } else p.bad = 1;
    } else if (CMD("window")) {
        i->op = HWB_WINDOW_BAR; i->name = hp_name(&p); hp_need(&p, "="); hp_word(&p, "bar");
        i->address.kind = HWB_ADDR_BAR; i->address.device = hp_name(&p); i->address.bar = hp_operand(&p, 0);
    } else if (CMD("wait")) {
        i->op = HWB_WAIT; hp_word(&p, "us"); i->value = hp_operand(&p, 0);
    } else if (CMD("poll")) {
        i->op = HWB_POLL; i->width = hp_width(&p); i->space = HW_PHYS; hp_address(&p, i, 0);
        if (i->address.kind == HWB_ADDR_PORT) i->space = HW_IO;
        if (i->address.kind == HWB_ADDR_PCI) i->space = HW_PCI;
        i->mask = hp_operand(&p, 0); i->value = hp_operand(&p, 0);
        i->timeout = hp_operand(&p, 0); i->interval = hp_operand(&p, 0);
        hp_need(&p, "->"); i->name = hp_name(&p);
    } else if (CMD("print") || CMD("log")) {
        i->op = CMD("print") ? HWB_PRINT : HWB_LOG; i->text = hp_text(&p);
        if (i->op == HWB_PRINT) i->value = hp_operand(&p, 1);
    } else if (CMD("if")) {
        i->op = HWB_IF; i->lhs = hp_operand(&p, 1);
        if (hp_take(&p, "!=")) i->not_equal = 1; else hp_need(&p, "==");
        i->rhs = hp_operand(&p, 1); hp_word(&p, "goto"); i->name = hp_name(&p);
    } else if (CMD("label") || CMD("goto") || CMD("call")) {
        i->op = CMD("label") ? HWB_LABEL : CMD("goto") ? HWB_GOTO : HWB_CALL;
        i->name = hp_name(&p);
    } else if (CMD("return") || CMD("end")) i->op = CMD("return") ? HWB_RETURN : HWB_END;
    else {
        /* Match the full width-bearing command, rejecting all other suffixes. */
        int found = 0;
        static const char *const bases[] = {"pciread", "pciwrite", "ioread", "iowrite", "mmioread", "mmiowrite", "mmiofill"};
        for (u32 b = 0; b < 7; b++) {
            u32 n = cmd.begin, k = 0;
            while (bases[b][k] && n < cmd.end && p.s[n] == bases[b][k]) { n++; k++; }
            if (bases[b][k]) continue;
            struct hp w = {p.s, n, cmd.end, 0};
            u32 width = hp_width(&w);
            if (w.bad || w.p != w.end) continue;
            found = 1; i->width = width; i->op = b == 6 ? HWB_FILL : b & 1U ? HWB_WRITE : HWB_READ;
            i->space = b < 2 ? HW_PCI : b < 4 ? HW_IO : HW_PHYS;
            hp_address(&p, i, b < 2);
            if ((i->space == HW_PCI && i->address.kind != HWB_ADDR_PCI) ||
                (i->space == HW_IO && i->address.kind != HWB_ADDR_PORT && i->address.kind != HWB_ADDR_BAR && i->address.kind != HWB_ADDR_WINDOW) ||
                (i->space == HW_PHYS && i->address.kind != HWB_ADDR_PHYS && i->address.kind != HWB_ADDR_BAR && i->address.kind != HWB_ADDR_WINDOW)) p.bad = 1;
            if (i->op == HWB_READ) { hp_need(&p, "->"); i->name = hp_name(&p); }
            else {
                if (i->op == HWB_FILL) i->count = hp_operand(&p, 0);
                i->value = hp_operand(&p, 1);
            }
            break;
        }
        if (!found) p.bad = 1;
    }
#undef CMD
    hp_skip(&p);
    return !p.bad && p.p == p.end;
}
enum hwb_rc hwb_parse_instruction(const struct hwb_program *g, u32 pc, struct hwb_instruction *out)
{
    if (!g || !out || !g->source || pc >= g->source_bytes || g->source_bytes > HWB_FILE_BYTES ||
        !(g->starts[pc / 8U] & (1U << (pc % 8U)))) return HWB_ARG;
    u32 line = 1;
    for (u32 n = 0; n < pc; n++) if (g->source[n] == '\n') line++;
    if (!hp_decode(g, pc, out, line)) return HWB_ARG;
    for (u32 n = pc + 1; n < g->source_bytes; n++)
        if (g->starts[n / 8U] & (1U << (n % 8U))) { out->next_pc = n; break; }
    return HWB_OK;
}
enum hwb_rc hwb_parse(struct hwb_program *g, const char *source, u32 bytes, u32 *error_line)
{
    if (error_line) *error_line = 1;
    if (!g || !source) return HWB_ARG;
    if (bytes > HWB_FILE_BYTES) {
        if (error_line) {
            for (u32 n = 0; n < HWB_FILE_BYTES; n++)
                if (source[n] == '\n') (*error_line)++;
        }
        return HWB_ARG;
    }
    *g = (struct hwb_program){0}; g->source = source; g->source_bytes = bytes;
    u32 line = 1;
    for (u32 begin = 0; begin < bytes; line++) {
        if (error_line) *error_line = line;
        u32 end = begin;
        while (end < bytes && source[end] != '\n') {
            if (!source[end] || (source[end] == '\r' && (end + 1 == bytes || source[end+1] != '\n'))) return HWB_ARG;
            end++;
        }
        u32 content_end = end;
        if (content_end > begin && source[content_end-1] == '\r') content_end--;
        if (content_end - begin > HWB_LINE_BYTES) return HWB_ARG;
        u32 pc = begin;
        while (pc < content_end && hp_space(source[pc])) pc++;
        if (pc < content_end && source[pc] != '#') {
            g->starts[pc / 8U] |= 1U << (pc % 8U); g->instructions++;
            struct hwb_instruction i;
            if (!hp_decode(g, pc, &i, line)) return HWB_ARG;
            if (i.op == HWB_LABEL) {
                if (g->label_count == HWB_LABELS) return HWB_ARG;
                for (u32 k = 0; k < g->label_count; k++)
                    if (hp_equal(source, i.name, g->labels[k].name)) return HWB_ARG;
                struct hwb_label *l = &g->labels[g->label_count++];
                u32 k = 0;
                while (i.name.begin + k < i.name.end) { l->name[k] = source[i.name.begin+k]; k++; }
                l->name[k] = 0; l->pc = pc;
            }
        }
        begin = end < bytes ? end + 1 : end;
    }
    line = 1;
    for (u32 pc = 0; pc < bytes; pc++) {
        if (source[pc] == '\n') line++;
        if (!(g->starts[pc / 8U] & (1U << (pc % 8U)))) continue;
        struct hwb_instruction i;
        if (!hp_decode(g, pc, &i, line)) return HWB_ARG;
        if (i.op == HWB_GOTO || i.op == HWB_CALL || i.op == HWB_IF) {
            int found = 0;
            for (u32 k = 0; k < g->label_count; k++) if (hp_equal(source, i.name, g->labels[k].name)) found = 1;
            if (!found) { if (error_line) *error_line = i.line; return HWB_ARG; }
        }
    }
    if (error_line) *error_line = 0;
    return HWB_OK;
}
