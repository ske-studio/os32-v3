/* Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1/F2/F5.
 * qemu-user runs the USER driver with a synthetic, synchronous exec boundary.
 * This does not replace privileged guest acceptance. */
#include "os32api.h"
struct image { unsigned words[66]; } __attribute__((aligned(8)));
extern void ppc_fp_load(const struct image *);
extern void ppc_fp_store(struct image *);
extern void ppc_fp_zero(void);
extern int fp_main(int, char **, KernelAPI *);
static KernelAPI api;
static int last_code, last_kind, calls;
static int omit_init;
static char output[256];
static unsigned used;
static void put(char ch, u8 attr) { (void)attr; if (used < sizeof(output)) output[used++] = ch; }
static int result(int *kind, int *code) { *kind = last_kind; *code = last_code; return 0; }
static int exec(const char *line)
{
    struct image parent;
    char *argv[] = {"fp_probe", line[18] == 'c' ? "child" : "zero"};
    ppc_fp_store(&parent);
    if (!omit_init) ppc_fp_zero();
    ++calls;
    last_code = fp_main(2, argv, &api);
    last_kind = EXEC_KIND_EXITED;
    ppc_fp_load(&parent);
    return 0;
}
int test_main(void)
{
    char *argv[] = {"fp_probe", "1"};
    api.shell_putchar = put; api.exec_run = exec; api.exec_last_result = result;
    ppc_fp_zero();
    if (fp_main(2, argv, &api) || used < 5 || output[3] != '2') return 1;
    argv[1] = "2";
    if (fp_main(2, argv, &api) || calls != 1) return 2;
    ppc_fp_zero(); argv[1] = "5";
    if (fp_main(2, argv, &api) || calls != 2) return 3;
    ppc_fp_zero(); omit_init = 1;
    if (fp_main(2, argv, &api) != 5) return 4;
    return 0;
}
