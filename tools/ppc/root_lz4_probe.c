/* Stage 1 diagnostic. The only storage used by the client is its low LOAD.
 * OF-PPC Binding 2.1 section 8; memory/available uses the root cell widths. */
#include "ofw.h"
#include "crc32_core.inc"

extern char ppc_kernel_entry[], low_pattern_start[], low_file_end[], low_end[], high_start[], high_end[];
static int output;

static void line(const char *label, unsigned a, unsigned b)
{
    char buf[96];
    unsigned n = 0;
    while (*label) buf[n++] = *label++;
    unsigned values[] = {a, b};
    for (unsigned i = 0; i < 2; ++i) {
        buf[n++] = ' ';
        for (int shift = 28; shift >= 0; shift -= 4)
            buf[n++] = "0123456789abcdef"[(values[i] >> shift) & 15];
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    ofw_write(output, buf, n);
}

static int inspect(const struct ofw_env *env, unsigned start, unsigned pattern,
                   unsigned file, unsigned end)
{
    for (unsigned page = start; page < end; page += 4096) {
        cell result[4] = {page};
        /* translate (virt -- phys.lo [phys.hi] mode true | false) */
        if (ofw_call_method("translate", env->mmu, 1, env->ac + 2, result) ||
            !result[env->ac + 1] || result[0] != page ||
            (env->ac == 2 && result[1])) {
            line("BAD-TRANSLATE", page, result[0]);
            return -1;
        }
    }
    line("TRANSLATED", start, end);
    for (unsigned pa = pattern; pa < file; ++pa) {
        unsigned shift = 8 * (3 - (pa & 3));
        unsigned char expected = (pa & ~3U) >> shift;
        if (*(const volatile unsigned char *)pa != expected) {
            line("BAD-PATTERN", pa, expected);
            return -1;
        }
    }
    line("CRC", start, crc32_core_final(crc32_core_update(CRC32_INIT, (const void *)start, file-start)));
    return 0;
}

static int regions(int mem, const char *key, const char *label, unsigned ac, unsigned sc)
{
    cell data[256];
    int length = ofw_getprop(mem, key, data, sizeof(data));
    unsigned stride = ac + sc;
    if (length <= 0 || length >= (int)sizeof(data) || length % (4*stride)) return -1;
    for (unsigned i = 0; i < (unsigned)length / 4; i += stride) {
        if ((ac == 2 && data[i]) || (sc == 2 && data[i+ac])) return -1;
        line(label, data[i+ac-1], data[i+stride-1]);
    }
    return 0;
}

void ppc_kernel_start(ppc_ci ci)
{
    struct ofw_env env;
    cell forth[] = {ppc_ptr_cell("reset-all"), 0};
    ofw_init(ci);
    if (ofw_setup(&env)) goto reset;
    output = env.out;
    /* Record actual execution, including any unexpected relocation. */
    extern char layout_pc[];
    unsigned pc;
    __asm__ volatile ("bl layout_pc\nlayout_pc: mflr %0" : "=r"(pc) : : "lr");
    line("ENTRY", pc - (unsigned)layout_pc + (unsigned)ppc_kernel_entry, (unsigned)ci);
    line("MMU", env.mmu, env.ac);
    cell bases[] = {ppc_ptr_cell("load-base"), 0, 0};
    if (ofw_call("interpret", 1, 2, bases) || bases[1]) goto reset;
    line("LOAD-BASE", bases[2], 0);
    bases[0] = ppc_ptr_cell("real-base");
    if (ofw_call("interpret", 1, 2, bases) || bases[1]) goto reset;
    line("REAL-BASE", bases[2], 0);
    int low = inspect(&env, (unsigned)ppc_kernel_entry, (unsigned)low_pattern_start,
                      (unsigned)low_file_end, (unsigned)low_end);
    int high = inspect(&env, (unsigned)high_start, (unsigned)high_start,
                       (unsigned)high_end, (unsigned)high_end);
    int mem = ofw_finddevice("/memory");
    int ram = regions(mem, "reg", "RAM", env.ac, env.sc);
    int available = regions(mem, "available", "AVAILABLE", env.ac, env.sc);
    line("LAYOUT-END", !(low || high || ram || available), 0);
reset:
    ofw_call("interpret", 1, 1, forth);
    for (;;) {}
}
