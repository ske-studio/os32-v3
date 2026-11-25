/* Real keyboard capture/merge; mouse and in2 consumers are outside these fixtures. */
#include "../../sys/hal/input/in2_dev.c"
#include "../../sys/hal/input/in2_state.c"
#ifndef KBD_MERGE_SOURCE
#define KBD_MERGE_SOURCE "../../sys/hal/input/kbd_merge.c"
#endif
#include KBD_MERGE_SOURCE
#ifndef LEGACY_KEYMAP_SOURCE
#define LEGACY_KEYMAP_SOURCE "../../sys/hal/input/legacy_keymap.c"
#endif
#include LEGACY_KEYMAP_SOURCE
#include "../../sys/input/keymaps/pc98_jp.c"
#include "../../sys/platform/pc98/input/kbd_hal.c"
void input_emit_in2(const InputEvent *e) { (void)e; }
u32 in2_time_ms(void) { return 0; }
int pointer_button(InputEvent *e) { (void)e; __builtin_trap(); }
u8 pointer_source_buttons(u8 src) { (void)src; return 0; }
void pointer_absolute(i16 x, i16 y) { (void)x; (void)y; __builtin_trap(); }
void mouse_ext_move(i16 x, i16 y, int m) { (void)x; (void)y; (void)m; __builtin_trap(); }
#include "../../sys/hal/input/keymap_registry.c"
#include "../../sys/hal/input/keymap_eval.c"
#include "../../sys/hal/input/keymap_state.c"

/* Keep legacy merge mutations exercised; S2x tests the real registration. */
int pc98_input_keymap_register(u16 *generation)
{ (void)generation; return OS32_ERR_INVAL; }

int in2_excl_drop_raw(u8 sc, int br) { (void)sc; (void)br; return 0; }
void mouse_ext_wheel(i16 delta) { (void)delta; __builtin_trap(); }
