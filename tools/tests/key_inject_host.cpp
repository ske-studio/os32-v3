/* aidebug_keys.cpp のホスト試験 (票 docs/archive/tools/TASK_KEY_INJECT.md 受入 K1/K2/K3/K5)。
 * ドライバは test_key_inject.py が np21w-src の実物とリンクして走らせる。 */
#include "compiler.h"	/* 試験用スタブ (SUPPORT_AIDEBUG を立てるだけ) */
#include <stdio.h>
#include <string.h>
#include "aidebug_keys.h"
static int fails = 0;
static void ck_ascii(char c, int wnk, int wsh) {
    int nk=-1, sh=-1;
    int r = aidebug_key_ascii_to_nkey(c, &nk, &sh);
    if (!r || nk != wnk || sh != wsh) {
        printf("FAIL ascii '%c' (0x%02x): r=%d nk=0x%02x sh=%d (want nk=0x%02x sh=%d)\n",
               c, (unsigned char)c, r, nk, sh, wnk, wsh);
        fails++;
    }
}
static void ck_name(const char *n, int want) {
    int g = aidebug_key_name_to_nkey(n);
    if (g != want) { printf("FAIL name \"%s\": got %d want %d\n", n, g, want); fails++; }
}
int main(void) {
    int i;
    /* K1: uppercase now carries SHIFT, same nkey as lowercase */
    ck_ascii('A', 0x1d, 1); ck_ascii('B', 0x2d, 1); ck_ascii('Z', 0x29, 1);
    /* K2: lowercase unchanged, no SHIFT */
    ck_ascii('a', 0x1d, 0); ck_ascii('b', 0x2d, 0); ck_ascii('z', 0x29, 0);
    /* every letter: same key, shift differs only by case */
    for (i = 0; i < 26; i++) {
        int nlo, slo, nup, sup;
        aidebug_key_ascii_to_nkey((char)('a'+i), &nlo, &slo);
        aidebug_key_ascii_to_nkey((char)('A'+i), &nup, &sup);
        if (nlo != nup || slo != 0 || sup != 1) {
            printf("FAIL pair %c/%c: %02x/%d vs %02x/%d\n", 'a'+i,'A'+i,nlo,slo,nup,sup); fails++;
        }
    }
    /* K3: symbols unchanged */
    ck_ascii('!', 0x01, 1); ck_ascii('"', 0x02, 1); ck_ascii('#', 0x03, 1);
    ck_ascii('$', 0x04, 1); ck_ascii('%', 0x05, 1); ck_ascii('&', 0x06, 1);
    ck_ascii('_', 0x33, 1); ck_ascii('+', 0x26, 1); ck_ascii('*', 0x27, 1);
    ck_ascii('-', 0x0b, 0); ck_ascii('/', 0x32, 0); ck_ascii('.', 0x31, 0);
    ck_ascii('\\', 0x0d, 0); ck_ascii('@', 0x1a, 0); ck_ascii(':', 0x27, 0);
    ck_ascii('0', 0x0a, 0); ck_ascii('9', 0x09, 0);
    ck_ascii(' ', 0x34, 0); ck_ascii('\n', 0x1c, 0); ck_ascii('\t', 0x0f, 0);
    /* unmapped stays unmapped */
    { int nk, sh; if (aidebug_key_ascii_to_nkey((char)0x1b, &nk, &sh)) { printf("FAIL ESC byte should be unmapped\n"); fails++; } }
    /* K5 + names unchanged */
    ck_name("SHIFT", 0x70); ck_name("SPACE", 0x34); ck_name("ESC", 0x00);
    ck_name("RETURN", 0x1c); ck_name("F1", 0x62); ck_name("KP7", 0x42);
    ck_name("shift", 0x70); ck_name("A", 0x1d); ck_name("a", 0x1d);
    ck_name("0", 0x0a); ck_name("NOSUCHKEY", -1); ck_name("", -1);
    /* new NKEY: literal */
    ck_name("NKEY:1d", 0x1d); ck_name("nkey:1D", 0x1d); ck_name("NKEY:0", 0x00);
    ck_name("NKEY:7f", 0x7f);
    ck_name("NKEY:80", -1);      /* key-up bit, refused */
    ck_name("NKEY:", -1); ck_name("NKEY", -1); ck_name("NKEY:1d2", -1);
    ck_name("NKEY:gg", -1); ck_name("N", 0x2e); /* still the plain N key */
    printf(fails ? "\n%d FAILURES\n" : "\nall checks passed\n", fails);
    return fails != 0;
}
