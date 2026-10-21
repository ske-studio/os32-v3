#include "hwdev_contract.h"
#include "kernel/module_image.h"
static const struct module_symbol_contract hwdev_import_contracts[] = {
    {0x92f2e639U, 1, 1},
    {0xa2272b76U, 1, 1},
    {0x05402719U, 1, 1},
    {0x4fe1503bU, 1, 1},
    {0xf6431588U, 1, 1},
    {0x45e49864U, 1, 1},
    {0x774036e6U, 1, 1},
    {0x2a409dffU, 1, 1},
    {0x1528a892U, 1, 1},
    {0x0f4add98U, 1, 1},
    {0xb3b49ae7U, 1, 1},
    {0x95ae25d6U, 1, 1},
    {0x8de4455cU, 1, 1},
    {0x81baf740U, 1, 1},
    {0x5564c4e6U, 1, 1},
    {0x080a9b53U, 1, 1},
    {0x1298a736U, 1, 1},
    {0x6bb3cb61U, 1, 1},
    {0x0574d963U, 1, 1},
    {0x11911bbbU, 1, 1},
    {0xf71e1c97U, 1, 1},
    {0x7d594b37U, 1, 1},
    {0xe69bd452U, 1, 1},
    {0xf527ef8dU, 1, 1},
    {0x64d729a2U, 1, 1},
    {0x70485fd1U, 1, 1},
    {0x271b54adU, 1, 1},
    {0xca4635c8U, 1, 1},
    {0xe6a3dc58U, 1, 1},
    {0xac5b37e9U, 1, 1},
    {0xafe30e54U, 1, 1},
    {0x2ea1abb2U, 1, 1},
    {0xbaa70544U, 1, 1},
    {0xbba6498fU, 1, 1},
    {0x47aba321U, 1, 1},
};
static const struct module_symbol_contract hwdev_export_contracts[] = {
    {0xce92964bU, 1, 0},
};
static void hwdev_fill_imports(u32 *slots, const struct hwdev_core_api *core) {
    slots[0] = (u32)(uptr)core->trial_begin;
    slots[1] = (u32)(uptr)core->trial_step;
    slots[2] = (u32)(uptr)core->trial_end;
    slots[3] = (u32)(uptr)core->recover_owner;
    slots[4] = (u32)(uptr)core->recover_owner_configure;
    slots[5] = (u32)(uptr)core->recover_safe_point;
    slots[6] = (u32)(uptr)core->recover_plan_enter;
    slots[7] = (u32)(uptr)core->recover_plan_leave;
    slots[8] = (u32)(uptr)core->recover_plan_access;
    slots[9] = (u32)(uptr)core->recover_plan_check;
    slots[10] = (u32)(uptr)core->recover_bind_plan;
    slots[11] = (u32)(uptr)core->recover_budget_apply;
    slots[12] = (u32)(uptr)core->recover_bar_probe;
    slots[13] = (u32)(uptr)core->recover_profile_crc;
    slots[14] = (u32)(uptr)core->recover_access;
    slots[15] = (u32)(uptr)core->recover_restore_point;
    slots[16] = (u32)(uptr)core->recover_target_address;
    slots[17] = (u32)(uptr)core->recover_window_hal_init;
    slots[18] = (u32)(uptr)core->recover_window_access;
    slots[19] = (u32)(uptr)core->recover_window_cleanup;
    slots[20] = (u32)(uptr)core->recover_gf_pre;
    slots[21] = (u32)(uptr)core->recover_gf_result;
    slots[22] = (u32)(uptr)core->recover_gf_screen_safe;
    slots[23] = (u32)(uptr)core->recover_hal_wait_frame;
    slots[24] = (u32)(uptr)core->kpeek_call;
    slots[25] = (u32)(uptr)kprintf;
    slots[26] = (u32)(uptr)kmemset;
    slots[27] = (u32)(uptr)pgalloc_alloc_phys;
    slots[28] = (u32)(uptr)boot_image_info;
    slots[29] = (u32)(uptr)pgalloc_free_n_owner;
    slots[30] = (u32)(uptr)serial_get_setup;
    slots[31] = (u32)(uptr)pci_cfg_read32;
    slots[32] = (u32)(uptr)pci_cfg_read16;
    slots[33] = (u32)(uptr)pci_cfg_write32;
    slots[34] = (u32)(uptr)pci_cfg_write16;
}
