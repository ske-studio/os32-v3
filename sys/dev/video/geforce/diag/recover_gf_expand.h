#ifndef KDEBUG_RECOVER_GF_EXPAND_H
#define KDEBUG_RECOVER_GF_EXPAND_H
/* HWDEV-local API; intentionally outside the shared contract umbrella. */
struct recover_profile;
struct gfinit_plan;
/* NULL buf resets metadata after detaching and releasing the backing plan. */
int recover_gf_profile_expand(struct recover_profile *p, struct gfinit_plan *buf);
#endif
