/*
 * Copyright 1992-2003 by Alan Hourihane, North Wales, UK.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Alan Hourihane, alanh@fairlite.demon.co.uk
 */
/* MIT/X11 X/trident_dac.c:1218-1242, Tridentddc1Read; design section 8.
 * A single DDC1 sample only: neither EDID acquisition nor an init gate.
 */
#include "trident.h"

/* C:1226-1242; register indices: X/trident_regs.h:40,76. */
#define DDC1_NEW_SELECT 0x0Bu
#define DDC1_NEW_MODE1 0x0Eu
#define DDC1_NEW_UNLOCK 0x80u
#define DDC1_I2C 0x37u
#define DDC1_SDA_INPUT 0x04u
#define DDC1_DATA_BIT 0x01u
#define DDC1_VRETRACE 0x08u
/* cur_vga_base is the CRTC index port, four above X's vgaIOBase. */
#define DDC1_STATUS_OFFSET 6u
#define DDC1_SEQ_DATA (TRI_SR_INDEX + 1u)

#define TRY(op) do { int rc_ = (op); if (rc_) return rc_; } while (0)

int tri_ddc1_read(struct trident_device *d, u8 *bit)
{
    u8 temp, status, value;
    if (!d || !bit) return TRI_E_INVAL;
    u16 base = d->cur_vga_base;

    /* New mode: OUTB index, INB data (including the discarded read). */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, DDC1_NEW_SELECT, &temp));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, DDC1_NEW_MODE1, &temp));
    TRY(trident_write8(d, TRI_SPACE_REGS, DDC1_SEQ_DATA,
                       temp | DDC1_NEW_UNLOCK));

    /* Define SDA as input, then restore SR0E, both with one OUTW. */
    TRY(trident_indexed_write(d, base, DDC1_I2C, DDC1_SDA_INPUT));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, DDC1_NEW_MODE1, temp));

    /* C:1237-1239: finish the current retrace, then wait for the next.
     * The reference has no timeout or saved deadline for this callback. */
    do {
        TRY(trident_read8(d, TRI_SPACE_REGS,
                          base + DDC1_STATUS_OFFSET, &status));
    } while (status & DDC1_VRETRACE);
    do {
        TRY(trident_read8(d, TRI_SPACE_REGS,
                          base + DDC1_STATUS_OFFSET, &status));
    } while (!(status & DDC1_VRETRACE));

    TRY(trident_indexed_read(d, base, DDC1_I2C, &value));
    *bit = value & DDC1_DATA_BIT;
    return 0;
}
