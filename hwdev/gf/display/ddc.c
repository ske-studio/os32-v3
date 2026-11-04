/*
 * Copyright 2015 Red Hat Inc.
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial busions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

/* NV44 DDC: GF_FEAT4_DESIGN §4 and the B3 addendum.
 * MIT nouveau c03c1a1 nvkm/subdev/i2c/busnv04.c:35-70: drive RMW,
 * release polarity and sense bits; bit.c:78-224: START/STOP, MSB first,
 * ACK and repeated START. OS32 uses the caller's absolute deadline.
 * dispnv04/hw.c:74-104, hw.h:119-136: head0 indexed access/owner.
 * ENGINE_CTRL is left untouched (crtc.c:496-499, nvreg.h:174-175). */
#include "api.h"

#define DDC_INDEX 0x6013d4U
#define DDC_DATA (DDC_INDEX + 1U)
#define DDC_OWNER 0x44U
#define DDC_SCL 0x20U
#define DDC_SDA 0x10U
#define DDC_ENABLE 0x01U
#define DDC_SENSE_SCL 0x04U
#define DDC_SENSE_SDA 0x08U
#define DDC_HALF_US 5U /* Design §4; nouveau bit.c:29 T_HOLD=5000ns. */
#define DDC_BLOCK_BYTES 128U
#define DDC_EDID_WRITE 0xa0U /* EDID 7-bit address 0x50. */
#define DDC_EDID_READ 0xa1U
#define DDC_SEGMENT_WRITE 0x60U /* E-DDC 7-bit address 0x30. */

struct ddc_bus {
    struct gf_io *io;
    struct gf4_i2c_port port;
    u32 dl;
};

static int ddc_read_reg(struct ddc_bus *b,u8 index,u32 *out)
{
    int rc=b->io->write(b->io->opaque,GF_MODE_REGISTER_BAR,DDC_INDEX,1,index);
    return rc ? rc : b->io->read(b->io->opaque,GF_MODE_REGISTER_BAR,DDC_DATA,1,out);
}

static int ddc_write_reg(struct ddc_bus *b,u8 index,u32 value)
{
    int rc=b->io->write(b->io->opaque,GF_MODE_REGISTER_BAR,DDC_INDEX,1,index);
    return rc ? rc : b->io->write(b->io->opaque,GF_MODE_REGISTER_BAR,DDC_DATA,1,value);
}

static int ddc_remaining(struct ddc_bus *b,u32 *left)
{
    u32 now;
    int rc=b->io->timing.clock_us(b->io->opaque,&now);
    if (rc) return rc;
    if ((i32)(now-b->dl)>=0) return GF4_ETIME;
    *left=b->dl-now;
    return 0;
}

static int ddc_delay(struct ddc_bus *b)
{
    u32 left;
    int rc=ddc_remaining(b,&left);
    if (rc) return rc;
    u32 us=left<DDC_HALF_US ? left : DDC_HALF_US;
    rc=b->io->timing.wait_us(b->io->opaque,us);
    if (rc) return rc;
    return ddc_remaining(b,&left);
}

/* No wait here: cleanup must still release the lines at an expired dl. */
static int ddc_lines(struct ddc_bus *b,bool scl,bool sda)
{
    u32 value;
    int rc=ddc_read_reg(b,b->port.drive,&value);
    if (rc) return rc;
    value=(value&~(DDC_SCL|DDC_SDA))|DDC_ENABLE;
    if (scl) value|=DDC_SCL;
    if (sda) value|=DDC_SDA;
    return ddc_write_reg(b,b->port.drive,value);
}

static int ddc_half(struct ddc_bus *b,bool scl,bool sda)
{
    u32 left,value;
    int rc=ddc_remaining(b,&left);
    if (rc) return rc;
    rc=ddc_lines(b,scl,sda);
    if (rc) return rc;
    if (scl) for (;;) {
        rc=ddc_remaining(b,&left);
        if (rc) return rc;
        rc=ddc_read_reg(b,b->port.sense,&value);
        if (rc) return rc;
        if (value&DDC_SENSE_SCL) break;
        rc=ddc_delay(b);
        if (rc) return rc;
    }
    return ddc_delay(b);
}

static int ddc_start(struct ddc_bus *b)
{
    int rc=ddc_half(b,false,true);
    if (!rc) rc=ddc_half(b,true,true);
    if (!rc) rc=ddc_half(b,true,false);
    if (!rc) rc=ddc_lines(b,false,false);
    return rc;
}

static int ddc_stop(struct ddc_bus *b)
{
    /* Raw transitions also run after timeout; waits keep the original dl.
     * Release SDA only after attempting to release SCL (bit.c:102-115). */
    int rc=ddc_lines(b,false,false);
    int next=ddc_delay(b);
    if (!rc) rc=next;
    next=ddc_lines(b,true,false);
    if (!rc) rc=next;
    /* With time left, honour stretching on the STOP clock too. */
    next=ddc_half(b,true,false);
    if (!rc) rc=next;
    next=ddc_lines(b,true,true);
    if (!rc) rc=next;
    next=ddc_delay(b);
    return rc ? rc : next;
}

static int ddc_bit(struct ddc_bus *b,bool sda,bool sample,u32 *bit)
{
    int rc=ddc_half(b,false,sda);
    if (!rc) rc=ddc_half(b,true,sda);
    if (!rc && sample) {
        u32 value;
        rc=ddc_read_reg(b,b->port.sense,&value);
        if (!rc) *bit=!!(value&DDC_SENSE_SDA);
    }
    if (!rc) rc=ddc_lines(b,false,sda);
    return rc;
}

static int ddc_put(struct ddc_bus *b,u8 value)
{
    for (u32 mask=0x80U;mask;mask>>=1) {
        int rc=ddc_bit(b,!!(value&mask),false,0);
        if (rc) return rc;
    }
    u32 nack;
    int rc=ddc_bit(b,true,true,&nack);
    return rc ? rc : (nack ? GF4_EDDC : 0);
}

static int ddc_get(struct ddc_bus *b,u8 *out,bool last)
{
    u32 value=0;
    for (u32 n=0;n<8U;++n) {
        u32 bit;
        int rc=ddc_bit(b,true,true,&bit);
        if (rc) return rc;
        value=(value<<1)|bit;
    }
    int rc=ddc_bit(b,last,false,0); /* ACK all but the last byte. */
    if (!rc) *out=(u8)value;
    return rc;
}

int gf4_ddc_read(struct gf4_ctx *ctx,u8 bus,u8 block,u8 out[128],u32 dl)
{
    if (!ctx) return GF4_EINVAL;
    if (!out || !ctx->ch || !ctx->ch->f || !ctx->links ||
        !ctx->links->i2c_port) return ctx->last_rc=GF4_EINVAL;
    struct ddc_bus b={.io=&ctx->ch->f->io,.dl=dl};
    if (!b.io->read || !b.io->write || !b.io->timing.clock_us ||
        !b.io->timing.wait_us) return ctx->last_rc=GF4_EINVAL;
    int rc=ctx->links->i2c_port(ctx->links->owner,bus,&b.port);
    if (rc) return ctx->last_rc=rc;
    u32 owner=0,drive=0,left;
    bool owner_saved=false,drive_saved=false;
    u8 bytes[DDC_BLOCK_BYTES];
    rc=ddc_remaining(&b,&left);
    if (!rc) rc=ddc_read_reg(&b,DDC_OWNER,&owner);
    if (!rc) {
        owner_saved=true; /* A failing write may already have applied. */
        rc=ddc_write_reg(&b,DDC_OWNER,0);
    }
    if (!rc) rc=ddc_read_reg(&b,b.port.drive,&drive);
    if (!rc) {
        drive_saved=true;
        rc=ddc_start(&b);
    }
    if (!rc && block>=2U) {
        rc=ddc_put(&b,DDC_SEGMENT_WRITE);
        if (!rc) rc=ddc_put(&b,(u8)(block/2U));
        if (!rc) rc=ddc_start(&b);
    }
    if (!rc) rc=ddc_put(&b,DDC_EDID_WRITE);
    if (!rc) rc=ddc_put(&b,(u8)((block&1U)*DDC_BLOCK_BYTES));
    if (!rc) rc=ddc_start(&b);
    if (!rc) rc=ddc_put(&b,DDC_EDID_READ);
    for (u32 n=0;!rc && n<DDC_BLOCK_BYTES;++n)
        rc=ddc_get(&b,&bytes[n],n==DDC_BLOCK_BYTES-1U);
    if (drive_saved) {
        int cleanup=ddc_stop(&b);
        if (!rc) rc=cleanup;
        cleanup=ddc_lines(&b,true,true);
        if (!rc) rc=cleanup;
        cleanup=ddc_write_reg(&b,b.port.drive,drive);
        if (!rc) rc=cleanup;
    }
    if (owner_saved) {
        int cleanup=ddc_write_reg(&b,DDC_OWNER,owner);
        if (!rc) rc=cleanup;
    }
    if (!rc) for (u32 n=0;n<DDC_BLOCK_BYTES;++n) out[n]=bytes[n];
    ctx->last_rc=rc;
    return rc;
}
