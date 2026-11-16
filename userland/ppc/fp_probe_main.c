/* Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1, F2, F4, F5.
 * Compile this integer driver with soft-float so startup checks precede FP use. */
#include "os32api.h"

struct image { unsigned words[66]; } __attribute__((aligned(8)));
extern void ppc_fp_load(const struct image *);
extern void ppc_fp_store(struct image *);
extern void ppc_fp_first(struct image *);

static void text(KernelAPI *api, const char *s)
{
    while (*s) api->shell_putchar(*s++, 7);
}

static void pattern(struct image *p, unsigned seed)
{
    for (unsigned i = 0; i < 32; ++i) {
        p->words[2*i] = 0x3ff00000 + seed + i;
        p->words[2*i+1] = seed + i + 1;
    }
    p->words[64] = 0;
    p->words[65] = seed;
}

static int probe(int argc, char **argv, KernelAPI *api)
{
    struct image before, after;
    char mode = argc > 1 ? argv[1][0] : '?';
    if (mode == '1') {
        ppc_fp_first(&after);
        if (after.words[0] != 0x40000000 || after.words[1]) return 41;
        text(api, "F1=2\n");
        return 0;
    }
    if (mode == '4') {
        text(api, "F4-before\n");
        ppc_fp_first(&after);
        text(api, "F4-after\n");
        return 0;
    }
    if (mode == '5' || mode == 'z') {
        ppc_fp_store(&after);
        for (unsigned i = 0; i < 64; ++i)
            if (after.words[i]) return 5;
        if (after.words[65]) return 5;
        if (mode == '5') {
            pattern(&before, 2);
            ppc_fp_load(&before);
            api->exec_run("/bin/fp_probe.bin zero");
            int kind = 0, code = -1;
            api->exec_last_result(&kind, &code);
            return kind == EXEC_KIND_EXITED ? code : 43;
        }
        return 0;
    }
    if (mode == 'c') {
        pattern(&before, 2);
        ppc_fp_load(&before);
        return 0;
    }
    if (mode != '2') return 42;
    pattern(&before, 1);
    ppc_fp_load(&before);
    api->exec_run("/bin/fp_probe.bin child");
    ppc_fp_store(&after);
    for (unsigned i = 0; i < 32; ++i)
        if (before.words[2*i] != after.words[2*i] ||
            before.words[2*i+1] != after.words[2*i+1]) return i ? (int)i : 32;
    if ((after.words[65] & 3) != 1) return 33;
    int kind = 0, code = -1;
    api->exec_last_result(&kind, &code);
    return kind == EXEC_KIND_EXITED && code == 0 ? 0 : 43;
}

int main(int argc, char **argv, KernelAPI *api)
{
    int rc = probe(argc, argv, api);
    text(api, "FP-exit=");
    if (rc >= 10) api->shell_putchar('0' + rc / 10, 7);
    api->shell_putchar('0' + rc % 10, 7);
    text(api, "\n");
    return rc;
}
