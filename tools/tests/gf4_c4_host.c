/* C4: libc-free A1 UNIT 14, linked with real gf_display_data.c, edid.c,
 * tv_data.c. No local dependency stubs, external oracle reads, or MMIO.
 * Bind/loan expectations come from GF_FEAT4_DESIGN C4, not helper output.
 */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"

static const struct gf4_data_ops *data;
static struct gf4_state *state;
static struct gf4_state before;

static void unchanged(void)
{
    assert(!gf2_host_memcmp(state, &before, sizeof(before)));
}

static void invalid_and_busy(void)
{
    assert(gf4_data_bind(NULL, &state) == GF4_EINVAL);
    assert(!state);
    assert(gf4_data_bind(&data, NULL) == GF4_EINVAL);
    assert(!data);
    assert(gf4_data_bind(NULL, NULL) == GF4_EINVAL);
    assert(gf4_data_bind(&data, &state) == GF4_OK);
    assert(data && state);
    assert(data->version == GF4_DATA_VERSION && data->size == sizeof(*data));
    assert(state->version == GF4_STATE_VERSION && state->size == sizeof(*state));
    /* All non-header storage starts zero; compare the complete image. */
    before = (struct gf4_state){
        .version = GF4_STATE_VERSION, .size = sizeof(before)
    };
    unchanged();
    /* Arbitrary mode count marks live caller-owned data; it is not an
     * expected hardware value. A failed or later bind must preserve it.
     */
    state->nmodes = GF4_MODES;
    state->head[GF4_HEADS - 1U].blank = true;
    before = *state;
    const struct gf4_data_ops *loan_data = data;
    struct gf4_state *loan_state = state;
    assert(gf4_data_bind(&data, &state) == GF4_EBUSY);
    assert(data == loan_data && state == loan_state);
    unchanged();
    data = NULL;
    assert(gf4_data_bind(&data, &state) == GF4_EBUSY);
    assert(!data && state == loan_state);
    unchanged();
    data = loan_data; state = NULL;
    assert(gf4_data_bind(&data, &state) == GF4_EBUSY);
    assert(data == loan_data && !state);
    /* Simulate complete stop, then clear C3's loan slots. */
    data = NULL;
    assert(gf4_data_bind(&data, &state) == GF4_OK);
    assert(data == loan_data && state == loan_state);
    unchanged();
}

static void schema_rejected(void)
{
    const struct gf4_data_ops *loan_data = data;
    struct gf4_state *loan_state = state;
    /* Wrong version and correct-version/wrong-size are independent. */
    for (u32 which = 0; which < 2U; ++which) {
        if (!which) loan_state->version = GF4_STATE_VERSION + 1U;
        else loan_state->size = sizeof(*loan_state) - 1U;
        before = *loan_state;
        data = NULL; state = NULL;
        assert(gf4_data_bind(&data, &state) == GF4_ESTATE);
        assert(!data && !state);
        assert(!gf2_host_memcmp(loan_state, &before, sizeof(before)));
        loan_state->version = GF4_STATE_VERSION;
        loan_state->size = sizeof(*loan_state);
        assert(gf4_data_bind(&data, &state) == GF4_OK);
        assert(data == loan_data && state == loan_state);
    }
}

static void helpers(void)
{
    /* Exact typed binding, so no wrapper can swallow a helper's rc. */
    assert(data->edid_modes == gf4_edid_modes);
    assert(data->tv_state == gf4_tv_state_calc);
    assert(data->tv_modes == gf4_tv_modes);
    struct gf4_edid_state parsed;
    struct gf4_tv_state tv, tv_before;
    u8 invalid_edid[GF4_HOST_EDID_BYTES] = {0};
    assert(data->edid_modes(NULL, 0, &parsed) == GF4_EINVAL);
    assert(data->edid_modes(invalid_edid, 0, &parsed) == GF4_EEDID);
    assert(parsed.last_rc == GF4_EEDID && !parsed.base_valid);
    /* Norm order: nouveau dispnv04/tvnv17.h:56-70.
     * Native 720p values: tvmodesnv17.c:178-229, tvnv17.c:251-292.
     * These independent facts exercise both real TV helper dispatches.
     */
    enum { TV_HD720P = 10, TV_NORMS = 12 };
    assert(data->tv_modes(TV_HD720P, &parsed) == GF4_OK);
    assert(parsed.base_valid && parsed.ntiming && parsed.preferred == 0);
    const struct gf4_timing *t = &parsed.timing[parsed.preferred];
    assert(t->khz == 74250 && t->hd == 1280 && t->vd == 720);
    assert(data->tv_state(TV_HD720P, t, &tv) == GF4_OK);
    assert(tv.version == GF4_TV_VERSION && tv.size == sizeof(tv));
    assert(tv.norm == TV_HD720P);
    tv_before = tv;
    assert(data->tv_state(TV_NORMS, t, &tv) == GF4_EINVAL);
    assert(!gf2_host_memcmp(&tv, &tv_before, sizeof(tv)));
    struct gf4_edid_state parsed_before = parsed;
    assert(data->tv_modes(TV_NORMS, &parsed) == GF4_EINVAL);
    assert(!gf2_host_memcmp(&parsed, &parsed_before, sizeof(parsed)));
}

void _start(void)
{
    invalid_and_busy();
    schema_rejected();
    before = *state;
    helpers();
    unchanged();
    gf2_host_exit(0);
}
