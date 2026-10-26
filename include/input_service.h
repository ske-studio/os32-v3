/* Cooperative input interface. Register a complete resident table at boot,
 * before IRQ publication; no runtime replacement or ownership transfer. */
#ifndef OS32_INPUT_SERVICE_H
#define OS32_INPUT_SERVICE_H

#include "types.h"
#include "os32_kapi_shared.h"
#include "kbd.h"

typedef struct InputOps {
    int (*getchar)(void);
    int (*getkey)(void);
    int (*trygetchar)(void);
    int (*trygetkey)(void);
    int (*peekkey)(void);
    int (*trygetchar_local)(void);
    int (*getchar_kapi)(void);
    int (*getkey_kapi)(void);
    int (*getkey_wait)(int interruptible);
    int (*has_key)(void);
    int (*is_pressed)(int scancode);
    int (*trygetrawkey)(void);
    u32 (*modifiers)(void);
    u32 (*dropped_count)(void);
    u8 (*shift_state)(void);
    void (*gui_mode)(int on);
    void (*discard_stop)(void);
    int (*diag)(KbdDiag *out);
    int (*diag_log)(u32 after_seq, KbdDiagLogEnt *out, int max);
} InputOps;

void input_register(const InputOps *ops);
int input_getchar(void);
int input_getkey(void);
int input_trygetchar(void);
int input_trygetkey(void);
int input_peekkey(void);
int input_trygetchar_local(void);
int input_getchar_kapi(void);
int input_getkey_kapi(void);
int input_getkey_wait(int interruptible);
int input_has_key(void);
int input_is_pressed(int scancode);
int input_trygetrawkey(void);
u32 input_get_modifiers(void);
u32 input_dropped_count(void);
u8 input_shift_state(void);
void input_set_gui_mode(int on);
void input_discard_stop(void);
int input_diag(KbdDiag *out);
int input_diag_log(u32 after_seq, KbdDiagLogEnt *out, int max);
void input_owner_exit(int aborted);

#endif /* OS32_INPUT_SERVICE_H */
