/* Fixed boundary expectations, independent of the implementation's formulas. */
#define assert(value) do { if (!(value)) return 1; } while (0)
#include "dma_contract.h"
int main(void)
{
    assert(!dma_range_ok(0, 0, 0x1000000));
    assert(dma_range_ok(0, 0x10000, 0x1000000));
    assert(dma_range_ok(0xffff, 1, 0x1000000));
    assert(!dma_range_ok(0xffff, 2, 0x1000000));
    assert(dma_range_ok(0xff0000, 0x10000, 0x1000000));
    assert(!dma_range_ok(0xffffff, 2, 0x1000000));
    assert(!dma_range_ok(0x1000000, 1, 0x1000000));
    assert(dma_range_ok(0xfffffffeU, 1, 0xffffffffU));
    assert(!dma_range_ok(0xfffffffeU, 4, 0xffffffffU));
    assert(dma_crosses_64k(0, 0));
    return 0;
}

void _start(void)
{
    int rc = main();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
