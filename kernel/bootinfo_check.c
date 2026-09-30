/* ======================================================================== */
/*  BOOTINFO_CHECK.C — ブート情報域の検証と表示行 (純粋関数)                */
/*                                                                          */
/*  I/O も低位メモリも触らない。kernel/bootinfo.c が 0x7E00 の写しを渡す。  */
/*    試験: tools/tests/test_bootinfo.py (+ bootinfo_host.c)                */
/*    記録: tools/tests/bootinfo_tdd.md                                     */
/*    票  : docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 0                      */
/*                                                                          */
/*  バイト列は**オフセットで**読む。ホストの u32 は 64bit なので構造体を     */
/*  重ねて読むと試験が i386 と違う並びを見る。                              */
/* ======================================================================== */

#include "bootinfo.h"

static u16 rd16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
    p[2] = (u8)((v >> 16) & 0xFF);
    p[3] = (u8)((v >> 24) & 0xFF);
}

u16 bootinfo_sum(const u8 *raw)
{
    unsigned int i;
    u16 s = 0;

    for (i = 0; i < (unsigned int)(BI_DRIVE_SIZE * BOOTINFO_NDRIVES); i++) {
        s = (u16)(s + raw[BI_OFF_DRIVE0 + i]);
    }
    return s;
}

void bootinfo_seal(u8 *raw)
{
    wr16(raw + BI_OFF_SUM, bootinfo_sum(raw));
    wr32(raw + BI_OFF_MAGIC, (u32)BOOTINFO_MAGIC);
    wr32(raw + BI_OFF_CHECK, (u32)BOOTINFO_CHECK);
}

void bootinfo_seal_image(u8 *raw, u32 crc, u32 size)
{
    wr32(raw + BI_OFF_IMG_CRC, crc);
    wr32(raw + BI_OFF_IMG_SIZE, size);
    wr32(raw + BI_OFF_IMG_CHECK, BOOTINFO_IMG_CHECK_OF(crc, size));
}

int bootinfo_drive_usable(const struct bootinfo_drive *d)
{
    if (d == 0) return 0;
    if (!d->queried) return 0;
    if (d->da != (u8)BOOTINFO_DA_HD0 && d->da != (u8)BOOTINFO_DA_HD1) return 0;
    if (d->cf != 0) return 0;
    if (d->seclen != (u16)BOOTINFO_SECLEN) return 0;
    if (d->heads == 0) return 0;
    if (d->spt == 0) return 0;
    if (d->cyl == 0) return 0;
    return 1;
}

static void clear_out(struct bootinfo *out)
{
    unsigned int i;
    u8 *p = (u8 *)out;

    for (i = 0; i < (unsigned int)sizeof(*out); i++) p[i] = 0;
}

int bootinfo_parse(const u8 *raw, unsigned int len, struct bootinfo *out)
{
    unsigned int i;

    if (out == 0) return BOOTINFO_ERR_ARG;
    clear_out(out);
    if (raw == 0 || len < (unsigned int)BOOTINFO_WIRE_SIZE) {
        out->status = BOOTINFO_ERR_ARG;
        return out->status;
    }

    out->magic = rd32(raw + BI_OFF_MAGIC);
    if (out->magic != (u32)BOOTINFO_MAGIC) {
        out->status = BOOTINFO_ERR_MAGIC;
        return out->status;
    }
    if (rd32(raw + BI_OFF_CHECK) != (u32)BOOTINFO_CHECK) {
        out->status = BOOTINFO_ERR_CHECK;
        return out->status;
    }
    out->version = rd16(raw + BI_OFF_VERSION);
    if (out->version != (u16)BOOTINFO_VERSION) {
        out->status = BOOTINFO_ERR_VERSION;
        return out->status;
    }
    out->ndrives = raw[BI_OFF_NDRIVES];
    if (out->ndrives != (u8)BOOTINFO_NDRIVES) {
        out->status = BOOTINFO_ERR_NDRIVES;
        return out->status;
    }
    if (rd16(raw + BI_OFF_SUM) != bootinfo_sum(raw)) {
        out->status = BOOTINFO_ERR_SUM;
        return out->status;
    }
    out->source = raw[BI_OFF_SOURCE];

    for (i = 0; i < (unsigned int)BOOTINFO_NDRIVES; i++) {
        const u8 *r = raw + BI_OFF_DRIVE0 + i * BI_DRIVE_SIZE;
        struct bootinfo_drive *d = &out->drive[i];

        d->da           = r[BI_DRV_DA];
        d->loader_valid = r[BI_DRV_VALID];
        d->cf           = r[BI_DRV_CF];
        d->ah           = r[BI_DRV_AH];
        d->seclen       = rd16(r + BI_DRV_BX);
        d->cyl          = rd16(r + BI_DRV_CX);
        d->heads        = r[BI_DRV_DH];
        d->spt          = r[BI_DRV_DL];
        d->queried      = r[BI_DRV_QUERIED];
        /* ローダの判定は信じ切らず、同じ規則で判定し直す。両方が 1 のときだけ使う
         * (ローダが 0 と書いたものを拾い直さない)。 */
        d->valid = (u8)((d->loader_valid == 1 && bootinfo_drive_usable(d)) ? 1 : 0);
    }
    /* イメージ欄 (v2)。主部とは別のチェック語で、合わなければ「記録なし」
     * (主部の status は OK のまま — 幾何は使える)。 */
    {
        u32 crc  = rd32(raw + BI_OFF_IMG_CRC);
        u32 size = rd32(raw + BI_OFF_IMG_SIZE);
        if (size != 0 &&
            rd32(raw + BI_OFF_IMG_CHECK) == BOOTINFO_IMG_CHECK_OF(crc, size)) {
            out->img_valid = 1;
            out->img_crc   = crc;
            out->img_size  = size;
        }
    }
    out->status = BOOTINFO_OK;
    return out->status;
}

/* ---- 表示行 ------------------------------------------------------------- */

struct linebuf {
    char *buf;
    int size;
    int len;
};

static void lb_ch(struct linebuf *lb, char c)
{
    if (lb->len + 1 < lb->size) {
        lb->buf[lb->len++] = c;
        lb->buf[lb->len] = '\0';
    }
}

static void lb_str(struct linebuf *lb, const char *s)
{
    while (*s) lb_ch(lb, *s++);
}

static void lb_dec(struct linebuf *lb, u32 v)
{
    char t[12];
    int n = 0;

    do {
        t[n++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0 && n < (int)sizeof(t));
    while (n > 0) lb_ch(lb, t[--n]);
}

static void lb_hex(struct linebuf *lb, u32 v, int digits)
{
    static const char hx[] = "0123456789abcdef";
    int i;

    for (i = digits - 1; i >= 0; i--) {
        lb_ch(lb, hx[(v >> (i * 4)) & 0xFUL]);
    }
}

static void lb_chs(struct linebuf *lb, u32 c, u32 h, u32 s)
{
    lb_dec(lb, c);
    lb_ch(lb, '/');
    lb_dec(lb, h);
    lb_ch(lb, '/');
    lb_dec(lb, s);
}

static int lb_init(struct linebuf *lb, char *buf, int size)
{
    lb->buf = buf;
    lb->size = size;
    lb->len = 0;
    if (buf == 0 || size <= 0) return -1;
    buf[0] = '\0';
    return 0;
}

int bootinfo_format_bios(const struct bootinfo *bi, int slot,
                         char *buf, int size)
{
    struct linebuf lb;
    const struct bootinfo_drive *d;

    if (lb_init(&lb, buf, size) != 0) return 0;
    if (bi == 0 || bi->status != BOOTINFO_OK) {
        lb_str(&lb, "[hdd] bios geom: none (magic ");
        lb_hex(&lb, bi ? bi->magic : 0UL, 8);
        lb_str(&lb, " err=");
        if (bi && bi->status < 0) {
            lb_ch(&lb, '-');
            lb_dec(&lb, (u32)(-bi->status));
        } else {
            lb_dec(&lb, 0UL);
        }
        lb_ch(&lb, ')');
        return lb.len;
    }
    if (slot < 0 || slot >= (int)BOOTINFO_NDRIVES) return 0;
    d = &bi->drive[slot];

    lb_str(&lb, "[hdd] bios da=");
    lb_hex(&lb, d->da, 2);
    if (!d->queried) {
        lb_str(&lb, " not queried");
        return lb.len;
    }
    lb_str(&lb, " cf=");
    lb_dec(&lb, d->cf);
    lb_str(&lb, " ah=");
    lb_hex(&lb, d->ah, 2);
    lb_str(&lb, " len=");
    lb_dec(&lb, d->seclen);
    lb_str(&lb, " C/H/S=");
    lb_chs(&lb, d->cyl, d->heads, d->spt);
    lb_str(&lb, " src=");
    lb_str(&lb, bi->source == BOOTINFO_SRC_FD ? "fd" :
                bi->source == BOOTINFO_SRC_HDD ? "hdd" : "?");
    if (!d->valid) lb_str(&lb, " (unusable)");
    return lb.len;
}

int bootinfo_format_ata(const struct bootinfo_ata *a, int drive,
                        char *buf, int size)
{
    struct linebuf lb;

    if (lb_init(&lb, buf, size) != 0) return 0;
    if (a == 0) return 0;
    lb_str(&lb, "[hdd] ata");
    lb_dec(&lb, (u32)drive);
    lb_str(&lb, " def=");
    lb_chs(&lb, a->def_cyl, a->def_heads, a->def_spt);
    lb_str(&lb, " cur=");
    lb_chs(&lb, a->cur_cyl, a->cur_heads, a->cur_spt);
    lb_str(&lb, a->cur_valid ? "(valid)" : "(n/a)");
    lb_str(&lb, " lba=");
    lb_dec(&lb, a->lba ? 1UL : 0UL);
    lb_str(&lb, " total=");
    lb_dec(&lb, a->total);
    return lb.len;
}
