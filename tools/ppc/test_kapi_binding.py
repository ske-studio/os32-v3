#!/usr/bin/env python3
"""Check PowerMac's typed bindings for the absent providers and their complete link dependencies."""
# CHECK registration lives in tools/checks/ppc-kapi-binding.py.
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "sdk"))
from kapi_target_ppc import ABSENT_PROVIDER_SLOTS, absent_result

# Expected absent slots after the console_font stages (PPC_KAPI_FILL_DESIGN §4 item 10).
EXPECTED_ABSENT_SLOTS = frozenset("""
boot_image_info buz_off buz_on db_bind_blob db_bind_int db_bind_null
db_bind_text db_close db_column_int db_column_text db_error_code db_exec
db_finalize db_last_error db_mem_used db_open db_open_existing db_prepare
db_prepare_only db_step ext2_format ext2_format_at fm_note_off fm_note_on
fm_play_mml fm_set_tone_num fm_startup_sound
gfx2_adapter_count gfx2_adapter_gen gfx2_adapter_info gfx2_caps gfx2_check_op gfx2_connector_edid gfx2_connector_info
gfx2_cursor_image gfx2_cursor_move gfx2_cursor_show gfx2_dpms gfx2_fence_query
gfx2_fence_wait gfx2_flip gfx2_gamma gfx2_head_info gfx2_lock gfx2_mode_count
gfx2_mode_get gfx2_mode_set gfx2_palette gfx2_present gfx2_screen_surface
gfx2_submit gfx2_surface_create gfx2_surface_desc gfx2_surface_free gfx2_unlock
gfx2_vblank_wait gfx_add_dirty_rect gfx_get_framebuffer gfx_get_palette
gfx_hardware_scroll gfx_hw_blit gfx_hw_fill_rect gfx_hw_get gfx_hw_submit
gfx_init gfx_init_200 gfx_lease_palette gfx_osri_show gfx_present
gfx_present_dirty gfx_present_nosync gfx_present_raster gfx_present_rect
gfx_screen_info gfx_screen_owner gfx_set_palette gfx_shutdown gfx_stats
gfx_surface_lease gui_call gui_register hdd_geom_info ide_drive_present
ide_get_info ide_identify ide_init ide_read_sector ide_write_sector
ide_write_sectors ime_dict_info ime_dict_suspend ime_feed_key ime_get_mode
ime_getchar ime_getkey ime_is_active ime_set_mode ime_set_render
ime_switch_dict ime_toggle ime_trygetchar ime_trygetkey ime_user_clear
ime_user_delete ime_user_export ime_user_list kcg_init kcg_load_font
kcg_read_ank kcg_read_kanji kcg_set_scale kdebug_call loop_attach loop_detach
loop_status mem_stat mouse_available mouse_cursor_hide mouse_cursor_set_mode
mouse_cursor_show mouse_poll mouse_set_bounds np2_check_hostdrv np2_detect
np2_get_clock np2_get_cpu np2_get_version
pci_bind_info pci_cfg_read32 pci_count
pci_get pcm_close pcm_open pcm_set_volume pcm_status pcm_write rtc_read
serial_diag serial_get_status serial_getchar serial_init serial_init_vfast
serial_is_initialized serial_putchar serial_puts serial_trygetchar sfs_begin
sfs_end snd_bgm_is_playing snd_bgm_play snd_bgm_set_persist snd_bgm_stop
snd_focus snd_se_play snd_se_play_raw snd_set_master ssg_all_off ssg_tone
ssg_volume surface_lease surface_query surface_unlease sys_get_build_info
sys_set_mtime    sys_switch_shell
sys_time_now sys_ver_info v86_boot v86_boot2 v86_disktest
v86_gdc_capture v86_selftest
""".split())

# Fixed allowlist: exec/*.c (including exec_*.inc), KBOOT_SHELL=1 TUs,
# input_service, kbd_inject, con_sink, pipe_buffer, kapi_strings, kstring.
ALLOWED_UNDEFINED = frozenset("""
boot_token_get
con_sink_capture
appmem_error_public
appmem_map
appmem_unmap
caller_access_get
caller_identity_get
con_sink_read
con_sink_stat
console_get_cursor_x
console_get_cursor_y
console_get_size
console_set_cursor
pm_console_clear
dev_api_get_info
dev_blk_read_lba
dev_blk_write_lba
dev_count
dev_find
exec_abort_clear
exec_app_state
exec_disk_write_allowed
exec_heap_alloc
exec_heap_free
exec_heap_user_alloc
exec_heap_user_free
exec_kill
exec_last_result
exec_park
exec_resume
exec_run
exec_start
exec_stop_culprit
exec_sys_yield
exec_tvram_read_allowed
font_lease_app_open
font_lease_app_read_at
font_lease_app_close
unicode_file_lookup
ppc_kapi_wait_once
kapi_host_open
kapi_host_status
kapi_host_read
kapi_host_write
kapi_host_close
kapi_host_notice_read
kapi_host_notice_serial
kapi_io_policy_set
kapi_io_policy_get
kapi_io_policy_reset
kapi_io_pump
rshell_active
boot_serial_mirror
fd_is_redirected
fd_redirect_get_buf_len
fd_redirect_reset
fd_redirect_to_buffer
fd_redirect_to_file
input_diag
input_diag_log
input_dropped_count
input_get_modifiers
input_getchar_kapi
input_getkey_kapi
input_is_pressed
input_peekkey
input_trygetchar
input_trygetchar_local
input_trygetkey
input_trygetrawkey
kapi_sys_exit
shm_alloc
shm_free
shm_lock
kbd_inject
kbd_inject_pending
kmalloc_free
kmalloc_total
kmalloc_used
launch_cancel
launch_child
launch_pending
launch_poll
launch_report
launch_req
launch_take
memory_boot_ram_kb
paging_enabled
paging_is_present
pipe_alloc
pipe_clear
pipe_free
pipe_get_buf
pipe_get_len
ring3_call_from_user
ring3_fault_kill
ring3_ls_window
ring3_user_range_ok
ring3_user_ranges_writable
shell_print_utf8
shell_putchar
sys_get_mem_kb
sys_reboot
sys_time
tick_count
tvram_putchar_at
tvram_putkanji_at
tvram_readchar_at
tvram_reverse_cell
tvram_scroll
vfs_chdir
vfs_close
path_get_cwd_user
path_get_drive_user
path_set_drive
path_set_cwd
path_parse
vfs_cwd_user
vfs_dev_mount_count
vfs_devname_user
vfs_fstat
vfs_is_mounted
vfs_isatty
vfs_ls
vfs_mkdir
vfs_mount
vfs_open
vfs_read_fd
vfs_rename
vfs_rm
vfs_rmdir
vfs_seek
vfs_stat
vfs_sync
vfs_umount
vfs_umount_checked
vfs_write_fd
""".split())


def verify(source, entries):
    for api in entries:
        name = api['name']
        target = api.get('target', name)
        if re.search(r'\b' + re.escape(target) + r'\s*\(', source):
            return False
        args = ', '.join(api.get('args', [])) or 'void'
        body = ''.join('    (void)' + re.search(r'([a-zA-Z0-9_]+)(?:\[[0-9]*\])?$', arg).group(1) + ';\n'
                       for arg in api.get('args', []))
        if api['ret'] != 'void':
            body += '    return (' + api['ret'] + ')' + absent_result(api) + ';\n'
        expected = api['ret'] + ' __cdecl wrap_' + name + '(' + args + ')\n{\n' + body + '}\n'
        if expected not in source:
            return False
    return bool(re.search(r'void __cdecl wrap_sys_reboot\(void\)\s*\{[^}]*\bsys_reboot\(\);', source))


def generate(tmp, mutation=None):
    sdk = tmp / 'sdk'
    sdk.mkdir()
    for name in ('gen_kapi.py', 'kapi_abi_x86.py', 'target_profile.py', 'kapi_target_ppc.py'):
        shutil.copy(ROOT / 'sdk' / name, sdk / name)
    helper = sdk / 'kapi_target_ppc.py'
    generator = sdk / 'gen_kapi.py'
    if mutation == 'empty':
        text = helper.read_text().replace('ABSENT_PROVIDER_SLOTS = frozenset().union(*(slots for slots, _ in PROVIDER_GROUPS.values()))', 'ABSENT_PROVIDER_SLOTS = frozenset()')
        helper.write_text(text)
    if mutation in ('group-empty', 'ime-empty'):
        group = 'gfx' if mutation == 'group-empty' else 'ime'
        text = helper.read_text().replace(
            'IME_SLOTS = PROVIDER_GROUPS',
            'PROVIDER_GROUPS[' + repr(group) + '] = (frozenset(), frozenset())\nIME_SLOTS = PROVIDER_GROUPS')
        helper.write_text(text)
    if mutation == 'reboot-absent':
        text = helper.read_text().replace("'buz_on', 'buz_off'", "'buz_on', 'buz_off', 'sys_reboot'")
        helper.write_text(text)
    if mutation == 'slot-swap':
        old = "'rtc_read',"
        text = helper.read_text()
        if text.count(old) != 1 or "'tvram_putchar_at'" in text:
            raise RuntimeError('slot-swap: expected rtc_read absent and tvram_putchar_at present')
        helper.write_text(text.replace(old, "'tvram_putchar_at',"))
    if mutation == 'putchar-absent':
        anchor = "    'port': (frozenset({\n"
        text = helper.read_text()
        if text.count(anchor) != 1:
            raise RuntimeError('putchar-absent: expected one port group')
        helper.write_text(text.replace(anchor, anchor + "        'tvram_putchar_at',\n"))
    if mutation == 'font-absent':
        anchor = "    'port': (frozenset({\n"
        text = helper.read_text()
        if text.count(anchor) != 1:
            raise RuntimeError('font-absent: expected one port group')
        group = ("    'console_font': (frozenset({"
                 "'font_source_open', 'font_source_read_at', 'font_source_close', "
                 "'unicode_to_jis'}), frozenset()),\n")
        helper.write_text(text.replace(anchor, group + anchor))
    if mutation == 'zero':
        text = generator.read_text().replace('{absent_result(api)};', '0;')
        generator.write_text(text)
    out = tmp / 'gen'
    subprocess.run([sys.executable, '-B', str(generator), '--target', 'ppc-powermac',
                    '--out-dir', str(out), str(ROOT / 'sdk/kapi.json')], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return out


def main():
    if ABSENT_PROVIDER_SLOTS != EXPECTED_ABSENT_SLOTS:
        print('unexpected absent slots:', sorted(ABSENT_PROVIDER_SLOTS - EXPECTED_ABSENT_SLOTS))
        print('missing absent slots:', sorted(EXPECTED_ABSENT_SLOTS - ABSENT_PROVIDER_SLOTS))
        return 1
    entries = [api for api in json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
               if api['name'] in EXPECTED_ABSENT_SLOTS]
    assert len(entries) == 172
    assert {'buz_on', 'buz_off', 'rtc_read'} <= ABSENT_PROVIDER_SLOTS
    assert not {'sys_reboot', 'sys_shm_alloc', 'sys_shm_lock', 'sys_shm_free'} & ABSENT_PROVIDER_SLOTS
    with tempfile.TemporaryDirectory(prefix='ppc-binding-') as directory:
        tmp = Path(directory)
        (tmp / 'normal').mkdir()
        out = generate(tmp / 'normal')
        source = (out / 'kapi/kapi_generated.c').read_text()
        if not verify(source, entries):
            print('binding verification failed'); return 1
        dirs = ['.', 'include', 'arch/ppc', 'platform/powermac', 'sdk/include',
                'sdk/include/os32', 'kernel', 'init', 'kernel/console', 'kernel/irq',
                'kernel/time', 'mm', 'lib', 'kapi', 'drivers', 'net', 'gfx', 'exec',
                'fs', 'kernel/cli']
        dirs += [str(p.relative_to(ROOT)) for base in ('drivers', 'fs', 'kernel', 'gfx')
                 for p in (ROOT / base).rglob('*') if p.is_dir()]
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if not cc.is_file():
            required = os.environ.get('OS32_REQUIRE_PPC') == '1'
            print(('FAIL' if required else 'SKIP') + ' ppc-kapi-binding: unavailable: ' + str(cc))
            return 1 if required else 0
        subprocess.run([str(cc), '-std=gnu11', '-ffreestanding', '-D__KERNEL_BUILD__',
                        '-D__cdecl=', '-I' + str(out / 'include')]
                       + ['-I' + str(ROOT / d) for d in dirs]
                       + ['-c', str(out / 'kapi/kapi_generated.c'), '-o', str(tmp / 'kapi.o')], check=True)
        nm = cc.with_name('powerpc-elf-nm')
        undefined = {line.split()[-1] for line in subprocess.check_output(
            [str(nm), '-u', str(tmp / 'kapi.o')], text=True).splitlines()}
        if 'sys_reboot' not in undefined:
            print('missing sys_reboot provider'); return 1
        missing = {'exec_tvram_read_allowed', 'pm_console_clear', 'tvram_putchar_at',
                   'tvram_putkanji_at', 'tvram_readchar_at', 'tvram_reverse_cell',
                   'tvram_scroll', 'shm_alloc', 'shm_free', 'shm_lock',
                   'font_lease_app_open', 'font_lease_app_read_at',
                   'font_lease_app_close', 'unicode_file_lookup'} - undefined
        if missing:
            print('missing providers:', sorted(missing)); return 1
        unexpected = undefined - ALLOWED_UNDEFINED
        if unexpected:
            print('unexpected providers:', sorted(unexpected)); return 1
        print('undefined providers:', len(undefined))
        if '--mutate' in sys.argv:
            for mutation in ('empty', 'zero', 'group-empty', 'ime-empty', 'reboot-absent', 'slot-swap'):
                path = tmp / mutation
                path.mkdir()
                mutated = generate(path, mutation)
                if verify((mutated / 'kapi/kapi_generated.c').read_text(), entries):
                    print(mutation + ': SURVIVED'); return 1
                print(mutation + ': RED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
