"""PowerMac absent providers: slot sets and includes used only by that group.

Common exec, caller validation, owner, FD and heap providers remain bound.
"""

PROVIDER_EXTERNS = frozenset({
    "extern int kapi_pci_bind_info(u32 idx, void *out);",
    "extern volatile u32 tick_count;",
    "extern void kapi_sys_exit(int status);",
    "extern int kapi_serial_diag(SerialDiag *out);",
    "extern int kapi_sys_set_mtime(const char *path, u32 mtime);",
    "extern void kapi_sys_get_build_info(char *buf, int size);",
    "extern i32 kapi_mem_stat(i32 app_id, void *out, u32 size);",
    "extern int kapi_sys_time_now(u32 *lo, u32 *hi);",
    "extern int kapi_sys_ver_info(void *out, u32 size);",
})

PROVIDER_GROUPS = {
    'sys': (frozenset({'buz_on', 'buz_off'}), frozenset([])),
    'surface': (frozenset(), frozenset([])),
    'gfx': (frozenset({
        'gfx_hardware_scroll',
        'gfx_set_palette',
        'gfx_get_palette',
        'gfx_get_framebuffer',
        'gfx_screen_info',
        'gfx_hw_fill_rect',
        'gfx_hw_blit',
        'gfx_stats',
        'gfx_lease_palette',
        'gfx_hw_submit',
        'gfx_hw_get',
        'gfx_osri_show',
    }), frozenset(['gfx_hal.h', 'palette.h'])),
    'gui': (frozenset({
        'gui_call',
        'gui_register',
        'sys_switch_shell',
    }), frozenset([])),
    'sound': (frozenset({
        'fm_startup_sound',
        'fm_play_mml',
        'snd_bgm_play',
        'snd_bgm_stop',
        'snd_bgm_is_playing',
        'snd_se_play',
        'snd_se_play_raw',
        'snd_set_master',
        'snd_bgm_set_persist',
        'fm_note_on',
        'fm_note_off',
        'fm_set_tone_num',
        'ssg_tone',
        'ssg_volume',
        'ssg_all_off',
        'snd_focus',
    }), frozenset(['fm_contract.h', 'snd_engine.h'])),
    'platform': (frozenset({
        'np2_detect',
        'np2_get_version',
        'np2_get_cpu',
        'np2_get_clock',
        'np2_check_hostdrv',
        'loop_attach',
        'loop_detach',
        'loop_status',
    }), frozenset()),
    'ide': (frozenset({
        'ide_init',
        'ide_drive_present',
        'ide_identify',
        'ide_read_sector',
        'ide_get_info',
        'hdd_geom_info',
        'ide_write_sector',
        'ide_write_sectors',
    }), frozenset(['ide_contract.h'])),
    'ext2': (frozenset({
        'ext2_format',
        'ext2_format_at',
    }), frozenset(['ext2.h'])),
    'serialfs': (frozenset({
        'sfs_begin',
        'sfs_end',
    }), frozenset(['serialfs.h'])),
    'system_info': (frozenset({
    }), frozenset([])),
    'ime': (frozenset({
        'ime_getchar',
        'ime_trygetchar',
        'ime_toggle',
        'ime_is_active',
        'ime_set_mode',
        'ime_get_mode',
        'ime_getkey',
        'ime_switch_dict',
        'ime_user_list',
        'ime_user_delete',
        'ime_user_export',
        'ime_user_clear',
        'ime_trygetkey',
        'ime_feed_key',
        'ime_set_render',
        'ime_dict_info',
        'ime_dict_suspend',
    }), frozenset(['ime.h'])),
    'v86': (frozenset({
        'v86_selftest',
        'v86_disktest',
        'v86_boot',
        'v86_boot2',
        'v86_gdc_capture',
    }), frozenset(['v86.h', 'v86_gcap.h'])),
    'pcm': (frozenset({
        'pcm_open',
        'pcm_write',
        'pcm_status',
        'pcm_close',
        'pcm_set_volume',
    }), frozenset(['pcm_contract.h'])),
    'debug': (frozenset({
        'kdebug_call',
    }), frozenset([])),
    # OF uses the shared display entry points for resource-free operations.
    'gfx2': (frozenset({
        'gfx2_check_op',
        'gfx2_mode_set',
        'gfx2_flip',
        'gfx2_present',
    }), frozenset([])),
}

IME_SLOTS = PROVIDER_GROUPS['ime'][0]
ABSENT_PROVIDER_SLOTS = frozenset().union(*(slots for slots, _ in PROVIDER_GROUPS.values()))
ABSENT_PROVIDER_INCLUDES = frozenset().union(*(includes for _, includes in PROVIDER_GROUPS.values()))

# Queries of absent capabilities; IME retains the F10 NOSYS contract.
ZERO_SLOTS = frozenset({
    'np2_detect',
    'np2_check_hostdrv',
    'ide_drive_present',
    'snd_bgm_is_playing',
    'gfx2_adapter_count',
    'unicode_to_jis',
})

def absent_result(api):
    """Pointer acquisition is NULL; capability queries are false/zero."""
    return "0" if "*" in api["ret"] or api["name"] in ZERO_SLOTS else "OS32_ERR_NOSYS"
