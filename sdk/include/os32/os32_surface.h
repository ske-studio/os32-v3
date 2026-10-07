/* Public value ABI, KAPI v70. All addresses are caller virtual addresses. */
#ifndef OS32_PUBLIC_SURFACE_H
#define OS32_PUBLIC_SURFACE_H
#define OS32_SURFACE_MAX 4
#define OS32_SURFACE_CLIENT 1
#define OS32_SURFACE_DISPLAY 2
#define OS32_SURFACE_TVRAM 3
#define OS32_SURFACE_UNICODE 4
#define OS32_SURFACE_PC98 1
#define OS32_SURFACE_PEGC 2
#define OS32_SURFACE_CIRRUS 3
#define OS32_SURFACE_RW 1
#define OS32_SURFACE_RO 2
#define OS32_SURFACE_PLANAR4 0
#define OS32_SURFACE_PACKED8 1
#define OS32_SURFACE_TABLE 2
#define OS32_SURFACE_TEXT 3
typedef struct { unsigned long sid, generation; } OS32_SurfaceRef;
typedef struct {
    OS32_SurfaceRef ref;
    unsigned long role, backend, format, width, height, pitch, planes;
    unsigned long plane_offset[OS32_SURFACE_MAX];
    unsigned long bytes, access_max;
} OS32_SurfaceDesc;
typedef struct {
    unsigned long count;
    OS32_SurfaceDesc desc[OS32_SURFACE_MAX];
} OS32_SurfaceQueryResult;
typedef struct { unsigned long token, base, bytes, planes[OS32_SURFACE_MAX]; } OS32_LeaseView;
typedef struct { unsigned long count; OS32_LeaseView views[OS32_SURFACE_MAX]; } OS32_LeaseResult;
#endif
