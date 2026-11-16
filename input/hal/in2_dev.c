#include "in2_int.h"
#include "keymap.h"
#include "io.h"
#include "os32_kapi_shared.h"

static struct {
    u16 generation;
    u8 kind, origin, used;
} sources[IN2_MAX_DEV];

int input_src_register(u8 kind, u8 origin, u16 *generation)
{
    unsigned int flags = irq_save();
    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) {
        if (!sources[i].used) {
            sources[i].generation++;
            sources[i].kind = kind;
            sources[i].origin = origin;
            sources[i].used = 1;
            *generation = sources[i].generation;
            irq_restore(flags);
            return (int)i + 1;
        }
    }
    irq_restore(flags);
    return OS32_ERR_FULL;
}

void input_src_release(u8 src, u16 generation)
{
    unsigned int flags = irq_save();
    if (src >= 1 && src <= IN2_MAX_DEV && sources[src - 1].used &&
        sources[src - 1].generation == generation) {
        km_source_release(src, generation);
        input_state_release(src, generation);
        sources[src - 1].used = 0;
    }
    irq_restore(flags);
}

int input_src_info(u8 src, u16 generation, u8 *kind, u8 *origin)
{
    if (src < 1 || src > IN2_MAX_DEV || !sources[src - 1].used ||
        generation != sources[src - 1].generation) return 0;
    *kind = sources[src - 1].kind;
    *origin = sources[src - 1].origin;
    return 1;
}
