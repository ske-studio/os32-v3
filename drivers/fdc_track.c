/* ======================================================================== */
/*  FDC_TRACK.C — FD の読み出しをトラック単位に束ねる (I/O を触らない)      */
/*                                                                          */
/*  ここには I/O もタイマも置かない。読むのは ops 経由だけ。                */
/*  理由と契約は fdc_track.h、試験は tools/tests/test_fdc_track.py。        */
/* ======================================================================== */

#include "fdc_track.h"

/* ======================================================================== */
/*  要求を 1 トラックの中に切る                                             */
/* ======================================================================== */
int fdc_track_split(const struct fdc_geom *g, u32 lba, u32 count,
                    struct fdc_run *run)
{
    u32 spt, heads, per_cyl, in_track, left;

    if (g == 0 || count == 0 || g->spt == 0 || g->heads == 0) return 0;

    spt = (u32)g->spt;
    heads = (u32)g->heads;
    per_cyl = spt * heads;

    run->cyl  = (int)(lba / per_cyl);
    run->head = (int)((lba / spt) % heads);
    run->sect = (int)(lba % spt) + 1;

    /* トラックの残り (sect..spt) と要求の残りの小さい方。
     * **ヘッドの境目でも切る** — MT (マルチトラック) は使わない
     * (理由は fdc.c の fdc_read_sectors の注記)。 */
    left = spt - (lba % spt);
    in_track = (count < left) ? count : left;
    run->count = (int)in_track;
    return run->count;
}

/* ======================================================================== */
/*  1 回で何番のセクタまで読むか                                            */
/* ======================================================================== */
int fdc_track_eot(const struct fdc_geom *g, const struct fdc_run *run)
{
    int last = run->sect + run->count - 1;

    if (run->sect < 1 || run->count < 1 || last > (int)g->spt) return 0;
#if FDC_TRACK_READ_AHEAD
    return (int)g->spt;
#else
    return last;
#endif
}

/* ======================================================================== */
/*  中身の出し入れ                                                          */
/* ======================================================================== */
void fdc_track_init(struct fdc_track_cache *c, int i, u8 *buf)
{
    if (i < 0 || i >= FDC_TRACK_SLOTS) return;
    c->slot[i].valid = 0;
    c->slot[i].buf = buf;
}

void fdc_track_init_sector(struct fdc_track_cache *c, int j, u8 *buf)
{
    if (j < 0 || j >= FDC_SECTOR_SLOTS) return;
    c->sec[j].valid = 0;
    c->sec[j].buf = buf;
}

void fdc_track_invalidate(struct fdc_track_cache *c)
{
    int i;
    for (i = 0; i < FDC_TRACK_SLOTS; i++) c->slot[i].valid = 0;
    for (i = 0; i < FDC_SECTOR_SLOTS; i++) c->sec[i].valid = 0;
    c->bad_valid = 0;
}

/* ======================================================================== */
/*  セクタキャッシュ (count == 1 の読みだけ)                                */
/* ======================================================================== */
static int fdc_sec_find(const struct fdc_track_cache *c, int drv,
                        const struct fdc_geom *g, u32 gen, u32 lba)
{
    int j;

    for (j = 0; j < FDC_SECTOR_SLOTS; j++) {
        const struct fdc_sector_ent *e = &c->sec[j];
        if (!e->valid || e->buf == 0) continue;
        if (e->gen != gen || e->geom != g) continue;
        if (e->drv != drv || e->lba != lba) continue;
        return j;
    }
    return -1;
}

/* 返したばかりのセクタを覚える: 空き、無ければいちばん長く使っていないもの。 */
static void fdc_sec_put(struct fdc_track_cache *c, const struct fdc_track_ops *ops,
                        int drv, const struct fdc_geom *g, u32 gen, u32 lba,
                        const u8 *src)
{
    int j, v = -1;

    if (g->bps > FDC_SECTOR_SLOT_BYTES) return;
    for (j = 0; j < FDC_SECTOR_SLOTS; j++) {
        if (c->sec[j].buf == 0) continue;
        if (!c->sec[j].valid) { v = j; break; }
        if (v < 0 || c->sec[j].used < c->sec[v].used) v = j;
    }
    if (v < 0) return;
    ops->copy(c->sec[v].buf, src, g->bps);
    c->sec[v].drv = drv;
    c->sec[v].lba = lba;
    c->sec[v].geom = g;
    c->sec[v].gen = gen;
    c->sec[v].used = ++c->clock;
    c->sec[v].valid = 1;
}

/* 先読みが失敗した印の付いたトラックか。 */
static int fdc_track_is_bad(const struct fdc_track_cache *c, int drv,
                            const struct fdc_geom *g, const struct fdc_run *run)
{
    return c->bad_valid && c->bad_geom == g && c->bad_drv == drv
        && c->bad_cyl == run->cyl && c->bad_head == run->head;
}

int fdc_track_hit(const struct fdc_track_cache *c, int drv,
                  const struct fdc_geom *g, u32 gen,
                  const struct fdc_run *run)
{
    int i;

    for (i = 0; i < FDC_TRACK_SLOTS; i++) {
        const struct fdc_track_slot *t = &c->slot[i];
        if (!t->valid || t->buf == 0) continue;
        /* 書き込み・Ready 変化・メディアの変更で世代が進んだら別物。 */
        if (t->gen != gen) continue;
        /* ジオメトリが変わった (1.44MB ⇔ 2HD の差し替え) なら別物。 */
        if (t->geom != g) continue;
        if (t->drv != drv || t->cyl != run->cyl || t->head != run->head) continue;
        if (run->sect < t->first) continue;
        if (run->sect + run->count - 1 > t->last) continue;
        return i;
    }
    return -1;
}

/* 埋める先のスロット: 空き、無ければいちばん長く使っていないもの。 */
static int fdc_track_victim(const struct fdc_track_cache *c)
{
    int i, v = -1;

    for (i = 0; i < FDC_TRACK_SLOTS; i++) {
        if (c->slot[i].buf == 0) continue;
        if (!c->slot[i].valid) return i;
        if (v < 0 || c->slot[i].used < c->slot[v].used) v = i;
    }
    return v;
}

/* 受け皿 (FDC_TRACK_MAX_BYTES) に 1 トラックが入るか。 */
static int fdc_track_fits(const struct fdc_geom *g)
{
    return (u32)g->spt * (u32)g->bps <= (u32)FDC_TRACK_MAX_BYTES;
}

/* 区間を 1 セクタずつ buff へ読む (旧来の読み方。束ねた読みの受け身)。 */
static int fdc_track_read_singly(const struct fdc_track_ops *ops, int drv,
                                 const struct fdc_geom *g,
                                 const struct fdc_run *run, u8 *dst)
{
    int i;

    for (i = 0; i < run->count; i++) {
        if (ops->read_one(ops->ctx, drv, run->cyl, run->head, run->sect + i,
                          g, dst + (u32)i * g->bps) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ======================================================================== */
/*  読み出し本体                                                            */
/* ======================================================================== */
static int fdc_track_read_inner(struct fdc_track_cache *c,
                                const struct fdc_track_ops *ops,
                                int drv, const struct fdc_geom *g,
                                u32 lba, u32 count, u8 *buff)
{
    struct fdc_run run;
    u8 *dst = buff;
    int single = (count == 1);
    u32 gen0 = 0;

    /* 1 セクタの読み (FatFs の窓の出し入れ) はまずセクタキャッシュ。 */
    if (single && g != 0 && g->spt != 0) {
        int j;
        gen0 = ops->gen(ops->ctx, drv);
        j = fdc_sec_find(c, drv, g, gen0, lba);
        if (j >= 0) {
            ops->copy(buff, c->sec[j].buf, g->bps);
            c->sec[j].used = ++c->clock;
            c->sec_hits++;
            return 0;
        }
    }

    while (count > 0) {
        int n = fdc_track_split(g, lba, count, &run);
        int hit;
        u32 bytes, gen;

        if (n <= 0) return -1;
        bytes = (u32)n * g->bps;
        /* 世代はこの区間の直前に聞く (前の区間の読みの途中で Ready 変化を
         * 見ていれば、ここで進んでいる)。 */
        gen = ops->gen(ops->ctx, drv);
        hit = fdc_track_hit(c, drv, g, gen, &run);

        if (hit >= 0) {
            /* 持っている中身から写す。 */
            struct fdc_track_slot *t = &c->slot[hit];
            ops->copy(dst, t->buf + (u32)(run.sect - t->first) * g->bps,
                      bytes);
            t->used = ++c->clock;
            c->trk_hits++;
        } else if (!fdc_track_fits(g) || fdc_track_victim(c) < 0) {
            /* 受け皿に入らないジオメトリ / 受け皿が無い。旧来どおり読む。 */
            if (fdc_track_read_singly(ops, drv, g, &run, dst) != 0) return -1;
        } else {
            int eot = fdc_track_eot(g, &run);
            int want_last = run.sect + run.count - 1;
            int mrc;
            struct fdc_track_slot *t = &c->slot[fdc_track_victim(c)];

            /* 先読みが落ちたトラックでは要求の範囲だけを読む。 */
            if (eot > want_last && fdc_track_is_bad(c, drv, g, &run)) {
                eot = want_last;
            }

            /* 先に捨てる — read_multi が途中まで t->buf を書き換えて
             * 失敗しても、半端な中身を当てない (印は残す)。 */
            t->valid = 0;
            mrc = (eot > 0)
                ? ops->read_multi(ops->ctx, drv, run.cyl, run.head, run.sect,
                                  eot - run.sect + 1, g, t->buf)
                : -2;
            if (mrc == 0) {
                t->drv = drv;
                t->cyl = run.cyl;
                t->head = run.head;
                t->first = run.sect;
                t->last = eot;
                t->geom = g;
                /* **読む前に聞いた世代**で覚える。読みの途中で Ready 変化を
                 * 見て世代が進んでいれば、この中身は次から当たらない。 */
                t->gen = gen;
                t->used = ++c->clock;
                t->valid = 1;
                c->fills++;
                ops->copy(dst, t->buf, bytes);
            } else if (mrc == -3) {
                /* NR (媒体無し)。1 セクタずつ読んでも同じなので打ち切る
                 * (ラリー 2 の Codex)。 */
                return -1;
            } else {
                /* 先読みの分まで読もうとして落ちたなら、このトラックに印を
                 * 付ける (次からは要求の範囲だけ)。 */
                if (eot > want_last) {
                    c->bad_valid = 1;
                    c->bad_drv = drv;
                    c->bad_cyl = run.cyl;
                    c->bad_head = run.head;
                    c->bad_geom = g;
                }
                /* まとめ読みが失敗した。**要求した区間だけ**を 1 セクタずつ
                 * 読み直す (リトライと回復は read_one = fdc 側が持つ)。
                 * 先読みの分まで読み直すと、傷んだセクタが要求の外に
                 * あるだけで読めるファイルまで失敗にしてしまう。 */
                if (fdc_track_read_singly(ops, drv, g, &run, dst) != 0) {
                    return -1;
                }
            }
        }

        dst += bytes;
        lba += (u32)n;
        count -= (u32)n;
    }

    /* 読めた 1 セクタを覚える。世代は読む前に聞いた値 (読みの途中で
     * Ready 変化を見て進んでいれば、次から当たらない)。 */
    if (single) fdc_sec_put(c, ops, drv, g, gen0, lba - 1u, buff);
    return 0;
}

/* ======================================================================== */
/*  入口: 2 秒規則を当ててから読む                                          */
/* ======================================================================== */
int fdc_track_read(struct fdc_track_cache *c, const struct fdc_track_ops *ops,
                   int drv, const struct fdc_geom *g,
                   u32 lba, u32 count, u8 *buff)
{
    u32 now = ops->now(ops->ctx);
    int rc;

    /* 最後の読みから FDC_TRACK_IDLE_TICKS を超えていたら両方捨てる
     * (fdc_track.h の注記)。差は符号無しで取る (tick_count の一周も可)。 */
    if (c->touched && (u32)(now - c->last_tick) > (u32)FDC_TRACK_IDLE_TICKS) {
        fdc_track_invalidate(c);
        c->idle_drops++;
    }
    rc = fdc_track_read_inner(c, ops, drv, g, lba, count, buff);
    /* 読み終えた時刻を残す (長い読みの途中で 2 秒が過ぎても、直後の読みで
     * 捨てないように)。 */
    c->touched = 1;
    c->last_tick = ops->now(ops->ctx);
    return rc;
}
