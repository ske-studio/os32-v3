#ifndef KDEBUG_GFINIT_H
#define KDEBUG_GFINIT_H
#include "recover.h"
#include "gfinit_wire.h"
STATIC_ASSERT(sizeof(struct gfinit_request) == 40, gfinit_request_size);
/* Release terminal work only after initialized state is invalidated. */
void gfinit_recover_end(void);
int gfinit_call(u32 op, void *buf, u32 size, const struct recover_ops *base,
                struct recover_transport *transport, const struct recover_profile *s0);
#endif
