/* Read-only display state after routing to 98. MIT nouveau c03c1a1:
 * dispnv04/hw.c:682-689, hw.h:127-136; nvreg.h:153,257,301,468-478.
 * CR31 is indexed: only the selector byte is written and restored. Never
 * write CR data, CR44 owner, cursor position, PCRTC or PVIDEO state here. */
#ifndef GEFORCE_LEAVE_LOG_H
#define GEFORCE_LEAVE_LOG_H
#define LEAVE_CR 0x6013d4U
#define LEAVE_STRIDE 0x2000U
static void geforce_leave_log(struct gf_io *io,int route_rc)
{
    u32 cr[2]={0},v[10]={0},valid=0;
    int rc=0;
    if (!io || !io->read || !io->write) rc=GF4_ESTATE;
    else {
        for (u32 h=0;h<2U;h++) {
            u32 port=LEAVE_CR+h*LEAVE_STRIDE,index=0;
            int err=io->read(io->opaque,0,port,1,&index);
            if (!err) {
                err=io->write(io->opaque,0,port,1,0x31U);
                if (!err) err=io->read(io->opaque,0,port+1U,1,&cr[h]);
                if (!err) valid|=1U<<h;
                int restore=io->write(io->opaque,0,port,1,index);
                if (!err) err=restore;
            }
            if (!rc) rc=err;
        }
        /* Cursor positions, PCRTC starts, STOP, INTR_EN, buffer offsets/limits. */
        static const u32 offsets[]={0x680300U,0x682300U,0x600800U,0x602800U,
            0x8704U,0x8140U,0x8920U,0x8924U,0x8908U,0x890cU};
        for (u32 i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
            int err=io->read(io->opaque,0,offsets[i],4,&v[i]);
            if (!err) valid|=1U<<(i+2U);
            if (!rc) rc=err;
        }
    }
    kprintf(KLOG_ATTR,"GF4 STATE stage=leave98 route_rc=%d read_rc=%d valid=%08x h0_cr31=%02x h1_cr31=%02x h0_cursor=%08x h1_cursor=%08x h0_start=%08x h1_start=%08x pvideo_stop=%08x pvideo_intr_en=%08x offset0=%08x offset1=%08x limit0=%08x limit1=%08x\n",
            route_rc,rc,valid,cr[0],cr[1],v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]);
}
#endif
