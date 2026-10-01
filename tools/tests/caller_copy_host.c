#define HOST_CALLER_COPY_TEST
static unsigned int copy_probes;
#include "access_walk_host.c"

static void caller_copy_tests(void)
{
    char out[16], input[8] = "abcdefg";
    u8 *p = P2V(payload);
    u32 va = MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 4;
    struct caller_access trusted = {.origin = CALLER_TRUSTED};
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = (MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT;
    u32 args[6] = {MEM_APP_BAND_BASE - PAGE_SIZE, PAGE_SIZE * 2, 3, 0x32, 0xffffffff, 0};
    u32 mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_APP_BAND_BASE - PAGE_SIZE);
    for (u32 f = 0; f < 2; f++) {
        u32 flags = f ? 0x202 : 2, root = host_cr3;
        host_arch_if = flags;
#define SAME() CHECK(host_arch_if == flags && host_cr3 == root)
#define RESET() do { for (u32 j = 0; j < sizeof(out); j++) out[j] = 0x55; copy_probes = 0; } while (0)
#define UNCHANGED() do { for (u32 j = 0; j < sizeof(out); j++) CHECK(out[j] == 0x55); } while (0)
        kmemcpy(p + PAGE_SIZE - 4, "abc", 4);
        RESET();
        CHECK(copy_caller_cstr(&caller, (void *)va, out, sizeof(out)));
        CHECK(copy_probes == 4 && out[0] == 'a' && out[2] == 'c' && out[3] == 0 && out[4] == 0x55); SAME();
        /* NUL at cap-1 is success; a smaller cap is not truncation success. */
        CHECK(copy_caller_cstr(&caller, (void *)va, out, 4)); SAME();
        CHECK(!copy_caller_cstr(&caller, (void *)va, out, 3)); SAME();
        p[PAGE_SIZE - 1] = 'd';
        CHECK(!copy_caller_cstr(&caller, (void *)va, out, 8)); SAME();
        RESET();
        CHECK(!copy_caller_cstr(&caller, (void *)va, out, 0));
        CHECK(!copy_caller_cstr(&caller, 0, out, 4));
        CHECK(!copy_caller_cstr(&caller, (void *)va, 0, 4));
        CHECK(!copy_probes); UNCHANGED(); SAME();
        /* Reject second-page NP before changing either destination. */
        RESET();
        CHECK(!copy_caller_bytes(&caller, (void *)va, out, 8)); UNCHANGED(); SAME();
        CHECK(!copy_to_caller(&caller, (void *)va, input, 8));
        for (u32 j = 0; j < 4; j++) CHECK(p[PAGE_SIZE - 4 + j] == (u8)input[j]);
        SAME();
        CHECK(!check_caller_write_range(&caller, (void *)va, 8)); SAME();
        RESET();
        CHECK(!copy_to_caller(&caller, (void *)va, input, ~(u32)0));
        CHECK(!copy_caller_bytes(&caller, (void *)va, out, ~(u32)0));
        CHECK(!check_caller_write_range(&caller, (void *)va, ~(u32)0));
        CHECK(!copy_probes); UNCHANGED(); SAME();
        CHECK(copy_to_caller(0, 0, 0, 0));
        CHECK(copy_caller_bytes(0, 0, 0, 0));
        CHECK(check_caller_write_range(0, 0, 0)); SAME();
        CHECK(!copy_to_caller(&caller, 0, input, 1));
        CHECK(!copy_to_caller(&caller, (void *)va, 0, 1));
        CHECK(!copy_caller_bytes(&caller, 0, out, 1));
        CHECK(!copy_caller_bytes(&caller, (void *)va, 0, 1)); SAME();
        /* Noncontiguous PA for the next virtual page. */
        CHECK(!paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                       payload, PAGE_RW | PTE_USER));
        CHECK(copy_to_caller(&caller, (void *)va, input, 8)); SAME();
        CHECK(p[PAGE_SIZE - 4] == 'a' && p[PAGE_SIZE - 1] == 'd' && p[0] == 'e' && p[3] == 0);
        RESET(); CHECK(copy_caller_bytes(&caller, (void *)va, out, 8));
        for (u32 j = 0; j < 8; j++) CHECK(out[j] == input[j]);
        SAME();
        RESET(); CHECK(copy_caller_cstr(&caller, (void *)va, out, 8));
        CHECK(copy_probes == 8 && out[7] == 0); SAME();
        pt[index + 1] &= ~PTE_RW;
        CHECK(!copy_to_caller(&caller, (void *)va, "XXXXXXXX", 8));
        CHECK(p[PAGE_SIZE - 4] == 'a' && p[0] == 'e'); SAME();
        CHECK(copy_caller_bytes(&caller, (void *)va, out, 8)); SAME();
        pt[index + 1] = 0;
        /* Explicit USER remains guarded in WM; stale/root mismatch refuse. */
        ring3_wm_depth = 1; caller.generation++;
        CHECK(!copy_to_caller(&caller, (void *)va, input, 1)); SAME();
        caller.generation--; ring3_wm_depth = 0;
        host_cr3 = paging_kernel_pd_phys();
        CHECK(!copy_caller_cstr(&caller, (void *)va, out, 4));
        CHECK(host_cr3 == paging_kernel_pd_phys() && host_arch_if == flags);
        host_cr3 = root;
        /* TRUSTED obeys the same low band as redirect, including exact end. */
        u8 *low = P2V(MEM_APP_BAND_BASE - 1);
        *low = 0;
        RESET(); CHECK(copy_caller_cstr(&trusted, (void *)low, out, 8));
        CHECK(copy_probes == 1 && out[0] == 0); SAME();
        *low = 'x';
        CHECK(!copy_caller_cstr(&trusted, (void *)low, out, 1)); SAME();
        CHECK(!copy_caller_cstr(&trusted, (void *)low, out, 2)); SAME();
        RESET(); CHECK(!copy_to_caller(&trusted, low, input, 2));
        CHECK(!copy_probes && *low == 'x'); SAME();
        CHECK(copy_to_caller(&trusted, low, input, 1)); CHECK(*low == 'a'); SAME();
        CHECK(!copy_caller_bytes(&trusted, low, out, 2)); SAME();
        CHECK(!copy_caller_cstr(&trusted, (void *)MEM_APP_BAND_BASE, out, 1)); SAME();
        CHECK(!copy_caller_cstr(&trusted, (void *)~(u32)0, out, 2)); SAME();
        CHECK(!copy_caller_cstr(&trusted, 0, out, 2)); SAME();
        CHECK(!copy_caller_cstr(&trusted, (void *)low, out, 0)); SAME();
    }
    SAY("PASS: d4 real walk/copies, NUL/NP/cap/overflow, trusted band, atomic refusal, IF/CR3");
    die(0);
}
