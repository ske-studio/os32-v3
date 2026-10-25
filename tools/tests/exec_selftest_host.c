/* Kernel half of the exec fixture. The module half uses generated imports. */
#include "exec/exec.c"
#include "exec/ring3_str.c"
#include "exec/os32x_hdr.c"

/* Back the dynamic KAPI address with ordinary ILP32 process memory. */
u8 host_arena[KHEAP_SIZE + MEM_KAPI_SIZE + PAGE_SIZE]
    __asm__("__kernel_resident_end") __attribute__((aligned(PAGE_SIZE)));

static u32 host_flags = PTE_PRESENT | PTE_USER;
static int host_bad_string;
u32 paging_pte_flags(u32 addr) { (void)addr; return host_flags; }
const char *vfs_cwd(void) { return host_bad_string ? 0 : "/cwd"; }
const char *vfs_devname(const char *p) { (void)p; return "hd0"; }
const char *path_get_drive(void) { return "hd0"; }
const char *path_get_cwd(void) { return "/path"; }
void shell_service_wm_mark(void) { ring3_wm_shell_gen = 17; }
volatile u32 ring3_wm_shell_gen;
void *kmemset(void *p, int v, u32 n)
{
    u8 *b = p;
    for (u32 i = 0; i < n; i++) b[i] = (u8)v;
    return p;
}
void *kmemcpy(void *p, const void *q, u32 n)
{
    u8 *b = p; const u8 *a = q;
    for (u32 i = 0; i < n; i++) b[i] = a[i];
    return p;
}
char *kstrncpy(char *p, const char *q, u32 n)
{
    u32 i = 0;
    if (!n) return p;
    while (i + 1 < n && q[i]) { p[i] = q[i]; i++; }
    p[i] = 0;
    return p;
}
int kstrcmp(const char *a, const char *b)
{
    if (!a || !b) return a != b;
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
i32 __cdecl kapi_reserved_nosys(void) { return OS32_ERR_NOSYS; }
int kdi_get_ring3_in_syscall(void) { return ring3_in_syscall; }
void kdi_set_ring3_in_syscall(int v) { ring3_in_syscall = v; }
const u8 *kdi_ptr_ring3_ls_start(void) { return ring3_ls_start; }
const u8 *kdi_ptr_ring3_ls_end(void) { return ring3_ls_end; }
u32 *kdi_ptr___kernel_resident_end(void) { return &__kernel_resident_end; }

void host_init(void)
{
    kapi = (KernelAPI *)KAPI_ADDR;
    kmemset(kapi, 0, sizeof(*kapi));
    kapi->shm_base = MEM_SHM_BASE;
#if KAPI_FUNC_RESERVED > 0
    for (u32 i = 0; i < KAPI_FUNC_RESERVED; i++)
        kapi->kapi_reserved[i] = kapi_reserved_nosys;
#endif
    ring3_trampoline_init();
}
void host_flags_set(u32 flags) { host_flags = flags; }
int host_restored(void)
{
    return ring3_wm_depth == 0 && ring3_wm_depth_underflow == 0 &&
           ring3_wm_shell_gen == 0;
}
void host_string_set(int bad) { host_bad_string = bad; }
