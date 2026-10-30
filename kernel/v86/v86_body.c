#include "v86_module.h"
int v86_module_init(V86ModuleApi *out)
{
    *out = (V86ModuleApi){
        .v86_bios_attach_disk = v86_bios_attach_disk,
        .v86_bios_attach_second = v86_bios_attach_second,
        .v86_bios_detach_disk = v86_bios_detach_disk,
        .v86_bios_has_disk = v86_bios_has_disk,
        .v86_bios_boot_daua = v86_bios_boot_daua,
        .v86_bios_setup = v86_bios_setup,
        .v86_bios_is_hle = v86_bios_is_hle,
        .v86_bios_dispatch = v86_bios_dispatch,
        .v86_bios_call_count = v86_bios_call_count,
        .v86_bios_last_vector = v86_bios_last_vector,
        .v86_io_apply_policy = v86_io_apply_policy,
        .v86_io_reset_policy = v86_io_reset_policy,
        .v86_io_in = v86_io_in,
        .v86_io_out = v86_io_out,
        .v86_io_trap_count = v86_io_trap_count,
        .v86_io_last_port = v86_io_last_port,
        .v86_pic_set_imr = v86_pic_set_imr,
        .v86_pic_should_inject = v86_pic_should_inject,
        .v86_kbd_push = v86_kbd_push,
        .v86_kbd_pending = v86_kbd_pending,
    };
    return 0;
}
const struct {
    u32 version;
    int (*initialize)(V86ModuleApi *);
} v86_module_ops = {V86_MODULE_OPS_VERSION, v86_module_init};
