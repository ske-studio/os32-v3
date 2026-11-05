#include "fep_store_fixture.h"

void fep_test_reset(FepTestFixture *f)
{
    unsigned char *p = (unsigned char *)f;
    for (u32 i = 0; i < sizeof(*f); i++) p[i] = 0;
}

int fep_test_settings(const FepTestFixture *f, int *present, int *kb,
                      const char **user_path)
{
    if (f->settings_rc) return f->settings_rc;
    *present = f->reserve_present;
    *kb = f->reserve_kb;
    *user_path = f->user_path;
    return 0;
}

void fep_test_script(FepTestFixture *f, const FepTestReply *replies, u32 count)
{
    f->script = replies;
    f->count = count;
    f->next = 0;
    f->mismatch = 0;
}

const FepTestReply *fep_test_next(FepTestFixture *f, enum FepTestOp op)
{
    static const FepTestReply failure = {FEP_TEST_STEP, SQLITE_MISUSE, 0, 0};
    if (f->mismatch || f->next >= f->count || f->script[f->next].op != op) {
        f->mismatch = 1;
        return &failure;
    }
    return &f->script[f->next++];
}

int fep_test_done(const FepTestFixture *f)
{
    return !f->mismatch && f->next == f->count;
}

int fep_test_resident_take(FepTestFixture *f)
{
    if (f->resident_isolated) return SQLITE_BUSY;
    if (f->resident_live >= OS32_SQLITE_RESIDENT_GROUPS) return SQLITE_FULL;
    f->resident_live++;
    return SQLITE_OK;
}

void fep_test_resident_finish(FepTestFixture *f, int isolated)
{
    if (isolated) { f->resident_isolated = 1; return; }
    if (!f->resident_live) { f->mismatch = 1; return; }
    f->resident_live--;
}
