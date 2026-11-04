/* Kernel-owned display data loan, GF_FEAT4_DESIGN C4.
 * This is an OS32 boundary, with no corresponding nouveau bind API.
 * EDID parsing replaces nouveau_connector.c:938-955 (DRM dependency);
 * TV data feeds dispnv04/tvmodesnv17.c:406-434. Neither helper does MMIO.
 */
#include "../../../hwdev/gf/display/api.h"

static const struct gf4_data_ops display_data = {
    .version = GF4_DATA_VERSION,
    .size = sizeof(struct gf4_data_ops),
    .edid_modes = gf4_edid_modes,
    .tv_state = gf4_tv_state_calc,
    .tv_modes = gf4_tv_modes
};

/* Initialize once: stop/restart and later loans must retain the image.
 * Parsers, mode generation and LUT initialization belong to B/C2 owners.
 */
static struct gf4_state display_state = {
    .version = GF4_STATE_VERSION,
    .size = sizeof(struct gf4_state)
};

i32 gf4_data_bind(const struct gf4_data_ops **data, struct gf4_state **state)
{
    if (!data || !state) return GF4_EINVAL;
    /* C3 owns the single production loan slots. It may clear both only
     * after complete module stop; never overwrite an outstanding loan.
     */
    if (*data || *state) return GF4_EBUSY;
    if (display_data.version != GF4_DATA_VERSION ||
        display_data.size != sizeof(struct gf4_data_ops) ||
        display_state.version != GF4_STATE_VERSION ||
        display_state.size != sizeof(struct gf4_state)) return GF4_ESTATE;
    *data = &display_data;
    *state = &display_state;
    return GF4_OK;
}
