#ifndef OS32_NANO_ADAPTER_H
#define OS32_NANO_ADAPTER_H
/* Private SDK implementation interface; not a public application API. */
#include <stdint.h>
#include <stddef.h>
#include <malloc.h>
#include <reent.h>
#define OS32_NANO_PAGE 4096u
#define OS32_NANO_LARGE 65536u
#define OS32_NANO_TOPDOWN 2u
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
    struct os32_nano_arena *next;
    uintptr_t map_base;
    size_t live;
};
/* Explicit selection is a private fixture/inspection seam; production malloc
 * starts at the configured primary and free/realloc find the pointer owner.
 * Callers zero initialize fixture state; CRT initializes the real primary.
 * Configuration is one-time and cannot replace live arenas or run while busy. */
int os32_nano_select(struct os32_nano_arena *arena);
int os32_nano_configure(struct os32_nano_arena *primary,
                        void *(*map)(void *, size_t, unsigned),
                        int (*unmap)(void *, uintptr_t, size_t), void *opaque);
void *os32_nano_crt_sbrk(struct _reent *r, ptrdiff_t incr);
void *os32_nano_morecore(struct os32_nano_arena *arena, struct _reent *r, ptrdiff_t incr);
void *os32_nano_sbrk(struct _reent *r, ptrdiff_t incr);
struct mallinfo os32_nano_info(struct _reent *r);
#ifndef OS32_CRT_RESIDENT
/* Internal USER tail reclamation; successful pages, or zero while busy. */
size_t os32_nano_trim(void);
#endif
#endif
