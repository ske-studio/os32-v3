/* Real suspension implementation; only state, dictionary I/O and rendering are fake. */
#include "ime.h"
#include "kprintf.h"
#include "fep_boundary_runtime.h"
static IME_State state;
static int closes, opens, close_rc, open_rc, warnings;
IME_State *ime_suspend_state(void) { return &state; }
void ime_set_mode(int mode) { state.mode = mode; }
int ime_dict_close(IME_Dict *dict) { (void)dict; closes++; return close_rc; }
int ime_dict_open(IME_Dict *dict, const char *path)
{ (void)dict; if (strcmp(path, IME_DICT_PATH)) exit(2); opens++; return open_rc; }
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{ (void)attr; (void)fmt; warnings++; }
#include "ime_suspend_source.inc"
static void require(int cond, const char *name)
{ __asm__ volatile("" : "+r"(cond)); if (!cond) { printf("FAIL %s\n", name); exit(1); } }
int main(void)
{
    require(!ime_dict_suspend(0) && !opens, "reopen without suspend");
    require(!ime_dict_suspend(1) && !closes, "unloaded no-op");
    state.dict_loaded = 1; state.mode = IME_MODE_KATAKANA;
    require(!ime_dict_suspend(1) && !state.dict_loaded && state.mode == IME_MODE_OFF && closes == 1, "close and OFF");
    require(!ime_dict_suspend(1) && closes == 1, "double close");
    require(!ime_dict_suspend(0) && state.dict_loaded && state.mode == IME_MODE_KATAKANA && opens == 1, "restore mode");
    require(!ime_dict_suspend(0) && opens == 1, "double reopen");
    close_rc = -3;
    require(ime_dict_suspend(1) == -3 && state.dict_loaded, "close failure");
    require(!ime_dict_suspend(0) && opens == 1, "failed close not suspended");
    close_rc = 0; state.mode = IME_MODE_HIRAGANA;
    require(!ime_dict_suspend(1), "second close");
    open_rc = -4;
    require(ime_dict_suspend(0) == -4 && !state.dict_loaded && state.mode == IME_MODE_OFF && warnings == 1, "reopen failure disabled");
    open_rc = 0;
    require(!ime_dict_suspend(0) && state.dict_loaded && state.mode == IME_MODE_HIRAGANA, "reopen retry");
    state.mode = IME_MODE_OFF;
    require(!ime_dict_suspend(1) && !ime_dict_suspend(0) && state.mode == IME_MODE_OFF, "loaded OFF restored");
    puts("GREEN ime suspend");
    return 0;
}
