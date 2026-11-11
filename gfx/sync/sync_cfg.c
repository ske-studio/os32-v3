/* HAL v2 sync profiles.  The single-task kernel serializes lookups. */
#include "sync_int.h"
#include "config.h"
#include "vfs.h"
#include "kprintf.h"

#define SYNC_CFG_MAX 4096u
static char text[SYNC_CFG_MAX + 1];

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static int eq(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return lower(*a) == lower(*b);
}
static int space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static void defaults(struct gfxsync_cfg *c)
{
    *c = (struct gfxsync_cfg){ .mode = GFXSYNC_SHADOW, .unit = 1,
        .method = GFXSYNC_M_PLAIN, .flush = GFXSYNC_F_NONE, .read = GFXSYNC_R_SHADOW };
}
static int number(const char *s, u32 *out)
{
    u32 n = 0;
    if (!*s) return -1;
    while (*s) {
        u32 d;
        if (*s < '0' || *s > '9') return -1;
        d = (u32)(*s++ - '0');
        if (n > (0xffffffffu - d) / 10u) return -1;
        n = n * 10u + d;
    }
    *out = n;
    return 0;
}
static int field(struct gfxsync_cfg *c, char *name, char *v)
{
    u32 n;
    if (eq(name, "mode")) {
        if (eq(v, "shadow")) c->mode = GFXSYNC_SHADOW;
        else if (eq(v, "direct")) c->mode = GFXSYNC_DIRECT;
        else return -1;
    } else if (eq(name, "read")) {
        if (eq(v, "shadow")) c->read = GFXSYNC_R_SHADOW;
        else if (eq(v, "direct")) c->read = GFXSYNC_R_DIRECT;
        else return -1;
    } else if (eq(name, "method")) {
        if (eq(v, "plain")) c->method = GFXSYNC_M_PLAIN;
        else if (eq(v, "gap")) c->method = GFXSYNC_M_GAP;
        else if (eq(v, "sync_read")) c->method = GFXSYNC_M_SYNC_READ;
        else if (eq(v, "wait_port")) c->method = GFXSYNC_M_WAIT_PORT;
        else return -1;
    } else if (eq(name, "flush")) {
        if (eq(v, "none")) c->flush = GFXSYNC_F_NONE;
        else if (eq(v, "readback")) c->flush = GFXSYNC_F_READBACK;
        else if (eq(v, "serialize")) c->flush = GFXSYNC_F_SERIALIZE;
        else return -1;
    } else if (eq(name, "unit")) {
        if (number(v, &n) || (n != 1 && n != 2 && n != 4)) return -1;
        c->unit = (u8)n;
    } else if (eq(name, "gap_ns")) {
        if (number(v, &n) || n > 100000u) return -1;
        c->gap_ns = n;
    } else if (eq(name, "read_ns")) {
        if (number(v, &n)) return -1;
        c->read_ns = n;
    } else return -1;
    return 0;
}

int gfxsync_cfg_lookup(const char *key, struct gfxsync_cfg *out)
{
    char wanted[16], wildcard[16], section[16];
    struct gfxsync_cfg candidate, best;
    u32 len = 0, i, j; int fd, n, rank = 0, current = 0, bad = 0, best_bad = 0;
    char *p;
    if (!key || !out) return OS32_ERR_INVAL;
    defaults(out);
    for (i = 0; key[i] && i < sizeof(wanted) - 1; i++) wanted[i] = lower(key[i]);
    if (key[i]) return OS32_ERR_INVAL;
    wanted[i] = 0;
    for (j = 0; j <= i; j++) wildcard[j] = wanted[j];
    if (i == 13 && wanted[0] == 'p' && wanted[1] == 'c' && wanted[2] == 'i' && wanted[3] == ':' && wanted[8] == ':') {
        wildcard[9] = '*'; wildcard[10] = 0;
    } else wildcard[0] = 0;
    fd = vfs_open(SYS_GFXSYNC_CFG, O_RDONLY);
    if (fd < 0) return OS32_ERR_NOTFOUND;
    while (len < sizeof(text)) {
        n = vfs_read_fd(fd, text + len, sizeof(text) - len);
        if (n < 0) { vfs_close(fd); kprintf(0x07, "gfxsync: read error\n"); return OS32_ERR_NOTFOUND; }
        if (!n) break;
        len += (u32)n;
    }
    vfs_close(fd);
    if (len > SYNC_CFG_MAX) { kprintf(0x07, "gfxsync: config too large\n"); return OS32_ERR_NOTFOUND; }
    text[len] = 0;
    p = text;
    defaults(&candidate); section[0] = 0;
    for (;;) {
        char *line = p, *end;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        end = line; while (*end && *end != ';') end++; *end = 0;
        while (space(*line)) line++;
        if (*line == '[' || (!*line && !*p)) {
            if (current > rank) { best = candidate; best_bad = bad; rank = current; }
            if (!*line && !*p) break;
            end = line + 1; while (*end && *end != ']') end++;
            current = 0; bad = 0; defaults(&candidate);
            if (*end == ']') {
                *end++ = 0; while (space(*end)) end++;
                for (i = 0; line[i + 1] && i < sizeof(section) - 1; i++) section[i] = lower(line[i + 1]);
                section[i] = 0;
                if (!line[i + 1] && !*end) {
                    current = eq(section, wanted) ? 2 : (wildcard[0] && eq(section, wildcard) ? 1 : 0);
                    for (j = 0; j <= i; j++) candidate.key[j] = section[j];
                }
            }
        } else if (current) {
            while (*line) {
                char *token, *v;
                while (space(*line)) line++;
                if (!*line) break;
                token = line; while (*line && !space(*line)) line++;
                if (*line) *line++ = 0;
                v = token; while (*v && *v != '=') v++;
                if (!*v) { bad = 1; continue; }
                *v++ = 0;
                if (field(&candidate, token, v)) bad = 1;
            }
        }
        if (!*p) {
            if (current > rank) { best = candidate; best_bad = bad; rank = current; }
            break;
        }
    }
    if (!rank) return OS32_ERR_NOTFOUND;
    if (best_bad) { kprintf(0x07, "gfxsync: invalid profile\n"); return OS32_ERR_NOTFOUND; }
    *out = best;
    return 0;
}
