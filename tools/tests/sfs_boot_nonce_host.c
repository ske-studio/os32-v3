/* Real persistent owner, two process boots supplied by argv; no libc. */
#include "types.h"
#include PRODUCT_SOURCE
static u8 boot;
void boot_token_get(u8 out[16])
{
    for (unsigned int i = 0; i < 16; ++i) out[i] = 0;
    out[0] = boot;
}
void host_alive_snapshot(struct HostAlive *out) { out->valid = 0; }
static u32 now(void *ctx) { (void)ctx; return 0; }
static int wait(void *ctx) { (void)ctx; return 1; }
int sfs_client_hello_deadline(SfsClient *c, u32 value, u32 deadline)
{
    (void)c; (void)deadline;
    __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(&value), "d"(4) : "memory");
    return -1; /* Failure must consume the nonce as well. */
}
int sfs_client_oneway_deadline(SfsClient *c, u8 t, const u8 *p, u16 n, u32 d)
{ (void)c; (void)t; (void)p; (void)n; (void)d; return 0; }
static void start(unsigned int *sp)
{
    char **argv = (char **)(sp + 1);
    boot = (u8)(argv[1][0] - '0');
    static const SfsIo io = {.now = now, .can_wait = wait};
    enabled = 1; config.io = &io;
    sfs_ensure_session(100);
    sfs_ensure_session(100);
    nonce = 0xffffffffu;
    sfs_ensure_session(100);
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0) : "memory");
    __builtin_unreachable();
}
__asm__(".global _start\n_start:\nmov %esp,%eax\npush %eax\ncall start\n");
/* Referenced by assembly. */
static void (*const keep_start)(unsigned int *) __attribute__((used)) = start;
