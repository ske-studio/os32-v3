/* Host state tests supply a deterministic replacement. */
#if defined(H3_FIXTURE) && !defined(H3_SELF_IDENTITY)
#define H3_SELF_IDENTITY(a, o, g) (h3_api->caller_identity((a), (o), (g)) == 0)
#endif
#ifdef H3_SELF_IDENTITY
static int h3_publish_identity(volatile H3Block *b)
{
    unsigned long app = 0, owner = 0, generation = 0;
    if (!H3_SELF_IDENTITY(&app, &owner, &generation) ||
        !app || !owner || !generation) return 0;
    b->owner = owner;
    b->generation = generation; /* commit word, existing protocol */
    return 1;
}
#endif

static int h3_identity(volatile H3Block *b)
{
#ifdef H3_SELF_IDENTITY
    if (!b->generation && !h3_publish_identity(b)) return 0;
#endif
    if (!b->owner || !b->generation) return 0;
    b->phase = H3_IDENTIFIED;
    return 1;
}

static unsigned int h3_consume(volatile H3Block *b)
{
    unsigned int mode;
    if (b->phase != H3_RESUMED || b->arm != 1 || b->consumed != 0)
        return 0;
    mode = b->mode;
    if (mode < H3_PF || mode > H3_KAPI_LOOP) {
        b->phase = H3_ERROR;
        return 0;
    }
    b->arm = 0;
    b->consumed++;
    b->phase = H3_ARMED;
    return mode;
}
