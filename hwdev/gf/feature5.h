/* F4 X2 boundary only; F5 A0 supplies the remaining feature5 declarations. */
#ifndef HWDEV_GF_FEATURE5_H
#define HWDEV_GF_FEATURE5_H
#include "display/api.h"

struct gf5;
/* F5 C2 owns these typed links in feature5_links.c. owner borrows resident
 * struct gf5; NULL is GF5_EINVAL (-501). Stop/restart delegate to display's
 * reservation-preserving F4 suspend/restart with the original dl and rc.
 * Admission applies F5 phase/management-slot rules before F4 admission;
 * only final RUN clears saved_closed and publishes all slots/scan hooks. */
i32 gf5_link_stop(void *owner, u32 dl);
i32 gf5_link_restart(void *owner, u32 dl);
i32 gf5_link_admission(void *owner, bool open);
#endif
