/* Shared reserved-cell round trip. The adapter owns tracing/safe points. */
#ifndef PC98_CELL_H
#define PC98_CELL_H
#include "types.h"
#include <stdbool.h>
#define PC98_CELL_MASK 0xffffU
static inline int pc98_cell_check(int (*access)(void *, u32, bool, u32 *), void *opaque)
{
    u32 saved, actual;
    if (access(opaque, 2, false, &saved)) return -1;
    u32 test = (saved ^ PC98_CELL_MASK) & PC98_CELL_MASK;
    int rc = access(opaque, 3, true, &test);
    /* A failed write can still alter the cell: always restore once. */
    if (!rc && (access(opaque, 4, false, &actual) || actual != test)) rc = -1;
    if (access(opaque, 5, true, &saved)) return -1;
    if (access(opaque, 7, false, &actual) || actual != saved) return -1;
    return rc;
}
#endif
