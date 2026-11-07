/* HW_BASIC_DESIGN §3: private shell/kdebug wire, never a public SDK ABI. */
#ifndef KDEBUG_HWB_WIRE_H
#define KDEBUG_HWB_WIRE_H
#include "types.h"

#define HW_OP 18U
#define HW_VERSION 1U
#define HW_REQ_BYTES 32U
#define HW_PCI_CONFIG_BYTES 256U
#define HW_BDF_BUS_SHIFT 8U
#define HW_BDF_DEV_SHIFT 3U
#define HW_BDF_BUS_MASK 255U
#define HW_BDF_DEV_MASK 31U
#define HW_BDF_FN_MASK 7U
#define HW_BDF_MASK 65535U
/* Arguments must be range checked before packing; do not mask bad inputs. */
#define HW_BDF(bus, dev, fn) (((u32)(bus) << HW_BDF_BUS_SHIFT) | \
                             ((u32)(dev) << HW_BDF_DEV_SHIFT) | (u32)(fn))
#define HW_BDF_BUS(bdf) (((u32)(bdf) >> HW_BDF_BUS_SHIFT) & HW_BDF_BUS_MASK)
#define HW_BDF_DEV(bdf) (((u32)(bdf) >> HW_BDF_DEV_SHIFT) & HW_BDF_DEV_MASK)
#define HW_BDF_FN(bdf) ((u32)(bdf) & HW_BDF_FN_MASK)

enum hw_action { HW_READ = 1, HW_WRITE = 2, HW_POINT = 3, HW_BLOCK32 = 4, HW_BLOCK8 = 5, HW_BLOCK16 = 6 };
#define HW_IS_BLOCK(action) ((action) == HW_BLOCK32 || (action) == HW_BLOCK8 || (action) == HW_BLOCK16)
#define HW_BLOCK_WIDTH(action) ((action) == HW_BLOCK8 ? 1U : (action) == HW_BLOCK16 ? 2U : 4U)
enum hw_space { HW_PCI = 1, HW_IO = 2, HW_PHYS = 3 };
enum hw_status { HW_OK = 0, HW_ARG = 1, HW_NOSYS = 2, HW_WINDOW = 3, HW_IO_ERROR = 4 };
struct hw_req {
    u32 version, action, space, width, bdf, addr, value, status;
};
STATIC_ASSERT(sizeof(struct hw_req) == HW_REQ_BYTES, hw_req_is_32);
/* Append-only extension: legacy actions still use the original 32 bytes.
 * BLOCK32 requires exactly 40 bytes, PHYS/width=4/bdf=0, count=1..1024,
 * BLOCK8/16 use the same extension, width=1/2, count <= 4096/2048.
 * increment=0/1. Values wrap at the access width. No implicit readback. */
#define HW_BLOCK32_MAX 1024U
#define HW_BLOCK_BYTES (HW_BLOCK32_MAX * 4U)
#define HW_BLOCK_REQ_BYTES 40U
struct hw_block_req {
    struct hw_req req;
    u32 count, increment;
};
STATIC_ASSERT(sizeof(struct hw_block_req) == HW_BLOCK_REQ_BYTES, hw_block_is_40);
#ifdef __KERNEL_BUILD__
/* Private dbg_call entry: validate size/version before access. Return 0 on
 * accepted request, OS32_ERR_INVAL on bad wire, OS32_ERR_NOSYS if unavailable.
 * No request output may be used on a nonzero call return. */
i32 hwb_access(void *buf, u32 size);
#endif
/* width is bytes (1/2/4). Input status=0. READ input value=0; failure value=0.
 * WRITE preserves input value, never reads back. POINT has zero unused fields,
 * returns consumed abort flag 0/1. Outputs valid only if call returns zero.
 * PCI absent mechanism: NOSYS/0; absent device: OK/all ones for that width. */
#endif
