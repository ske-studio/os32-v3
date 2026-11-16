/* HSYNC_MANIFEST_DESIGN §3: the wire is decoded byte by byte on both CPUs.
# descriptor: 32 bytes
0 magic[4]="HMD1"; 4 version:u16=1; 6 state:u16 (0 BUSY,1 READY,2 ERROR)
8 token[16]; 24 bytes:u32; 28 error:i32 (成功/BUSY は 0)
# data header: 24 bytes
0 magic[4]="HMT1"; 4 version:u16=1; 6 header_bytes:u16=24
8 total_bytes:u32; 12 entry_count:u32; 16 records_crc32:u32; 20 skipped_names:u32
# record: 16 + path_len bytes
0 kind:u8 (1=file,2=dir,3=other); 1 flags:u8=0; 2 path_len:u16
4 size:u32; 8 mtime:u32; 12 crc32:u32; 16 path[path_len]
# file: size<=0x7fffffff。dir/other: size=0,crc32=0。mtime=0 は不明。
 */
#ifndef HSYNC_INVENTORY_INC
#define HSYNC_INVENTORY_INC

#define HSI_MAX_BYTES (1024u * 1024u)
#define HSI_MAX_ENTRIES 8192u
#define HSI_DESC_SIZE 32u
#define HSI_HEADER_SIZE 24u
#define HSI_RECORD_SIZE 16u
#define HSI_READ_SIZE 64512u
#define HSI_WAIT_TICKS 6000u
#define HSI_FALLBACK 1

/* Ownership belongs to the caller after a successful acquisition. */
typedef struct {
    u8 *data;
    u32 *offsets;
    u32 bytes, count, skipped;
} HsInventory;

typedef struct {
    u8 token[16];
    u32 bytes;
    int error;
    unsigned state;
} HsDescriptor;

static u32 hsi_get16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 hsi_get32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static int hsi_magic(const u8 *p, const char *s)
{
    return p[0] == (u8)s[0] && p[1] == (u8)s[1] &&
           p[2] == (u8)s[2] && p[3] == (u8)s[3];
}
static int hsi_descriptor(const u8 *p, u32 len, HsDescriptor *d)
{
    u32 i;
    if (len < 4) return HSI_FALLBACK;
    if (!hsi_magic(p, "HMD1")) return HSI_FALLBACK;
    if (len != HSI_DESC_SIZE || hsi_get16(p + 4) != 1) return OS32_ERR_IO;
    d->state = hsi_get16(p + 6);
    for (i = 0; i < 16; i++) d->token[i] = p[8 + i];
    d->bytes = hsi_get32(p + 24);
    d->error = (int)hsi_get32(p + 28);
    if (d->state > 2 || (d->state != 2 && d->error != 0) ||
        (d->state == 2 && d->error >= 0)) return OS32_ERR_IO;
    if (d->state == 1 && d->bytes < HSI_HEADER_SIZE) return OS32_ERR_IO;
    return 0;
}
static void hsi_token_hex(const u8 *token, char *out)
{
    static const char digits[] = "0123456789abcdef";
    u32 i;
    for (i = 0; i < 16; i++) {
        out[i * 2] = digits[token[i] >> 4];
        out[i * 2 + 1] = digits[token[i] & 15];
    }
    out[32] = 0;
}
static void hsi_release(HsInventory *inv)
{
    if (inv->offsets) api->mem_free(inv->offsets);
    if (inv->data) api->mem_free(inv->data);
    inv->offsets = 0; inv->data = 0;
    inv->bytes = 0; inv->count = 0; inv->skipped = 0;
}

/* Compare components as bytes; a parent sorts before its descendants. */
static int hsi_path_order(const u8 *a, u32 alen, const u8 *b, u32 blen)
{
    u32 i = 0;
    while (i < alen && i < blen && a[i] == b[i]) i++;
    if (i == alen) return i == blen ? 0 : -1;
    if (i == blen) return 1;
    if (a[i] == '/') return -1;
    if (b[i] == '/') return 1;
    return a[i] < b[i] ? -1 : 1;
}

/* No consumer receives offsets until length, CRC and tree have all passed. */
static int hsi_decode(HsInventory *inv)
{
    const u8 *p = inv->data, *prev = 0;
    u32 parents[MAX_DEPTH + 2], children[MAX_DEPTH + 2];
    u32 pos = HSI_HEADER_SIZE, i, prevlen = 0, count, crc;
    if (inv->bytes < HSI_HEADER_SIZE || !hsi_magic(p, "HMT1") ||
        hsi_get16(p + 4) != 1 || hsi_get16(p + 6) != HSI_HEADER_SIZE ||
        hsi_get32(p + 8) != inv->bytes) return OS32_ERR_IO;
    count = hsi_get32(p + 12);
    if (count > HSI_MAX_ENTRIES || inv->bytes > HSI_MAX_BYTES) return OS32_ERR_IO;
    crc = crc32_core_final(crc32_core_update(CRC32_INIT, p + HSI_HEADER_SIZE,
                                              inv->bytes - HSI_HEADER_SIZE));
    if (crc != hsi_get32(p + 16)) return OS32_ERR_IO;
    for (i = 0; i < MAX_DEPTH + 2; i++) { parents[i] = 0; children[i] = 0; }
    for (i = 0; i < count; i++) {
        u32 n, j, depth = 0, component = 0, parentlen = 0;
        const u8 *path;
        if (pos > inv->bytes || inv->bytes - pos < HSI_RECORD_SIZE) return OS32_ERR_IO;
        n = hsi_get16(p + pos + 2);
        if (!n || n > inv->bytes - pos - HSI_RECORD_SIZE) return OS32_ERR_IO;
        path = p + pos + HSI_RECORD_SIZE;
        if (p[pos] < 1 || p[pos] > 3 || p[pos + 1] ||
            (p[pos] == 1 && hsi_get32(p + pos + 4) > HS_MAX_FILE_SIZE) ||
            (p[pos] != 1 && (hsi_get32(p + pos + 4) || hsi_get32(p + pos + 12)))) return OS32_ERR_IO;
        for (j = 0; j < n; j++) {
            if (!path[j] || path[j] == '\\') return OS32_ERR_IO;
            if (path[j] == '/') {
                if (!component) return OS32_ERR_IO;
                if (component >= NAME_CAP || depth >= MAX_DEPTH) return HSI_FALLBACK;
                if ((component == 1 && path[j-1] == '.') ||
                    (component == 2 && path[j-1] == '.' && path[j-2] == '.')) return OS32_ERR_IO;
                depth++; parentlen = j; component = 0;
            } else component++;
        }
        if (component >= NAME_CAP || n >= OS32_MAX_PATH) return HSI_FALLBACK;
        if (!component ||
            (component == 1 && path[n-1] == '.') ||
            (component == 2 && path[n-1] == '.' && path[n-2] == '.')) return OS32_ERR_IO;
        if (prev && hsi_path_order(prev, prevlen, path, n) >= 0) return OS32_ERR_IO;
        if (depth) {
            u32 off = parents[depth - 1];
            u32 plen;
            if (!off || p[off] != 2) return OS32_ERR_IO;
            plen = hsi_get16(p + off + 2);
            if (plen != parentlen) return OS32_ERR_IO;
            for (j = 0; j < plen; j++) if (p[off + HSI_RECORD_SIZE + j] != path[j]) return OS32_ERR_IO;
        }
        if (++children[depth] > MAX_FILES) return HSI_FALLBACK;
        parents[depth] = pos;
        if (depth + 1 < MAX_DEPTH + 2) children[depth + 1] = 0;
        inv->offsets[i] = pos;
        prev = path; prevlen = n;
        pos += HSI_RECORD_SIZE + n;
    }
    if (pos != inv->bytes) return OS32_ERR_IO;
    inv->count = count; inv->skipped = hsi_get32(p + 20);
    return 0;
}

/* Prefix construction also rejects a path that VFS would silently truncate. */
static int hsi_path(char *out, const char *root, const char *part, const char *token)
{
    static const char dir[] = "/.os32-manifest/";
    u32 n = 0, i, depth = 0;
    while (root[n]) {
        if (n + 1 >= OS32_MAX_PATH) return HSI_FALLBACK;
        out[n] = root[n];
        if (root[n] != '/' && (n == 0 || root[n-1] == '/')) depth++;
        n++;
    }
    if (n && out[n-1] == '/') n--;
    if (depth + (token ? 3u : 2u) > HS_MAX_PATH_DEPTH) return HSI_FALLBACK;
    for (i = 0; dir[i]; i++) { if (n + 1 >= OS32_MAX_PATH) return HSI_FALLBACK; out[n++] = dir[i]; }
    for (i = 0; part[i]; i++) { if (n + 1 >= OS32_MAX_PATH) return HSI_FALLBACK; out[n++] = part[i]; }
    if (token) {
        if (n + 34 > OS32_MAX_PATH) return HSI_FALLBACK;
        out[n++] = '/';
        for (i = 0; i < 32; i++) out[n++] = token[i];
    }
    out[n] = 0;
    return 0;
}
static int hsi_old_host(int rc)
{
    return rc == OS32_ERR_NOTFOUND || rc == OS32_ERR_NOTDIR ||
           rc == OS32_ERR_NOSYS || rc == OS32_ERR_NAMETOOLONG || rc == OS32_ERR_INVAL;
}
static int hsi_read_descriptor(const char *path, int first, HsDescriptor *d)
{
    u8 raw[HSI_DESC_SIZE];
    int fd = api->sys_open(path, KAPI_O_RDONLY), got, rc;
    if (fd < 0) return first && hsi_old_host(fd) ? HSI_FALLBACK : fd;
    got = api->sys_read(fd, raw, HSI_DESC_SIZE);
    api->sys_close(fd);
    if (got < 0) return first && hsi_old_host(got) ? HSI_FALLBACK : got;
    rc = hsi_descriptor(raw, (u32)got, d);
    return !first && rc == HSI_FALLBACK ? OS32_ERR_IO : rc;
}
static int hsi_acquire(const char *root, HsInventory *inv)
{
    char path[OS32_MAX_PATH], token[33];
    HsDescriptor d, next;
    OS32_Stat st;
    u32 start, i, pos;
    int fd, rc;
    inv->data = 0; inv->offsets = 0; inv->bytes = 0; inv->count = 0; inv->skipped = 0;
    if (hsi_path(path, root, "status", "00000000000000000000000000000000") ||
        hsi_path(path, root, "new", 0)) return HSI_FALLBACK;
    start = api->get_tick();
    rc = hsi_read_descriptor(path, 1, &d);
    if (rc) return rc;
    if ((u32)(api->get_tick() - start) >= HSI_WAIT_TICKS) return OS32_ERR_TIMEOUT;
    hsi_token_hex(d.token, token);
    while (d.state == 0) {
        if ((u32)(api->get_tick() - start) >= HSI_WAIT_TICKS) return OS32_ERR_TIMEOUT;
        if (hsi_path(path, root, "status", token)) return OS32_ERR_IO;
        api->sys_yield();
        rc = hsi_read_descriptor(path, 0, &next);
        if (rc) return rc;
        if ((u32)(api->get_tick() - start) >= HSI_WAIT_TICKS) return OS32_ERR_TIMEOUT;
        for (i = 0; i < 16; i++) if (d.token[i] != next.token[i]) return OS32_ERR_IO;
        d = next;
    }
    if (d.state == 2) return d.error == OS32_ERR_FULL ? HSI_FALLBACK : d.error;
    if (d.bytes > HSI_MAX_BYTES) return HSI_FALLBACK;
    if (hsi_path(path, root, "data", token)) return OS32_ERR_IO;
    rc = api->sys_stat(path, &st);
    if (rc < 0) return rc;
    if (st.st_size != d.bytes) return OS32_ERR_IO;
    inv->data = (u8 *)api->mem_alloc(d.bytes);
    inv->offsets = (u32 *)api->mem_alloc(HSI_MAX_ENTRIES * sizeof(u32));
    if (!inv->data || !inv->offsets) { hsi_release(inv); return HSI_FALLBACK; }
    inv->bytes = d.bytes;
    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) { hsi_release(inv); return fd; }
    for (pos = 0; pos < inv->bytes;) {
        u32 want = inv->bytes - pos;
        if (want > HSI_READ_SIZE) want = HSI_READ_SIZE;
        rc = api->sys_read(fd, inv->data + pos, want);
        if (rc < 0 || (u32)rc != want) { api->sys_close(fd); hsi_release(inv); return rc < 0 ? rc : OS32_ERR_IO; }
        pos += (u32)rc;
    }
    api->sys_close(fd);
    rc = hsi_decode(inv);
    if (rc) { hsi_release(inv); return rc; }
    return 0;
}
static int hsi_local_crc(const char *path, u32 expected_size, u32 *out_crc, u32 *out_bytes)
{
    u32 state = CRC32_INIT, total = 0;
    int fd = api->sys_open(path, KAPI_O_RDONLY), got;
    if (fd < 0) return fd;
    for (;;) {
        got = api->sys_read(fd, file_buf, FILE_BUF_SIZE);
        if (got < 0 || (u32)got > FILE_BUF_SIZE || (u32)got > HS_MAX_FILE_SIZE - total) {
            api->sys_close(fd); return got < 0 ? got : OS32_ERR_IO;
        }
        if (!got) break;
        state = crc32_core_update(state, file_buf, (u32)got);
        total += (u32)got;
    }
    api->sys_close(fd);
    if (total != expected_size) return OS32_ERR_IO;
    *out_crc = crc32_core_final(state); *out_bytes = total;
    return 0;
}
#endif
