#include "sndl.h"
#include "os32_sndl_table.h"

__asm__(".globl __os32_snd_dependency\n.set __os32_snd_dependency, 1");

extern KernelAPI *kapi;

static const u32 *sndl_table(KernelAPI *api)
{
    const OS32ShlibHeader *header = (const OS32ShlibHeader *)SNDL_BASE;
    const u32 *table = (const u32 *)(header + 1);
    static KernelAPI *bound_api;
    if (!api) return 0;
    if (bound_api == api) return table;
    if (header->magic != OS32_SHLIB_MAGIC || header->version != SNDL_TABLE_VERSION ||
        header->_rsvd[0] != OS32_SHLIB_PROTOCOL || header->nfunc <= SNDL_E_CHIP_CLOSE) return 0;
    if (((int (*)(KernelAPI *))table[SNDL_E_INIT])(api)) return 0;
    bound_api = api;
    return table;
}

int sndl_init(KernelAPI *api)
{
    return sndl_table(api) ? 0 : -1;
}

int sndl_voice_new(const SND2_Fmt *src, unsigned long *voice)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(const SND2_Fmt *src, unsigned long *voice))table[SNDL_E_VOICE_NEW])(src, voice);
}

int sndl_voice_submit(unsigned long voice, const void *pcm, unsigned long bytes,
                      unsigned long flags, unsigned long *buf_id)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice, const void *pcm, unsigned long bytes,
                      unsigned long flags, unsigned long *buf_id))table[SNDL_E_VOICE_SUBMIT])(voice, pcm, bytes, flags, buf_id);
}

int sndl_voice_set(unsigned long voice, unsigned long hz, signed long vol_cb, signed long pan_cb)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice, unsigned long hz, signed long vol_cb, signed long pan_cb))table[SNDL_E_VOICE_SET])(voice, hz, vol_cb, pan_cb);
}

int sndl_voice_play(unsigned long voice)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice))table[SNDL_E_VOICE_PLAY])(voice);
}

int sndl_voice_stop(unsigned long voice)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice))table[SNDL_E_VOICE_STOP])(voice);
}

int sndl_voice_reset(unsigned long voice)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice))table[SNDL_E_VOICE_RESET])(voice);
}

int sndl_voice_delete(unsigned long voice)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice))table[SNDL_E_VOICE_DELETE])(voice);
}

int sndl_voice_done(unsigned long voice, unsigned long *buf_id, unsigned long *how)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(unsigned long voice, unsigned long *buf_id, unsigned long *how))table[SNDL_E_VOICE_DONE])(voice, buf_id, how);
}

int sndl_render(short *pcm, unsigned long frames, unsigned long rate)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(short *pcm, unsigned long frames, unsigned long rate))table[SNDL_E_RENDER])(pcm, frames, rate);
}

int sndl_open(const SND2_Fmt *fmt, u32 period, u32 lead)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(const SND2_Fmt *fmt, u32 period, u32 lead))table[SNDL_E_OPEN])(fmt, period, lead);
}

int sndl_pump(void)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(void))table[SNDL_E_PUMP])();
}

int sndl_prime_slice(void)
{
    const u32 *table = sndl_table(kapi);
    const OS32ShlibHeader *header = (const OS32ShlibHeader *)SNDL_BASE;
    if (!table || header->nfunc <= SNDL_E_PRIME_SLICE) return OS32_ERR_NOSYS;
    return ((int (*)(void))table[SNDL_E_PRIME_SLICE])();
}

int sndl_close(u32 how)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(u32 how))table[SNDL_E_CLOSE])(how);
}

int sndl_voice_pos(u32 voice, u32 *buffer, u32 *play, u32 *gen)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(u32 voice, u32 *buffer, u32 *play, u32 *gen))table[SNDL_E_VOICE_POS])(voice, buffer, play, gen);
}

int sndl_chip_open(const SNDL_ChipReq *rq, u32 *handle, SNDL_ChipGot *got)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(const SNDL_ChipReq *rq, u32 *handle, SNDL_ChipGot *got))table[SNDL_E_CHIP_OPEN])(rq, handle, got);
}

int sndl_chip_write(u32 handle, const SND2_RegW *w, u32 n, u32 at_us)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(u32 handle, const SND2_RegW *w, u32 n, u32 at_us))table[SNDL_E_CHIP_WRITE])(handle, w, n, at_us);
}

int sndl_chip_read(u32 handle, u32 bank, u32 what, u32 reg)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(u32 handle, u32 bank, u32 what, u32 reg))table[SNDL_E_CHIP_READ])(handle, bank, what, reg);
}

int sndl_chip_close(u32 handle)
{
    const u32 *table = sndl_table(kapi);
    if (!table) return -1;
    return ((int (*)(u32 handle))table[SNDL_E_CHIP_CLOSE])(handle);
}
