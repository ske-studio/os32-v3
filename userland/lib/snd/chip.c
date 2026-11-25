#include "live.h"
#include "fmvoice.h"

typedef struct {
    u32 id, hw, requested;
    FmVoice *fm;
    SNDL_ChipGot got;
} Chip;
static Chip slots[8];
static u32 next_id;

static Chip *lookup(u32 id)
{
    Chip *c = &slots[(id-1) % 8];
    return id && c->id == id ? c : 0;
}

static void *allocate(uint32_t bytes)
{
    return sound_api->mem_alloc(bytes);
}

static int clock_now(u32 *lo)
{
    u32 hi;
    return sound_api->sys_time_now(lo, &hi);
}

static int fits(const SNDL_ChipReq *r, const SND2_ChipCaps *c)
{
    return (c->chip == r->chip || (r->chip == SND2_CHIP_OPN && c->chip == SND2_CHIP_OPNA)) &&
           c->fm_ch >= r->fm_ch && c->ssg_ch >= r->ssg_ch && c->banks >= r->banks &&
           !(r->need & ~c->flags) && (!(r->need & SND2_CHF_ADPCM) || c->adpcm_bytes);
}

static int hardware(Chip *slot, const SNDL_ChipReq *r, int fall)
{
    int n = sound_api->snd2_dev_count();
    for (int i = 0; i < n; ++i) {
        SND2_DevInfo info;
        SND2_ChipCaps c;
        if (sound_api->snd2_dev_info(i, &info) || info.kind != SND2_KIND_FM ||
            sound_api->snd2_chip_caps(info.dev, &c, sizeof c)) continue;
        if (!c.adpcm_bytes) c.flags &= ~SND2_CHF_ADPCM;
        u32 lost = 0, channels = 0;
        if (fall) {
            if (r->chip != SND2_CHIP_OPNA || r->fm_ch > 6 || r->banks > 2 ||
                r->ssg_ch > c.ssg_ch) continue;
            if (c.chip == SND2_CHIP_OPN && c.fm_ch >= 3 && c.banks >= 1) {
                lost = SND2_CHF_RHYTHM | SND2_CHF_ADPCM;
                channels = 3;
            } else if (c.chip == SND2_CHIP_OPNA && c.fm_ch >= r->fm_ch && c.banks >= r->banks &&
                       !c.adpcm_bytes && (r->need & SND2_CHF_ADPCM)) {
                lost = SND2_CHF_ADPCM;
            } else continue;
            if (r->need & ~(lost | c.flags)) continue;
        } else if (!fits(r, &c)) continue;
        if (sound_api->snd2_chip_open(info.dev, &slot->hw)) continue;
        slot->got = (SNDL_ChipGot){sizeof(slot->got), fall ? SNDL_HW_FALL : SNDL_HW,
                                   c.chip, c.fm_ch, c.ssg_ch, c.flags, lost, channels};
        return 0;
    }
    return OS32_ERR_NOSYS;
}

static int software(Chip *slot, const SNDL_ChipReq *r, const SND2_Fmt *fmt)
{
    SND2_ChipCaps c = {.chip=SND2_CHIP_OPNA, .fm_ch=6, .ssg_ch=3, .banks=2,
                       .adpcm_bytes=0x40000, .flags=SND2_CHF_TIMER | SND2_CHF_ADPCM};
    if ((fmt->rate != 44100 && fmt->rate != 22050) || !fits(r, &c)) return OS32_ERR_NOSYS;
    u32 now;
    int rc = clock_now(&now);
    if (rc) return rc;
    FmvMemory mem = {allocate, sound_api->mem_free};
    rc = fmv_new(&slot->fm, mem, now, 0, 0);
    if (rc) return rc;
    rc = sndl_soft_pcm_acquire(fmt);
    if (rc) {
        fmv_delete(slot->fm);
        slot->fm = 0;
        return rc;
    }
    slot->got = (SNDL_ChipGot){sizeof(slot->got), SNDL_SOFT, c.chip, 6, 3, c.flags, 0, 0};
    return 0;
}

int sndl_chip_open(const SNDL_ChipReq *rq, u32 *handle, SNDL_ChipGot *got)
{
    if (!sound_api || !rq || !handle || !got || rq->size != sizeof(*rq) ||
        rq->rsv || (rq->flags & ~SNDL_REQ_PREFER_SOFT)) return OS32_ERR_INVAL;
    SNDL_ChipReq r = *rq;
    int specified = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (r.pol[i] > SNDL_POL_FAIL) return OS32_ERR_INVAL;
        specified |= r.pol[i];
    }
    if (!specified) {
        r.pol[0] = SNDL_POL_SOFT;
        r.pol[1] = SNDL_POL_FALLDOWN;
        r.pol[2] = SNDL_POL_FAIL;
    }
    if (!r.chip) r.chip = SND2_CHIP_OPN;
    if (!r.fm_ch) r.fm_ch = r.chip == SND2_CHIP_OPN ? 3 : 6;
    if (!r.ssg_ch) r.ssg_ch = 3;
    if (!r.banks) r.banks = r.chip == SND2_CHIP_OPN ? 1 : 2;
    u32 mode = 0;
    SND2_Fmt fmt = {0};
    int devices = sound_api->snd2_dev_count();
    for (int i = 0; i < devices; ++i) {
        SND2_DevInfo info;
        if (sound_api->snd2_dev_info(i, &info)) continue;
        mode = info.flags & SND2_DEV_FM_MASK;
        if (info.kind == SND2_KIND_PCM_OUT && (info.flags & SND2_DEV_SOFT_CPU)) {
            SND2_Caps caps;
            if (!sound_api->snd2_caps(info.dev, &caps, sizeof caps) && caps.fmt_count)
                fmt = caps.fmt[0];
        }
    }
    if (mode == SND2_DEV_FM_NONE) return OS32_ERR_NOSYS;
    Chip *s = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if (!++next_id) ++next_id;
        s = &slots[(next_id-1) % 8];
        if (!s->id) break;
    }
    if (s->id) return OS32_ERR_NOSPC;
    int rc = OS32_ERR_NOSYS;
    if (mode != SND2_DEV_FM_HW && mode != SND2_DEV_FM_SOFT && (r.flags & SNDL_REQ_PREFER_SOFT))
        rc = software(s, &r, &fmt);
    if (rc && mode != SND2_DEV_FM_SOFT) rc = hardware(s, &r, 0);
    for (unsigned i = 0; rc && i < 4 && r.pol[i] && r.pol[i] != SNDL_POL_FAIL; ++i) {
        if (r.pol[i] == SNDL_POL_SOFT && mode != SND2_DEV_FM_HW) rc = software(s, &r, &fmt);
        if (r.pol[i] == SNDL_POL_FALLDOWN && mode != SND2_DEV_FM_SOFT) rc = hardware(s, &r, 1);
    }
    if (rc) return OS32_ERR_NOSYS;
    s->requested = r.need;
    *handle = s->id = next_id;
    *got = s->got;
    return 0;
}

static int discarded(const Chip *s, const SND2_RegW *w)
{
    if (s->got.backend != SNDL_HW_FALL) return 0;
    if (s->got.chip == SND2_CHIP_OPN)
        return w->bank || (w->reg >= 0x10 && w->reg <= 0x1d) || w->reg == 0x29;
    return w->bank && w->reg <= 0x10;
}

int sndl_chip_write(u32 handle, const SND2_RegW *w, u32 n, u32 at_us)
{
    Chip *s = lookup(handle);
    if (!s || n > 256 || (n && !w)) return OS32_ERR_INVAL;
    for (u32 i = 0; i < n; ++i) if (w[i].bank > 1) return OS32_ERR_INVAL;
    if (s->got.backend == SNDL_HW) return sound_api->snd2_chip_write(s->hw, w, n);
    for (u32 i = 0; i < n; ++i) {
        if (discarded(s, &w[i])) continue;
        int rc = s->fm ? fmv_write(s->fm, at_us, 256*w[i].bank + w[i].reg, w[i].val)
                       : sound_api->snd2_chip_write(s->hw, &w[i], 1);
        if (rc != (s->fm ? 0 : 1)) return i ? (int)i : rc;
    }
    return n;
}

int sndl_chip_read(u32 handle, u32 bank, u32 what, u32 reg)
{
    Chip *s = lookup(handle);
    if (!s || bank > 1 || reg > 255 || (what != 1 && what != 2)) return OS32_ERR_INVAL;
    if (s->fm) {
        u32 now;
        unsigned val;
        int rc = clock_now(&now);
        if (!rc) rc = what == 1 ? fmv_status(s->fm, now, bank, &val)
                                : fmv_read(s->fm, now, 256*bank + reg, &val);
        return rc ? rc : (int)val;
    }
    int adpcm = (s->requested & SND2_CHF_ADPCM) != 0;
    if (s->got.backend == SNDL_HW_FALL && bank) {
        if (adpcm && what == 2 && reg == 8) return 0;
        if (s->got.chip == SND2_CHIP_OPN)
            return what == 2 ? 255 : (adpcm ? 12 : 0);
    }
    int rc = sound_api->snd2_chip_read(s->hw, bank, what, reg);
    if (rc >= 0 && bank && what == 1 && s->got.backend == SNDL_HW_FALL && adpcm) rc |= 12;
    return rc;
}

int sndl_chip_prepare(struct SndlWindow *window)
{
    u32 now = window->now;
    int have_clock = window->offset != 0;
    /* Reserve timer events for every instance before advancing audio on any. */
    for (unsigned i = 0; i < 8; ++i) {
        if (!slots[i].fm) continue;
        if (!have_clock) {
            int rc = clock_now(&now);
            if (rc) return rc;
            window->now = now;
            have_clock = 1;
        }
        int rc = fmv_render(slots[i].fm, now, 0, 0);
        if (rc) return rc;
    }
    return 0;
}

int sndl_chip_mix(int32_t *stereo, unsigned frames, unsigned rate, const struct SndlWindow *window)
{
    static int16_t pcm[4096];
    unsigned stride = rate == 22050 ? 2 : 1;
    for (unsigned i = 0; i < 8; ++i) {
        if (!slots[i].fm) continue;
        int rc = fmv_render_part(slots[i].fm, window->now, (unsigned char *)pcm,
                                 window->frames*stride, window->offset*stride, frames*stride);
        if (rc) return rc;
        for (unsigned j = 0; j < frames; ++j) {
            stereo[2*j] += pcm[2*j*stride];
            stereo[2*j+1] += pcm[2*j*stride+1];
        }
    }
    return 0;
}

int sndl_chip_close(u32 handle)
{
    Chip *s = lookup(handle);
    if (!s) return OS32_ERR_INVAL;
    int rc = s->fm ? sndl_pcm_release(SND2_CLOSE_DRAIN) : sound_api->snd2_chip_close(s->hw);
    if (!rc || !s->fm || !sndl_pcm_active()) {
        fmv_delete(s->fm);
        *s = (Chip){0};
    }
    return rc;
}
