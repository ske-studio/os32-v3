/* Run the distributed OS32X app at its SDK entry with the distributed
 * library. Linux/i386 supplies only page mapping, console and process exit. */
#include "os32api.h"

extern const unsigned char sound_blob[], app_blob[];
static int saw_pass;

static void finish(int rc)
{
    if (!rc && !saw_pass) rc = 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}

static void print(unsigned char attr, const char *message, ...)
{
    const char expected[] = "sndl_test PASS\n";
    unsigned n = 0;
    (void)attr;
    while (message[n] && n < sizeof(expected) && message[n] == expected[n]) n++;
    if (n == sizeof(expected) - 1 && !message[n]) saw_pass = 1;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(message), "d"(n) : "memory");
}

static void *map_image(const unsigned char *blob)
{
    const OS32Header *h = (const OS32Header *)blob;
    unsigned length = h->text_size + h->bss_size;
    unsigned args[6] = {h->load_addr, length, 3, 0x32, ~0u, 0};
    unsigned addr;
    __asm__ volatile("int $0x80" : "=a"(addr) : "0"(90), "b"(args) : "memory");
    if (addr != h->load_addr) finish(3);
    unsigned char *dst = (unsigned char *)addr;
    for (unsigned i = 0; i < h->text_size; i++) dst[i] = blob[h->header_size + i];
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(addr),
                     "c"(h->code_size), "d"(5) : "memory");
    if (rc) finish(4);
    return dst + h->entry_offset;
}

void _start(void)
{
    static KernelAPI api;
    api.version = KAPI_VERSION;
    api.kprintf = print;
    api.sys_exit = finish;
    map_image(sound_blob);
    typedef void (*entry_fn)(int, char **, KernelAPI *, void *);
    entry_fn entry = (entry_fn)map_image(app_blob);
    char name[] = "sndl_test";
    char *argv[] = {name, 0};
    entry(1, argv, &api, 0);
    finish(5);
}
