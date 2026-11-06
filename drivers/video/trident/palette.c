/* TridentLoadPalette, trident_dac.c:1259-1282 (MIT/X11): per color,
 * mask/index/RGB writes each followed by DACDelay (two ST01 reads).
 * The hardware script trident_mode.hwb:350-358 instead sets mask/index once
 * and streams RGB without these reads. ST01 uses the current VGA base. */
#include "trident.h"

static int palette_write(struct trident_device *d, u32 reg, u8 value)
{
    u8 status;
    int rc = trident_write8(d, TRI_SPACE_REGS, reg, value);
    if (rc) return rc;
    rc = trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6, &status);
    if (rc) return rc;
    return trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6, &status);
}

int trident_palette(struct trident_device *d, int first, int count, const u8 *rgb)
{
    if (!d || !rgb || first < 0 || first >= (int)TRI_DAC_COLORS ||
        count <= 0 || count > (int)TRI_DAC_COLORS - first)
        return TRI_E_INVAL;
    for (int i = 0; i < count; ++i) {
        int rc = palette_write(d, TRI_DAC_MASK, 0xFF);
        if (rc) return rc;
        rc = palette_write(d, TRI_DAC_WRITE_INDEX, (u8)(first + i));
        if (rc) return rc;
        for (int component = 0; component < 3; ++component) {
            rc = palette_write(d, TRI_DAC_DATA,
                               trident_palette_dac6(*rgb++, GFX_PALETTE_COMPONENT_BITS));
            if (rc) return rc;
        }
    }
    return TRI_OK;
}
