#include "endian_dma.h"
#include "usb_platform.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_desc.h"
#include "drivers/usb/ohci_regs.h"
#include "drivers/usb/ohci_priv.h"
/* Runtime checks use known device bytes independently of the accessor pair:
 * inverse but equally broken get/put implementations must not pass. */
int main(void)
{
    const u32 values[] = { 0, 1, 0x12345678u, 0x80000000u, 0xffffffffu };
    struct ohci_ed_hw ed;
    unsigned int reg = 0;
    volatile u8 *bytes = (volatile u8 *)&ed.headp;
    volatile u8 *mm = (volatile u8 *)&reg;
    unsigned int i, j;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        u32 v = values[i];
        dma_le32_put(&ed.headp, v);
        mmio_wr32_le(&reg, v);
        for (j = 0; j < 4; ++j) {
            u8 want = (u8)(v >> (8 * j));
            if (bytes[j] != want || mm[j] != want) return 1;
        }
        if (dma_le32_get(&ed.headp) != v || mmio_rd32_le(&reg) != v) return 2;
    }
    bytes[0] = 0xef; bytes[1] = 0xcd; bytes[2] = 0xab; bytes[3] = 0x89;
    mm[0] = 0x10; mm[1] = 0x32; mm[2] = 0x54; mm[3] = 0x76;
    if (dma_le32_get(&ed.headp) != 0x89abcdefu) return 3;
    if (mmio_rd32_le(&reg) != 0x76543210u) return 4;
    return 0;
}
