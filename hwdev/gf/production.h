/* P4/P5 connection. All objects/callbacks live until reboot (module pinned). */
#ifndef HWDEV_GF_PRODUCTION_H
#define HWDEV_GF_PRODUCTION_H
#include "internal.h"
#define GF_ROM_MAX KPEEK_NV_PROM_LEN /* existing acquisition ceiling */
#define GF_PRODUCTION_SUBSYS 0x127010fcU
#define GF_PRODUCTION_BAR0_BYTES 0x01000000U
#define GF_PRODUCTION_BAR1_BYTES 0x10000000U
#define GF_PRODUCTION_BAR2_BYTES 0x01000000U
#define GF_PRODUCTION_INDEXES 6U
#define GF_PRODUCTION_LOCKS 6U
#define GF_PRODUCTION_PFB_AMOUNT 0x10020cU
#define GF_PRODUCTION_PFB_MASK 0xff000000U
#define GF_PRODUCTION_PFB_MIN 0x01000000U
/* Why the bus-0 resource scan rejected (reason=0: nothing recorded). The
 * production copy records this instead of printing (decode not yet safe). */
struct gfinit_resource_reject { const char *reason; u32 dev, fn, reg, raw; };
struct gf_production_resources {
    struct recover_identity identity;
    struct recover_bar_extent bars[RECOVER_BAR_COUNT];
    struct gfinit_resource_reject reject; /* set when the bus-0 scan rejects */
};
/* Failure record target for a resource reject: BDF (dev<<3|fn) << 8 | reg. */
#define GF_PRODUCTION_REJECT_TARGET(r) ((((r)->dev << 3 | (r)->fn) << 8) | (r)->reg)
struct gf_production_local { u32 alias, index, value; bool valid; };
struct gf_production_scratch {
    struct gf_init_context init;
    struct gf_init_program program;
    u8 rom[GF_ROM_MAX];
};
struct gf_production {
    struct gf_display_context display;
    struct video_device video;
    struct gf_production_scratch *scratch;
    /* route98 only selects/verifies 00: no VRAM, console or redraw here. */
    int (*route98)(void *);
    void *route_opaque;
    bool prepared, attempted, revoked, acquiring;
    /* Last resource-gate reject (no console output on this path). */
    struct gfinit_resource_reject reject;
    struct gf_production_local indexes[GF_PRODUCTION_INDEXES];
    struct gf_production_local locks[GF_PRODUCTION_LOCKS];
};
/* Read-only boot identification/resource check. P5 reserves ALL three decode
 * ranges, maps BAR0 and the BAR1 DISPLAY with paging_map_phys(supervisor+PCD)
 * before freeze, and passes this snapshot to prepare. No BAR sizing/writes.
 * Ra266's GPU is on bus 0; other bridge forwarding windows are exclusions. */
int gf_production_identify(struct gf_production_resources *out);
/* Read-only, once per zero-initialized p. Checks snapshot, reservation and
 * mappings. Does NOT devinit. First video_start(&p->video,...) does that. */
int gf_production_prepare(struct gf_production *p,
                          const struct gf_production_resources *boot,
                          int (*route98)(void *), void *opaque);
/* P5 failure/leave/shutdown/reboot order, even if stop fails:
 * gf_production_quiesce -> route00 -> gf_production_decode_safe -> 98 cells /
 * restore/redraw. Return values are 0/-1; never touch 98 VRAM if unsafe.
 * video_stop itself remains local cleanup + GPU quiesce (P3 contract). */
int gf_production_quiesce(struct gf_production *p);
int gf_production_decode_safe(struct gf_production *p);
/* Adapter internals, also exercised by the fake PCI/MMIO tests. */
int gf_production_identity(struct gf_production *p);
int gf_production_mapped(struct gf_production *p);
int gf_production_read(void *, u32, u32, u32, u32 *);
int gf_production_write(void *, u32, u32, u32, u32);
int gf_production_point(void *);
int gf_production_restore(void *, struct gf_init_context *);
int gf_production_clock(void *, u32 *);
int gf_production_wait(void *, u32);
int gf_production_mmio(struct gf_production *, u32, u32, u32, bool, u32 *);
extern const struct gf_display_owner gf_production_owner;
#endif
