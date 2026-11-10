/* PowerMac early LAN interface; wire headers are encoded byte by byte. */
#ifndef POWERMAC_LAN_BOOT_H
#define POWERMAC_LAN_BOOT_H

#include "types.h"

typedef unsigned long long u64;
struct gem;
struct gem_dma;
struct ppc_boot_alias;

enum lan_rc {
    LAN_OK = 0,
    LAN_AGAIN = 1,
    LAN_EINVAL = -1,
    LAN_ENODEV = -2,
    LAN_ERANGE = -3,
    LAN_ETIME = -4,
    LAN_EIO = -5,
    LAN_EUNSUPPORTED = -6,
    LAN_EBUSY = -7,
};

enum pln1_status {
    PLN1_STATUS_OK = 0,
    PLN1_STATUS_UNSUPPORTED = 1,
    PLN1_STATUS_BAD_LENGTH = 2,
    PLN1_STATUS_RANGE = 3,
    PLN1_STATUS_CONFLICT = 4,
    PLN1_STATUS_STALE = 5,
    PLN1_STATUS_BUSY = 6,
    PLN1_STATUS_FAULT = 7,
};

#define PM_GMAC_PROFILE_UNINORTH 1u

#define PLN1_MAGIC 0x504c4e31u
#define PLN1_VERSION 1
#define PLN1_TYPE_BEACON 1
#define PLN1_TYPE_LOG 2
#define PLN1_TYPE_REQ 3
#define PLN1_TYPE_RSP 4
#define PLN1_HEADER_BYTES 80
#define PLN1_ETHERTYPE 0x88b6
#define PLN1_PAYLOAD_MAX 256
#define PLN1_OFF_MAGIC 0
#define PLN1_OFF_VERSION 4
#define PLN1_OFF_TYPE 5
#define PLN1_OFF_HEADER_BYTES 6
#define PLN1_OFF_BOOT_ID 8
#define PLN1_OFF_SEQ 24
#define PLN1_OFF_DROP_RECORDS 32
#define PLN1_OFF_DROP_BYTES 40
#define PLN1_OFF_REQUEST_ID 48
#define PLN1_OFF_OPCODE 52
#define PLN1_OFF_STATUS 54
#define PLN1_OFF_PAYLOAD_BYTES 56
#define PLN1_OFF_FLAGS 58
#define PLN1_OFF_RESERVED 60
#define PLN1_OFF_HOST_NONCE 64
#define PLN1_OP_STATUS 1
#define PLN1_OP_CLOCK 2
#define PLN1_OP_MEMORY 3
#define PLN1_OP_READ_DIAG 4
#define PLN1_OP_PEEK 5
#define PLN1_REQ_STATUS_BYTES 0
#define PLN1_REQ_CLOCK_BYTES 0
#define PLN1_REQ_MEMORY_BYTES 0
#define PLN1_REQ_READ_DIAG_BYTES 4
#define PLN1_REQ_PEEK_BYTES 8

struct pm_pci_window { u32 ss, pci_base, cpu_base, bytes; };
#define PM_GMAC_BOOT_ID_OVERRIDE 1u
#define PM_GMAC_PEER_OVERRIDE 2u
struct pm_gmac_boot {
    u32 root_node, node, root_pa, bus_first, bus_last;
    u32 bdf, bar_reg, bar_bus, bar_pa, bar_bytes, vendor_device;
    u8 mac[6], peer_mac[6], boot_id[16];
    u32 nwin;
    struct pm_pci_window win[8];
    u32 dma_cpu_base, dma_bus_base, dma_bytes, profile;
    u32 override_flags;
};
struct pm_pci { u32 addr_va, data_va, bus_first, bus_last, target_bdf; };
struct lan_message {
    u8 boot_id[16], host_nonce[16];
    u64 seq, drop_records, drop_bytes;
    u32 request_id;
    u16 opcode, status, payload_bytes, flags;
    u8 type, payload[256];
};

int pm_gmac_capture(struct pm_gmac_boot *out);
int pm_gmac_clock(const struct pm_gmac_boot *b, u32 clock_va);
void pm_gmac_boot_id(struct pm_gmac_boot *b, u64 tb, u32 pvr);
int pm_pci_read32(struct pm_pci *, u32 bdf, u32 reg, u32 *value);
int pm_pci_write32(struct pm_pci *, u32 bdf, u32 reg, u32 value);
/* Pointer is equivalent to the design's out[4], with an incomplete type. */
int pm_gmac_alias_plan(const struct pm_gmac_boot *, struct ppc_boot_alias *out);
/* 0: proceed (Master read back clear, or GMAC absent/unknown and PCI untouched); negative: Master remained set after clearing, stop. */
void pm_gmac_quiesce_setup(const struct pm_gmac_boot *, const struct pm_pci *);
int pm_gmac_early_quiesce(void);
int pm_gmac_dma_init(const struct pm_gmac_boot *, struct gem_dma *out);
int pm_dma_bus(u32 pa, u32 bytes, u32 *bus);
int pm_lan_log_register(void);
/* Scratch remains valid until the next peek; complete only after a peek.
 * LAN_OK removes the head, LAN_AGAIN retains it. status bit 0 means saturation. */
int pm_lan_log_peek(const u8 **data, u32 *len, u64 *seq,
                    u64 *records, u64 *bytes, u32 *status);
void pm_lan_log_stats(u64 *next, u64 *records, u64 *bytes, u32 *flags);
void pm_lan_log_complete(int result);
int pm_lan_start(const struct pm_gmac_boot *);
void pm_lan_poll(u64 now);
int pm_lan_encode(u8 *dst, u32 cap, const struct lan_message *m, u32 *used);
int pm_lan_decode(const u8 *src, u32 bytes, struct lan_message *m);
struct pm_lan_range { u32 base, size; };
/* The service supplies valid fields and excludes RX buffers from peek_ok. */
struct pm_lan_diag {
    const u32 *status; u32 status_words; /* <= 64 */
    const u32 *memory; u32 memory_words; /* <= 64 */
    const u8 *diag;                     /* 256 bytes */
    const struct pm_lan_range *peek_ok; u32 peek_ok_count; /* <= 8 */
    void (*clock)(u32 *tick, u32 *tb_hz, u64 *tb, u64 *us);
    int (*translate)(u32 va, u32 *pa);
    int (*copy8)(u32 va, u8 *out);
};
/* Call only with a free response slot and after validating the peer and request. */
int pm_lan_monitor(const struct lan_message *req, const u8 src_mac[6],
                   const struct pm_lan_diag *d, struct lan_message *rsp);
int pm_lan_copy8(u32 va, u8 *out);

_Static_assert(LAN_OK == 0, "LAN_OK");
_Static_assert(LAN_AGAIN == 1, "LAN_AGAIN");
_Static_assert(LAN_EINVAL == -1, "LAN_EINVAL");
_Static_assert(LAN_ENODEV == -2, "LAN_ENODEV");
_Static_assert(LAN_ERANGE == -3, "LAN_ERANGE");
_Static_assert(LAN_ETIME == -4, "LAN_ETIME");
_Static_assert(LAN_EIO == -5, "LAN_EIO");
_Static_assert(LAN_EUNSUPPORTED == -6, "LAN_EUNSUPPORTED");
_Static_assert(LAN_EBUSY == -7, "LAN_EBUSY");
_Static_assert(PLN1_STATUS_OK == 0, "PLN1_STATUS_OK");
_Static_assert(PLN1_STATUS_UNSUPPORTED == 1, "PLN1_STATUS_UNSUPPORTED");
_Static_assert(PLN1_STATUS_BAD_LENGTH == 2, "PLN1_STATUS_BAD_LENGTH");
_Static_assert(PLN1_STATUS_RANGE == 3, "PLN1_STATUS_RANGE");
_Static_assert(PLN1_STATUS_CONFLICT == 4, "PLN1_STATUS_CONFLICT");
_Static_assert(PLN1_STATUS_STALE == 5, "PLN1_STATUS_STALE");
_Static_assert(PLN1_STATUS_BUSY == 6, "PLN1_STATUS_BUSY");
_Static_assert(PLN1_STATUS_FAULT == 7, "PLN1_STATUS_FAULT");
_Static_assert(PLN1_MAGIC == 0x504c4e31u, "PLN1_MAGIC");
_Static_assert(PLN1_VERSION == 1, "PLN1_VERSION");
_Static_assert(PLN1_TYPE_BEACON == 1, "PLN1_TYPE_BEACON");
_Static_assert(PLN1_TYPE_LOG == 2, "PLN1_TYPE_LOG");
_Static_assert(PLN1_TYPE_REQ == 3, "PLN1_TYPE_REQ");
_Static_assert(PLN1_TYPE_RSP == 4, "PLN1_TYPE_RSP");
_Static_assert(PLN1_HEADER_BYTES == 80, "PLN1_HEADER_BYTES");
_Static_assert(PLN1_ETHERTYPE == 0x88b6, "PLN1_ETHERTYPE");
_Static_assert(PLN1_PAYLOAD_MAX == 256, "PLN1_PAYLOAD_MAX");
_Static_assert(PLN1_OFF_MAGIC == 0, "PLN1_OFF_MAGIC");
_Static_assert(PLN1_OFF_VERSION == 4, "PLN1_OFF_VERSION");
_Static_assert(PLN1_OFF_TYPE == 5, "PLN1_OFF_TYPE");
_Static_assert(PLN1_OFF_HEADER_BYTES == 6, "PLN1_OFF_HEADER_BYTES");
_Static_assert(PLN1_OFF_BOOT_ID == 8, "PLN1_OFF_BOOT_ID");
_Static_assert(PLN1_OFF_SEQ == 24, "PLN1_OFF_SEQ");
_Static_assert(PLN1_OFF_DROP_RECORDS == 32, "PLN1_OFF_DROP_RECORDS");
_Static_assert(PLN1_OFF_DROP_BYTES == 40, "PLN1_OFF_DROP_BYTES");
_Static_assert(PLN1_OFF_REQUEST_ID == 48, "PLN1_OFF_REQUEST_ID");
_Static_assert(PLN1_OFF_OPCODE == 52, "PLN1_OFF_OPCODE");
_Static_assert(PLN1_OFF_STATUS == 54, "PLN1_OFF_STATUS");
_Static_assert(PLN1_OFF_PAYLOAD_BYTES == 56, "PLN1_OFF_PAYLOAD_BYTES");
_Static_assert(PLN1_OFF_FLAGS == 58, "PLN1_OFF_FLAGS");
_Static_assert(PLN1_OFF_RESERVED == 60, "PLN1_OFF_RESERVED");
_Static_assert(PLN1_OFF_HOST_NONCE == 64, "PLN1_OFF_HOST_NONCE");
_Static_assert(PLN1_OP_STATUS == 1, "PLN1_OP_STATUS");
_Static_assert(PLN1_OP_CLOCK == 2, "PLN1_OP_CLOCK");
_Static_assert(PLN1_OP_MEMORY == 3, "PLN1_OP_MEMORY");
_Static_assert(PLN1_OP_READ_DIAG == 4, "PLN1_OP_READ_DIAG");
_Static_assert(PLN1_OP_PEEK == 5, "PLN1_OP_PEEK");
_Static_assert(PLN1_REQ_STATUS_BYTES == 0, "PLN1_REQ_STATUS_BYTES");
_Static_assert(PLN1_REQ_CLOCK_BYTES == 0, "PLN1_REQ_CLOCK_BYTES");
_Static_assert(PLN1_REQ_MEMORY_BYTES == 0, "PLN1_REQ_MEMORY_BYTES");
_Static_assert(PLN1_REQ_READ_DIAG_BYTES == 4, "PLN1_REQ_READ_DIAG_BYTES");
_Static_assert(PLN1_REQ_PEEK_BYTES == 8, "PLN1_REQ_PEEK_BYTES");

#endif
