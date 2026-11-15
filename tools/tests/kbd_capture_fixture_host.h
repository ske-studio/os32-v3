/* Real keyboard capture/merge; mouse and in2 consumers are outside these fixtures. */
#include "../../input/hal/in2_dev.c"
#include "../../input/hal/in2_state.c"
#ifndef KBD_MERGE_SOURCE
#define KBD_MERGE_SOURCE "../../input/hal/kbd_merge.c"
#endif
#include KBD_MERGE_SOURCE
#include "../../drivers/input/kbd_hal.c"
void input_emit_in2(const InputEvent *e) { (void)e; }
u32 in2_time_ms(void) { return 0; }
int pointer_button(InputEvent *e) { (void)e; __builtin_trap(); }
u8 pointer_source_buttons(u8 src) { (void)src; return 0; }
void pointer_absolute(i16 x, i16 y) { (void)x; (void)y; __builtin_trap(); }
void mouse_ext_move(i16 x, i16 y, int m) { (void)x; (void)y; (void)m; __builtin_trap(); }
