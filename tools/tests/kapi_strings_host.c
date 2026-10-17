#define HOST_CALLER_COPY_TEST
#include "setup_host_source.c"
#undef used
#include "kapi_strings.h"
#include "os32_kapi_slots.h"
#include "vfs.h"

static char heap[KAPI_STRING_ARGS][KAPI_STRING_CAP];
static int allocated[KAPI_STRING_ARGS], allocation_count, allocation_fail;
static int effects, nonlocal, expected_accept;
static void *escape[5], *killed[5];
static u32 expected[16], expected_mask, invoked_bytes;
static u32 nul_mask;
static char expected_text[KAPI_STRING_CAP];
void *kmemset(void *p, int v, u32 n)
{
    u8 *b = p;
    while (n--) *b++ = (u8)v;
    return p;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
void *kmalloc(u32 size)
{
    CHECK(size <= KAPI_STRING_CAP);
    allocation_count++;
    if (allocation_fail == allocation_count) return 0;
    for (u32 i = 0; i < KAPI_STRING_ARGS; i++)
        if (!allocated[i]) { allocated[i] = 1; return heap[i]; }
    CHECK(0); return 0;
}
void kfree(void *p)
{
    for (u32 i = 0; i < KAPI_STRING_ARGS; i++)
        if (p == heap[i]) { CHECK(allocated[i]); allocated[i] = 0; return; }
    CHECK(0);
}
u32 kapi_invoke(void *target, const void *words, u32 bytes)
{
    const u32 *args = words;
    CHECK(expected_accept);
    CHECK(target == (void *)0x1234 && bytes == invoked_bytes);
    /* This is the wrapper entry: observing even one invocation on a rejected
     * input counts as a side effect (FS query/validator/output/device/launch). */
    effects++;
    for (u32 i = 0; i < bytes / 4; i++) {
        if (!(expected_mask & (1u << i))) CHECK(args[i] == expected[i]);
        else if (nul_mask & (1u << i)) CHECK(!args[i]);
        else CHECK(equal((const char *)(uptr)args[i], expected_text));
    }
    if (nonlocal) __builtin_longjmp(escape, 1);
    return 0x76543210;
}
#include "strings_host_source.c"
#include "ls_host_source.c"
#include "cleanup_host_source.c"

void ring3_fault_kill(void) { __builtin_longjmp(killed, 1); }
static int dispatch_strings(u32 slot, u32 va, u32 nbytes, u16 ptrmask)
{
    u32 frame[8] = {0}, wrapptr = 0x1234;
    const void *args_src = (const void *)(uptr)va;
    /* Per-call pointer metadata comes from the public API declarations. */
    u16 kapi_argptr[KAPI_FUNC_COUNT] = {0};
    kapi_argptr[slot] = ptrmask;
    if (__builtin_setjmp(killed)) return 0;
#include "dispatch_host_source.c"
    CHECK(0);
syscall_complete:
    CHECK(frame[7] == 0x76543210);
    return 1;
}
struct input_case { u32 slot, bytes, mask, pointers, cap, nullable; };
static const struct input_case cases[] = {
#include "cases_host_source.c"
};
static void invoke(const struct input_case *c, u32 words, int accepted)
{
    u32 root = host_cr3, flags = host_arch_if;
    effects = allocation_count = 0;
    expected_accept = accepted;
    invoked_bytes = c->bytes; expected_mask = c->mask;
    CHECK(dispatch_strings(c->slot, words, c->bytes, c->pointers) == accepted);
    CHECK(effects == accepted);
    CHECK(host_cr3 == root && host_arch_if == flags);
    for (u32 i = 0; i < KAPI_STRING_ARGS; i++) CHECK(!allocated[i]);
}

#define main guest_main
#define memcpy kmemcpy
#define memset kmemset
#define strcmp(a, b) (!equal(a, b))
#include "guest_host_source.c"
#undef strcmp
#undef memset
#undef memcpy
#undef main

static int guest_mapped, guest_unmapped, guest_armed, guest_calls;
static void *guest_fault[5];
static void *guest_map(u32 bytes, void *hint, u32 flags)
{
    CHECK(bytes == PAGE_SIZE * 2 && !hint && !flags && !guest_mapped);
    guest_mapped++;
    return (void *)MEM_EXEC_LOAD_ADDR;
}
static int guest_unmap(void *base, u32 bytes)
{
    CHECK(base == (void *)(MEM_EXEC_LOAD_ADDR + PAGE_SIZE) && bytes == PAGE_SIZE);
    CHECK(guest_mapped == 1 && !guest_unmapped);
    guest_unmapped++;
    return 0;
}
static void guest_print(u8 attr, const char *format, ...)
{
    CHECK(attr == 7 && guest_unmapped == 1 && !guest_armed);
    CHECK(equal(format, "KSTR armed %s; expect FAULT before target\n"));
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    CHECK(equal(__builtin_va_arg(ap, const char *), "boot2"));
    __builtin_va_end(ap);
    guest_armed++;
}
static int guest_boot2(const char *path, const char *second)
{
    u32 va = MEM_EXEC_LOAD_ADDR, tail = va + PAGE_SIZE;
    u8 *data = P2V(payload);
    u32 *words = (u32 *)data;
    const struct input_case c = {KAPI_SLOT_V86_BOOT2, 8, 3, 3, VFS_MAX_PATH, 3};
    CHECK(guest_mapped == 1 && guest_unmapped == 1 && guest_armed == 1);
    CHECK(equal(path, "/__t4b8b_missing__"));
    CHECK(second == (const char *)(uptr)(tail - 16));
    for (u32 i = 0; i < 16; i++) CHECK(second[i]);
    /* Synchronize the guest writes into the distinct B1 physical backing.
     * The real walker sees the second page missing, and the real dispatch
     * must fault before kapi_invoke (the V86 wrapper entry) is reached. */
    u32 *pt = P2V(space.app_pt_phys[0]);
    CHECK(!pt[((tail >> PAGE_SHIFT) % PTE_COUNT)]);
    kmemcpy(data + 512, path, sizeof("/__t4b8b_missing__"));
    kmemcpy(data + PAGE_SIZE - 16, second, 16);
    words[0] = va + 512; words[1] = tail - 16;
    invoke(&c, va, 0);
    guest_calls++;
    __builtin_longjmp(guest_fault, 1);
    return 0;
}
static void guest_boot2_test(void)
{
    KernelAPI api = {.mem_map = guest_map, .mem_unmap = guest_unmap,
                     .kprintf = guest_print, .v86_boot2 = guest_boot2};
    char *argv[] = {"kstr_v86", "boot2"};
    CHECK(guest_main(1, argv, &api) == 1 && !guest_mapped);
    if (!__builtin_setjmp(guest_fault)) {
        guest_main(2, argv, &api);
        CHECK(0); /* A surviving or missing boot2 call is never a pass. */
    }
    CHECK(guest_calls == 1 && !effects);
    SAY("PASS kstr_v86 boot2: bad second input faults before V86, no effects");
}
static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    u32 va = MEM_EXEC_LOAD_ADDR, tail = va + PAGE_SIZE;
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = (va >> PAGE_SHIFT) % PTE_COUNT;
    u8 *data = P2V(payload);
    u32 *words = (u32 *)data;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    u32 map_args[6] = {va, PAGE_SIZE * 2, 3, 0x32, ~0U, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(map_args) : "memory");
    CHECK(mapped == va);
    kmemset((void *)(uptr)va, 0, PAGE_SIZE * 2);
    kmemcpy((void *)(uptr)(tail - 4), "RAW", 4);
    kmemcpy((void *)(uptr)(va + 512), "RAW", 4);
    /* The raw-word mutant must reach an assertion, never NULL-dereference.
     * Its readable VA frame points to different, valid physical bytes. */
    for (u32 i = 0; i < KAPI_STRING_WORDS; i++) ((u32 *)(uptr)va)[i] = va + 1024;
    kmemcpy(data + 1024, "DIFF", 5);
    for (u32 ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
        const struct input_case *c = &cases[ci];
        for (u32 irq = 0; irq < 2; irq++) {
            host_arch_if = irq ? 0x202 : 2;
            nul_mask = 0;
            kmemcpy(expected_text, "abc", 4);
            kmemcpy(data + 512, expected_text, 4);
            kmemcpy(data + PAGE_SIZE - 4, expected_text, 4);
            for (u32 i = 0; i < c->bytes / 4; i++)
                words[i] = expected[i] = c->mask & (1u << i) ? va + 512 :
                    c->pointers & (1u << i) ? va + 1024 : 0x99 + i;
            invoke(c, va, 1);
            /* Every string argument independently ends on the final page byte.
             * Earlier arguments are valid when a later input fails. */
            for (u32 i = 0; i < c->bytes / 4; i++) {
                if (!(c->mask & (1u << i))) continue;
                words[i] = tail - 4;
                invoke(c, va, 1);
                data[PAGE_SIZE - 1] = 'x';
                invoke(c, va, 0);
                data[PAGE_SIZE - 1] = 0;
                words[i] = tail;
                invoke(c, va, 0);
                words[i] = va + 512;
            }
            /* Copy exactly the fixed words, including a page-final last word. */
            kmemcpy(data + PAGE_SIZE - c->bytes, words, c->bytes);
            invoke(c, tail - c->bytes, 1);
            invoke(c, tail - c->bytes + 4, 0);
            /* Allocation failure on each input releases all previous staging. */
            u32 count = 0;
            for (u32 bits = c->mask; bits; bits >>= 1) count += bits & 1;
            for (u32 i = 1; i <= count; i++) {
                allocation_fail = i;
                invoke(c, va, 0);
            }
            allocation_fail = 0;
            space.generation++;
            invoke(c, va, 0); /* saved caller must not silently recapture */
            space.generation--;
            /* Exact bound succeeds; no NUL within bound is never truncated. */
            const struct string_input *spec = &inputs[c->slot];
            CHECK(spec && spec->cap == c->cap && spec->nullable == c->nullable);
            CHECK(!paging_addrspace_map_user(&space, tail, payload + PAGE_SIZE, PAGE_RW | PTE_USER));
            kmemset(expected_text, 'x', spec->cap); expected_text[spec->cap - 1] = 0;
            kmemcpy(data + 512, expected_text, spec->cap);
            invoke(c, va, 1);
            data[512 + spec->cap - 1] = 'x';
            invoke(c, va, 0);
            pt[index + 1] = 0;
            kmemcpy(expected_text, "abc", 4);
            kmemcpy(data + 512, expected_text, 4);
            /* Nonlocal exit abandons the call stack. Owner cleanup releases
             * staging; cleanup for a different app must leave it intact. */
            invoked_bytes = c->bytes; expected_mask = c->mask;
            nonlocal = expected_accept = 1;
            if (!__builtin_setjmp(escape)) { u32 result; kapi_strings_call(c->slot, (void *)0x1234, (void *)(uptr)va, c->bytes, c->pointers, &result); CHECK(0); }
            nonlocal = 0;
            kapi_strings_cleanup_owned(3);
            CHECK(allocated[0]);
            exec_reclaim_resources(2);
            for (u32 i = 0; i < KAPI_STRING_ARGS; i++) CHECK(!allocated[i]);
            kapi_strings_cleanup_owned(2);
            /* Nullable inputs retain NULL (not an invented empty string). */
            for (u32 i = 0; i < c->bytes / 4; i++) {
                if (!(c->mask & (1u << i))) continue;
                words[i] = 0;
                nul_mask = spec->nullable & (1u << i);
                invoke(c, va, !!nul_mask);
                words[i] = expected[i];
            }
            nul_mask = 0;
            /* Ordinary UTF-8 and MML bytes are forwarded verbatim. */
            kmemcpy(expected_text, "UTF-8: \xe3\x81\x82 T120O4C4", sizeof("UTF-8: \xe3\x81\x82 T120O4C4"));
            kmemcpy(data + 512, expected_text, sizeof("UTF-8: \xe3\x81\x82 T120O4C4"));
            invoke(c, va, 1);
        }
    }
    guest_boot2_test();
    char path[VFS_MAX_PATH];
    struct caller_access saved;
    CHECK(caller_access_get(&saved));
    data[PAGE_SIZE - 1] = 0;
    CHECK(ls_copy_path(&saved, tail - 1, path) && !path[0]);
    data[PAGE_SIZE - 1] = 'x';
    CHECK(!ls_copy_path(&saved, tail - 1, path));
    CHECK(ls_copy_path(&saved, 0, path) && !path[0]);
    caller_access_leave(&previous);
    invoke(&cases[0], va, 0); /* no saved USER caller */
    SAY("PASS B1 string slots/edges/limits/arguments/refusal/effects/IF/CR3/owner cleanup");
}
