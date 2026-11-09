/* Host observer; links the actual product walker. No simulated walker body. */
#include "arch/ppc/ppc_pd.h"
#define abort() exit_code(99)
static void exit_code(int code) __attribute__((noreturn));
static void exit_code(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}

static u32 directory, leaf;
static unsigned reads;
static u32 read_word(u32 pa, void *context)
{
    if (context != &reads) abort();
    ++reads;
    /* Nonzero indices and high physical addresses catch shifted/truncated reads. */
    if (pa == 0xfffffabcU) return directory;
    if (pa == 0x12345d14U) return leaf;
    abort();
}
int main(void)
{
    unsigned count = 0;
    /* Columns: PDE P/U/R/cache/PS, PTE P/U/R/cache/PAT, access U/store.
     * Enumerate every column combination; expectations use independent truth
     * tables and literal hardware encodings, not product macros. */
    static const unsigned pp_table[2][2] = {{0, 0}, {1, 2}};
    static const unsigned cache_table[4] = {0x10, 0x40, 0x28, 0x28};
    for (unsigned d = 0; d < 64; ++d)
    for (unsigned t = 0; t < 64; ++t)
    for (unsigned access = 0; access < 4; ++access) {
        directory = 0x12345000U | (d & 31) | ((d & 32) ? 128 : 0);
        leaf = 0xfedcb000U | (t & 31) | ((t & 32) ? 128 : 0);
        unsigned du = (d >> 2) & 1, tu = (t >> 2) & 1;
        unsigned dr = (d >> 1) & 1, tr = (t >> 1) & 1;
        unsigned u = du && tu, rw = dr && tr;
        unsigned expected = (d & 1) && (t & 1) && !(d & 32) && !(t & 32)
            && (!(access & 1) || (u && (!(access & 2) || rw)));
        struct ppc_translation out = {0xdeadbeef, 0xcafebabe};
        reads = 0;
        int got = ppc_pd_lookup_read(0xfffff000U, 0xabf45123U,
                                    access & 2, access & 1, &out,
                                    read_word, &reads);
        if (got != (int)expected ||
            (got && (out.pa != 0xfedcb123U ||
                     out.attr != (cache_table[(t >> 3) & 3] | pp_table[u][rw]))) ||
            (!got && (out.pa != 0xdeadbeef || out.attr != 0xcafebabe)) ||
            reads != ((!(d & 1) || (d & 32)) ? 1U : 2U)) {
            return 1;
        }
        /* A/D do not affect the translation and must never be written. */
        directory |= 0x60;
        leaf |= 0x60;
        struct ppc_translation again = {0xdeadbeef, 0xcafebabe};
        if (ppc_pd_lookup_read(0xfffff000U, 0xabf45123U, access & 2,
                              access & 1, &again, read_word, &reads) != got ||
            again.pa != out.pa || again.attr != out.attr ||
            directory != (0x12345060U | (d & 31) | ((d & 32) ? 128 : 0)) ||
            leaf != (0xfedcb060U | (t & 31) | ((t & 32) ? 128 : 0))) return 2;
        ++count;
    }
    if (ppc_pd_lookup_read(1, 0, 0, 0, 0, read_word, &reads)) return 3;
    if (count != 16384) return 4;
    return 0;
}

void _start(void) { exit_code(main()); }
