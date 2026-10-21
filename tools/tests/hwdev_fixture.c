/* Synthetic M2 body: no hardware, allocation, core calls or GF code. */
#include "hwdev/generated/hwdev_contract.h"
u32 hwdev_import_slots[HWDEV_IMPORT_COUNT] __attribute__((section(".data.kdebug_import_slots"), aligned(4))) = {0};
static int call(u32 op, void *req, u32 size, const struct recover_ops *base,
                struct recover_transport *transport, const struct recover_profile *s0)
{ (void)op; (void)req; (void)size; (void)base; (void)transport; (void)s0; return 42; }
static void release(void) {}
const struct hwdev_ops fixture_callbacks = {HWDEV_ABI, sizeof(struct hwdev_ops), call, release};
const struct hwdev_manifest fixture_manifest = {
    {HWDEV_ABI, sizeof(struct hwdev_identity), HWDEV_KIND_GF6200, 1,
     RECOVER_PROFILE_VERSION, FIXTURE_PAIR, 0x11223344U, HWDEV_CONTRACT_CRC},
    HWDEV_ABI, sizeof(struct hwdev_manifest), HWDEV_LAYOUT_CRC,
    HWDEV_LIFETIME_FLAGS, HWDEV_WORK_MAX_PAGES, 0, HWDEV_ABI,
    sizeof(struct hwdev_ops), 0
};
static int initialize(const struct hwdev_manifest **m, const struct hwdev_ops **ops)
{ *m = &fixture_manifest; *ops = &fixture_callbacks; return 0; }
struct fixture_exports { u32 version; int (*initialize)(const struct hwdev_manifest **, const struct hwdev_ops **); };
const struct fixture_exports hwdev_ops = {1, initialize};
int hwdev_init(struct fixture_exports *out) { *out = hwdev_ops; return 0; }
