#include "sys/hal/input/keymap.h"
extern const KmBackend s1p_pc98_backend;
extern const KmExactCell expected[128][8];
extern const u8 expected_flags[128];

static const KmExactCell *select_cell(const KmPc98Extensions *ext, unsigned key,
                                      unsigned column, unsigned caps, unsigned num,
                                      unsigned mode, unsigned altlock)
{
    unsigned bank = !!mode ^ !!altlock;
    if (((ext->flags[key] & 1) && caps) || ((ext->flags[key] & 2) && num))
        column ^= 1;
    const KmExactCell *p = bank && ext->bank1 ? &ext->bank1[key][column] : &ext->bank0[key][column];
    if (p->kind == KM_CELL_INHERIT) p = &ext->bank0[key][column];
    return p;
}
static int check(void)
{
    const KmBackend *map = &s1p_pc98_backend;
    const KmPc98Extensions *ext = map->extensions;
    static KmExactCell bank1[128][8];
    KmPc98Extensions with_bank = *ext;
    if (map->id != 1 || map->count != 128 || ext->bank1 || ext->accents || ext->accent_count) return 1;
    for (unsigned k = 0; k < 128; ++k) {
        if (map->compat_sc[k] != k || ext->flags[k] != expected_flags[k]) return 2;
        for (unsigned c = 0; c < 8; ++c) {
            const KmExactCell *v = &expected[k][c];
            bank1[k][c] = (KmExactCell){0, KM_CELL_INHERIT, 0};
            for (unsigned state = 0; state < 32; ++state) {
                unsigned col = c ^ (((expected_flags[k] & 1) && (state & 1)) ||
                                    ((expected_flags[k] & 2) && (state & 2)) ? 1 : 0);
                const KmExactCell *want = &expected[k][col];
                const KmExactCell *got = select_cell(ext, k, c, state & 1, state & 2, state & 4, state & 8);
                if (got->sym != want->sym || got->kind != want->kind || got->op != want->op) return 3;
            }
            if (c < 2 && (map->entries[k].group1[c] != v->sym || map->entries[k].group2[c] != v->sym)) return 4;
        }
    }
    with_bank.bank1 = bank1;
    bank1[2][2] = (KmExactCell){0x55, KM_CHAR, 0};
    if (select_cell(&with_bank, 2, 2, 0, 0, 1, 0)->sym != 0x55) return 5;
    if (select_cell(&with_bank, 2, 2, 0, 0, 1, 1)->sym != 0x1a) return 6;
    if (select_cell(&with_bank, 2, 6, 0, 0, 1, 0)->sym != 0) return 7;
    return 0;
}
void _start(void)
{
    int rc = check();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (rc));
    __builtin_unreachable();
}
