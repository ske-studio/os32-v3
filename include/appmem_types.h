#ifndef APPMEM_TYPES_H
#define APPMEM_TYPES_H
#include "types.h"

/* TASK_T2_APPBAND §3-1 / §6-1: 512 B of the 1,376 B AS budget. */
#define APPMEM_EXTENT_MAX 32
#define APPMEM_EINVAL (-1)
#define APPMEM_ENOVA  (-2)
#define APPMEM_EFULL  (-3)
#define APPMEM_ENOSPC (-4)

#define APPMEM_MAP_EXACT   1U
#define APPMEM_MAP_TOPDOWN 2U

enum appmem_kind {
    APPMEM_LIBC_INITIAL = 1,
    APPMEM_EXEC_INITIAL,
    APPMEM_ANON,
    APPMEM_EXEC_ARENA,
    APPMEM_EXEC_LARGE
};

struct appmem_extent { u32 base, end, kind, flags; };
/* flags is internal metadata, independent of map_flags. EXEC_LARGE identity
 * is its extent/base, never USER metadata; adjacent LARGE extents never merge. */
struct appmem_table { struct appmem_extent e[APPMEM_EXTENT_MAX]; };
/* Native host fixtures use a wider u32 for pointer transport; byte budgets
 * are asserted in appmem.c with the real ILP32 build. */
STATIC_ASSERT(sizeof(struct appmem_extent) == 4 * sizeof(u32), appmem_extent_words);
STATIC_ASSERT(sizeof(struct appmem_table) == APPMEM_EXTENT_MAX * sizeof(struct appmem_extent), appmem_table_extents);

/* Image/stack/guard/shlib are not table entries.
 * LIBC_INITIAL is an extent in f4 (registered by f5).
 * f2/f3 primary_mapped_end excludes the fixed primary heap. In f5, register
 * LIBC_INITIAL [page_align(img_end), old primary_mapped_end) and pass
 * primary_mapped_end = page_align(img_end), so returned holes are reusable.
 * img_end may be unaligned; the other boundaries must be page aligned.
 * f5 supplies these saved kernel values, never caller-provided metadata. */
struct appmem_layout {
    u32 img_end, primary_mapped_end, exec_heap_cur_end, guard_b;
};

#endif
