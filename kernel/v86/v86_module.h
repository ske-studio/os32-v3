/* Private paired V86 body. Transition, PTE and exception returns stay core. */
#ifndef V86_MODULE_H
#define V86_MODULE_H
#include "../module/module_runtime.h"
#include "../module/module.h"
#include "v86_bios.h"
#include "v86_io.h"
#include "v86_pic.h"
#include "v86_kbd.h"
#define V86_MODULE_OPS_VERSION 1u
typedef struct {
    int (*v86_bios_attach_disk)(const char *path);
    int (*v86_bios_attach_second)(const char *path);
    void (*v86_bios_detach_disk)(void);
    int (*v86_bios_has_disk)(void);
    u32 (*v86_bios_boot_daua)(void);
    void (*v86_bios_setup)(void);
    int (*v86_bios_is_hle)(u32 vector);
    void (*v86_bios_dispatch)(u32 *frame, u32 vector);
    u32 (*v86_bios_call_count)(void);
    u32 (*v86_bios_last_vector)(void);
    void (*v86_io_apply_policy)(void);
    void (*v86_io_reset_policy)(void);
    u32 (*v86_io_in)(u16 port, int size);
    void (*v86_io_out)(u16 port, int size, u32 value);
    u32 (*v86_io_trap_count)(void);
    u16 (*v86_io_last_port)(void);
    void (*v86_pic_set_imr)(int slave, u8 imr);
    int (*v86_pic_should_inject)(u32 irq, u32 *out_vector);
    int (*v86_kbd_push)(u8 scancode);
    int (*v86_kbd_pending)(void);
} V86ModuleApi;
#define V86_MODULE_API_SLOTS 20u
int v86_module_load(void);
int v86_module_abort(void);
int v86_module_session_acquire(void);
void v86_module_session_release(void);
const struct kdebug_ops *v86_debug_ops(void);
#endif
