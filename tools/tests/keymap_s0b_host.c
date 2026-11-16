#include "keymap.h"
#include "os32_kapi_shared.h"
extern const KmBackend hid_jp_backend;
static KmEntry entries[128];
static u8 compat[128], flags[128];
static KmExactCell bank0[128][8], bank1[128][8];
static KmPc98Extensions ext = { bank0, bank1, flags, 0, 0 };
static KmBackend pc = { KM_MAP_PC98, 128, entries, compat, 0, &ext };
static int check(void)
{
    KmState s = {0, 0};
    KmEvalResult r, old;
    const KmBackend *h = &hid_jp_backend;
#define E(map, key, value) do { if (km_eval(map, key, &s, &r) || r.sym != (value)) return __LINE__; } while (0)
    E(h, 4, 'a');
    s.mods = KM_MOD_SHIFT; E(h, 4, 'A');
    s.locks = KM_LOCK_CAPS; E(h, 4, 'a');
    E(h, 0x1e, '!');
    s.mods = KM_MOD_CTRL | KM_MOD_SHIFT; s.locks = 0; E(h, 6, 3);
    s.mods = KM_MOD_CTRL; E(h, 0x1f, 0);
    s.mods = KM_MOD_ALT | KM_MOD_COMMAND; E(h, 4, 'a');
    s.mods = KM_MOD_SHIFT; E(h, 0x59, 0xf231);
    if (!is_keypad(r.sym) || (r.sym & 0x7f) != '1' || is_keypad(0xf295)) return __LINE__;
    s.mods = 0; E(h, 0x53, 0xf10b);
    if (!r.no_keydata || r.effect || !r.is_modifier) return __LINE__;
    E(h, 0xe3, 0xf110); if (r.effect != KM_MOD_COMMAND) return __LINE__;
    E(h, 0x90, 0xf113); if (r.action != 1) return __LINE__;
    E(h, 0x8a, 0xf115); if (r.is_modifier) return __LINE__;
    E(h, 0x8b, 0xf116); if (r.is_modifier) return __LINE__;
    compat[0x35] = 0x35; bank0[0x35][0].sym = 0xf115;
    compat[0x51] = 0x51; bank0[0x51][0].sym = 0xf116;
    E(&pc, 0x35, 0xf115); if (r.is_modifier) return __LINE__;
    E(&pc, 0x51, 0xf116); if (r.is_modifier) return __LINE__;
    bank0[2][2].sym = '@';
    bank0[1][2].sym = 0x1a; bank0[1][6].sym = 0;
    bank0[1][0].sym = 0x80; bank0[1][1].sym = 0xff;
    bank1[1][0].kind = KM_CELL_INHERIT;
    s.mods = KM_MOD_CTRL; E(&pc, 1, 0x1a); E(&pc, 2, '@');
    s.mods |= KM_MOD_ALT; E(&pc, 1, 0);
    s.mods = 0; E(&pc, 1, 0x80);
    s.mods = KM_MOD_SHIFT; E(&pc, 1, 0xff);
    flags[1] = 3; s.locks = KM_LOCK_CAPS | KM_LOCK_NUM; E(&pc, 1, 0x80);
    s.mods = KM_MOD_MODE; s.locks = 0; E(&pc, 1, 0x80);
    bank1[1][0].kind = KM_BSD_SPECIAL; bank1[1][0].sym = 0xf500; bank1[1][0].op = 0x82;
    E(&pc, 1, 0xf500); if (r.kind != KM_BSD_SPECIAL || r.effect != KM_LOCK_ALT) return __LINE__;
    if (!r.is_modifier) return __LINE__;
    s.locks = KM_LOCK_ALT; E(&pc, 1, 0x80);
    s.mods = 0; E(&pc, 1, 0xf500);
    flags[1] = 3; s.mods = 0; s.locks = KM_LOCK_CAPS; E(&pc, 1, 0xff);
    s.locks = KM_LOCK_NUM; E(&pc, 1, 0xff);
    s.locks = KM_LOCK_CAPS | KM_LOCK_NUM; E(&pc, 1, 0xff);
    old = r; if (km_eval(&pc, 128, &s, &r) != OS32_ERR_INVAL ||
        r.sym != old.sym || r.effect != old.effect) return __LINE__;
    return 0;
}
void _start(void)
{
    int result = check();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (result));
    __builtin_unreachable();
}
