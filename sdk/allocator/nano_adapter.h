#ifndef OS32_NANO_ADAPTER_H
#define OS32_NANO_ADAPTER_H
/* Private f1b connection, not an installed SDK API. f6 supplies the CRT
 * backend; f7 owns arena discovery, allocation routing and lifetime. */
#include <stdint.h>
#include <stddef.h>
#include <malloc.h>
#include <reent.h>
#define OS32_NANO_PAGE 4096u
struct os32_nano_arena {
    void *free_list;
    char *sbrk_start;
    struct mallinfo info;
    uintptr_t initial, brk, mapped_end, limit;
    /* USER: atomically map exactly [base, base+bytes), 0 on success.
     * Failure leaves mappings unchanged. Resident: NULL, fixed mapped_end.
     * This callback must never return a noncontiguous substitute mapping. */
    int (*grow_exact)(void *opaque, uintptr_t base, size_t bytes);
    void *opaque;
};
/* Caller owns zero initialization, disjoint arena backing and initial rounded
 * up to CHUNK_ALIGN (4) by f6 CRT; fixtures may violate alignment to exercise
 * upstream sbrk_aligned. Selection is
 * explicit here: caller-origin dispatch belongs to kernel f5/f9, not CR3. */
int os32_nano_select(struct os32_nano_arena *arena);
void *os32_nano_morecore(struct os32_nano_arena *arena, struct _reent *r, ptrdiff_t incr);
void *os32_nano_sbrk(struct _reent *r, ptrdiff_t incr);
struct mallinfo os32_nano_info(struct _reent *r);
#endif
