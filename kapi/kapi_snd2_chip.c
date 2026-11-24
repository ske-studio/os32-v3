#include "../sound/hal/snd2_int.h"

/* Public indices are dense across both registries. Each registry returns
 * the stable dev ID used by the subsequent PCM or chip operations. */
int kapi_snd2_dev_count(void)
{
    SND2_DevInfo info;
    u32 chips = 0;
    while (snd2_chip_dev_info(chips, &info) == 0)
        ++chips;
    return snd2_dev_count() + chips;
}

int kapi_snd2_dev_info(u32 index, SND2_DevInfo *out)
{
    u32 pcm = snd2_dev_count();
    if (index < pcm)
        return snd2_dev_info(index, out);
    return snd2_chip_dev_info(index - pcm, out);
}
