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

# Expected absent slots after the 11-19 PowerPC stages (port 7・clock・surface 4・console_font 4・gfx2 queries 9; PPC_KAPI_FILL_DESIGN §4 item 10).
EXPECTED_ABSENT_SLOTS = frozenset("""
buz_off buz_on ext2_format ext2_format_at fm_note_off fm_note_on
fm_play_mml fm_set_tone_num fm_startup_sound
gfx2_check_op gfx_get_palette gfx_hardware_scroll gfx_hw_get gfx_hw_submit
gfx_lease_palette gfx_osri_show gfx_set_palette
gui_call gui_register hdd_geom_info ide_drive_present
ide_get_info ide_identify ide_init ide_read_sector ide_write_sector
ide_write_sectors ime_dict_info ime_dict_suspend ime_feed_key ime_get_mode
ime_getchar ime_getkey ime_is_active ime_set_mode ime_set_render
ime_switch_dict ime_toggle ime_trygetchar ime_trygetkey ime_user_clear
ime_user_delete ime_user_export ime_user_list kdebug_call
np2_check_hostdrv np2_detect
np2_get_clock np2_get_cpu np2_get_version
pcm_close pcm_open pcm_set_volume pcm_status pcm_write
sfs_begin
sfs_end snd_bgm_is_playing snd_bgm_play snd_bgm_set_persist snd_bgm_stop
snd_focus snd_se_play snd_se_play_raw snd_set_master ssg_all_off ssg_tone
ssg_volume
sys_switch_shell
v86_boot v86_boot2 v86_disktest
v86_gdc_capture v86_selftest
""".split())

# PPC_KAPI_FILL stage 5c, A11. Each wrapper calls the provider of the same name.
PUBLISHED_GFX = frozenset("""
gfx_get_framebuffer gfx_screen_info gfx_hw_fill_rect gfx_hw_blit gfx_stats
gfx2_mode_set gfx2_flip gfx2_present
""".split())

PRESENT_SLOTS = frozenset("""
gfx_init_200 gfx_present gfx_present_rect gfx_add_dirty_rect
gfx_present_dirty gfx_present_nosync gfx_present_raster
""".split())

GFX2_QUERY_PROVIDERS = frozenset("""
gfx2_adapter_count gfx2_adapter_info gfx2_adapter_gen
gfx2_caps gfx2_head_info gfx2_connector_info
gfx2_connector_edid gfx2_mode_count gfx2_mode_get
""".split())

DISPLAY_PROVIDERS = PUBLISHED_GFX | frozenset("""
gfx_kapi_init_200 gfx_kapi_init gfx_shutdown gfx2_surface_create gfx2_surface_free gfx2_surface_desc
gfx2_screen_surface gfx2_lock gfx2_unlock gfx2_submit gfx2_fence_query gfx2_fence_wait gfx2_cursor_image
gfx_screen_owner gfx2_vblank_wait gfx2_palette gfx2_gamma
gfx2_cursor_move gfx2_cursor_show gfx2_dpms
gfx_present gfx_present_rect gfx_add_dirty_rect gfx_present_dirty gfx_present_nosync gfx_present_raster
""".split())

# Fixed allowlist: exec/*.c (including exec_*.inc), KBOOT_SHELL=1 TUs,
# input_service, kbd_inject, con_sink, pipe_buffer, kapi_strings, kstring.
KCG_PROVIDERS = frozenset("platform_kcg_init platform_kcg_set_scale platform_kcg_load_font kcg_read_ank kcg_read_kanji".split())

LOOP_PROVIDERS = frozenset({"platform_loop_dev_attach", "platform_loop_dev_detach", "platform_loop_dev_status"})

ALLOWED_UNDEFINED = frozenset("""
platform_pci_count platform_pci_get platform_pci_cfg_read32 kapi_pci_bind_info
surface_api_bundle
boot_image_info
kapi_sys_ver_info
surface_api_query
surface_api_lease
surface_api_unlease
port_init port_init_vfast port_putchar_kapi port_getchar_kapi port_trygetchar
port_is_initialized port_get_status port_puts_kapi kapi_serial_diag kapi_sys_set_mtime kapi_sys_get_build_info kapi_sys_time_now
boot_token_get
clock_read_legacy
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
pointer_poll pointer_available pointer_cursor_show pointer_set_bounds pointer_cursor_set_mode pointer_cursor_hide
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
kapi_mem_stat
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


DB_PROVIDERS = {'kapi_' + name for name in '''
db_open db_close db_exec db_prepare db_step
db_column_int db_column_text db_finalize db_last_error db_mem_used
db_open_existing db_prepare_only db_bind_int db_bind_text db_bind_blob
db_bind_null db_error_code
'''.split()}


def insert_group(helper, mutation, group):
    """Put an extra absent group right after the table opening (stable across stages)."""
    opening = "PROVIDER_GROUPS = {\n"
    text = helper.read_text()
    if text.count(opening) != 1:
        raise RuntimeError(mutation + ": expected one PROVIDER_GROUPS opening")
    helper.write_text(text.replace(opening, opening + group))


def verify(source, entries):
    if any('#include "' + header + '"' in source for header in ('gfx_hal.h', 'palette.h')):
        return False
    reference = (ROOT / 'kapi/kapi_generated.c').read_text()
    for slot in PRESENT_SLOTS | PUBLISHED_GFX:
        wrapper = re.search(r'(?:void|int) __cdecl wrap_' + slot + r'\([^)]*\)\n\{.*?\n\}', reference, re.S)
        if not wrapper or wrapper.group() not in source:
            return False
    for api in entries:
        name = api['name']
        target = api.get('target', name)
        if re.search(r'\b' + re.escape(target) + r'\s*\(', source):
            return False
        args = ', '.join(api.get('args', [])) or 'void'
        body = ''.join('    (void)' + re.search(r'([a-zA-Z0-9_]+)(?:\[[0-9]*\])?$', arg).group(1) + ';\n'
                       for arg in api.get('args', []))
        if 'retired' in api:
            if api['ret'] != 'void':
                body += '    return ' + str(api['retired']['result']) + ';\n'
        elif api['ret'] != 'void':
            body += '    return (' + api['ret'] + ')' + absent_result(api) + ';\n'
        expected = api['ret'] + ' __cdecl wrap_' + name + '(' + args + ')\n{\n' + body + '}\n'
        if expected not in source:
            return False
    wrapper = re.search(r'\bwrap_mem_stat\([^)]*\)\n\{(.*?)\n\}', source, re.S)
    if not wrapper or not re.search(
            r'KAPI_HIT\(248\).*?if \(!ring3_user_ranges_writable\(\(u32\)out, KAPI_OUT_LEN\(out, size\),'
            r'\s*\(u32\)0, 0u\)\) \{\s*ring3_fault_kill\(\);.*?\}\s*return kapi_mem_stat\(app_id, out, size\);',
            wrapper.group(1), re.S):
        return False
    if any(not re.search(r'\b' + provider + r'\(', source) for provider in DB_PROVIDERS):
        return False
    calls = ('platform_pci_count()', 'platform_pci_get(idx, out)',
             'platform_pci_cfg_read32(bus, dev, fn, reg)', 'kapi_pci_bind_info(idx, out)')
    if any('return ' + call + ';' not in source for call in calls):
        return False
    return bool(re.search(r'void __cdecl wrap_sys_reboot\(void\)\s*\{[^}]*\bsys_reboot\(\);', source))


def generate(tmp, mutation=None, *, link=True):
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
        # Same count, different set: an absent slot becomes present and a present one absent.
        old = "'buz_on', 'buz_off'"
        text = helper.read_text()
        if text.count(old) != 1 or "'tvram_putchar_at'" in text:
            raise RuntimeError('slot-swap: expected buz_off absent and tvram_putchar_at present')
        helper.write_text(text.replace(old, "'buz_on', 'tvram_putchar_at'"))
    if mutation == 'gfx2-present-absent':
        insert_group(helper, mutation, "    'present_mut': (frozenset({'gfx2_present'}), frozenset()),\n")
    if mutation == 'check-op-bound':
        anchor = "        'gfx2_check_op',\n"
        source = helper.read_text()
        if source.count(anchor) != 1:
            raise RuntimeError('check-op-bound: expected one absent slot')
        helper.write_text(source.replace(anchor, ''))
    if mutation == 'present-absent':
        insert_group(helper, mutation, "    'present_mut': (frozenset({'gfx_present'}), frozenset()),\n")
    if mutation == 'putchar-absent':
        insert_group(helper, mutation, "    'cell_mut': (frozenset({'tvram_putchar_at'}), frozenset()),\n")
    if mutation == 'kcg-absent':
        insert_group(helper, mutation, "    'kcg': (frozenset({'kcg_init', 'kcg_set_scale', "
                     "'kcg_load_font', 'kcg_read_ank', 'kcg_read_kanji'}), frozenset()),\n")
    if mutation == 'font-absent':
        group = ("    'console_font': (frozenset({"
                 "'font_source_open', 'font_source_read_at', 'font_source_close', "
                 "'unicode_to_jis'}), frozenset()),\n")
        insert_group(helper, mutation, group)
    if mutation == "ver-absent":
        insert_group(helper, mutation, "    'ver': (frozenset({'sys_ver_info'}), frozenset()),\n")
    if mutation == 'loop-absent':
        insert_group(helper, mutation, "    'loop': (frozenset({'loop_attach', 'loop_detach', 'loop_status'}), frozenset()),\n")
    if mutation == 'rtc-absent':
        if "'rtc_read'" in helper.read_text():
            raise RuntimeError('rtc-absent: expected rtc_read published')
        insert_group(helper, mutation, "    'clock': (frozenset({'rtc_read'}), frozenset()),\n")
    if mutation == 'mem-absent':
        insert_group(helper, mutation, "    'missing_mem': (frozenset({'mem_stat'}), frozenset()),\n")
    if mutation == 'db-absent':
        insert_group(helper, mutation, "    'db': (frozenset({'db_step'}), frozenset()),\n")
    if mutation == 'pci-absent':
        insert_group(helper, mutation, "    'pci': (frozenset({'pci_get'}), frozenset()),\n")
    if mutation == 'zero':
        text = generator.read_text().replace('{absent_result(api)};', '0;')
        generator.write_text(text)
    if mutation in ('db-null-nosys', 'db-hook-inverted'):
        if mutation == 'db-null-nosys':
            old = 'result = "0" if name == "db_mem_used" else absent_result(api)'
            new = old.replace('else absent_result(api)', 'else ("OS32_ERR_NOSYS" if name == "db_last_error" else absent_result(api))')
        else:
            old = 'defined(OS32_HOOK_KAPI_DB) && !OS32_HOOK_KAPI_DB'
            new = old.replace('&& !', '&& ')
        text = generator.read_text()
        if text.count(old) != 1:
            raise RuntimeError(mutation + ': expected one generator anchor')
        generator.write_text(text.replace(old, new))
    out = tmp / 'gen'
    subprocess.run([sys.executable, '-B', str(generator), '--target', 'ppc-powermac',
                    '--feature-link', str(int(link)), '--out-dir', str(out), str(ROOT / 'sdk/kapi.json')], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return out


def absent_db(out, cc, dirs):
    obj = out / 'absent.o'
    flags = ['-ffreestanding', '-D__KERNEL_BUILD__', '-D__cdecl=', '-I' + str(out / 'include')]
    subprocess.run([cc, *flags, *['-I' + str(ROOT / d) for d in dirs],
                    '-DOS32_HOOK_KAPI_DB=0', '-ffunction-sections', '-c',
                    out / 'kapi/kapi_generated.c', '-o', obj], check=True)
    undefined = subprocess.check_output([cc.with_name('powerpc-elf-nm'), '-u', obj], text=True)
    if re.search(r'\bkapi_db_\w+', undefined):
        return 'provider reference'
    # Run the three real wrapper sections with the Linux/PPC exit syscall.
    probe = out / 'probe.c'
    probe.write_text('''#include "os32_kapi_shared.h"
extern const char *wrap_db_last_error(int);
extern u32 wrap_db_mem_used(void);
extern int wrap_db_open(const char *);
void _start(void)
{
    int rc = wrap_db_last_error(0) != 0 ? 10 :
             wrap_db_mem_used() != 0 ? 11 :
             wrap_db_open("db") != OS32_ERR_NOSYS ? 12 : 0;
    __asm__ volatile("mr 3,%0; li 0,1; sc" : : "r"(rc) : "r3", "r0");
}
''')
    exe = out / 'probe'
    subprocess.run([cc, *flags, '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'sdk/include/os32'),
                    '-msdata=none', '-nostdlib', '-Wl,--gc-sections,-e,_start', probe, obj, '-o', exe], check=True)
    return subprocess.run(['qemu-ppc', exe], timeout=5).returncode


def check_planar_link(tmp, cc, dirs):
    flags = ['-ffreestanding', '-D__KERNEL_BUILD__', '-D__cdecl=']
    flags += ['-I' + str(ROOT / d) for d in dirs]
    provider = ROOT / 'platform/powermac/display_provider.c'
    anchor = 'void gfx_planar_present_dirty(int sync) { (void)sync; }'
    source = provider.read_text()
    if source.count(anchor) != 1:
        raise RuntimeError('planar-missing: expected one empty implementation')
    mutant = tmp / 'display_provider.c'
    mutant.write_text(source.replace(anchor, ''))
    for path in (provider, mutant):
        obj = tmp / 'present.o'
        subprocess.run([cc, *flags, '-nostdlib', '-r', ROOT / 'gfx/gfx_present.c',
                        path, '-o', obj], check=True)
        undefined = subprocess.check_output([cc.with_name('powerpc-elf-nm'), '-u', obj],
                                            text=True).split()
        if ('gfx_planar_present_dirty' in undefined) != (path == mutant):
            raise RuntimeError('planar-missing: unexpected link dependency')
    print('planar-missing: RED')


def main():
    if ABSENT_PROVIDER_SLOTS != EXPECTED_ABSENT_SLOTS:
        print('unexpected absent slots:', sorted(ABSENT_PROVIDER_SLOTS - EXPECTED_ABSENT_SLOTS))
        print('missing absent slots:', sorted(EXPECTED_ABSENT_SLOTS - ABSENT_PROVIDER_SLOTS))
        return 1
    entries = [api for api in json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
               if api['name'] in EXPECTED_ABSENT_SLOTS]
    assert len(entries) == len(EXPECTED_ABSENT_SLOTS)
    assert {'buz_on', 'buz_off'} <= ABSENT_PROVIDER_SLOTS
    assert not {'sys_reboot', 'sys_shm_alloc', 'sys_shm_lock', 'sys_shm_free'} & ABSENT_PROVIDER_SLOTS
    with tempfile.TemporaryDirectory(prefix='ppc-binding-') as directory:
        tmp = Path(directory)
        (tmp / 'normal').mkdir()
        out = generate(tmp / 'normal', link=False)
        (tmp / 'link').mkdir()
        linked = generate(tmp / 'link')
        for header in ('os32_kapi_slots.h', 'os32_kapi_generated.h'):
            path = Path('sdk/include/os32') / header
            if any((profile / path).read_bytes() != (ROOT / path).read_bytes() for profile in (out, linked)):
                print('target ABI mismatch:', header)
                return 1
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
        sfs_slots = {'sfs_begin', 'sfs_end'}
        linked_source = (linked / 'kapi/kapi_generated.c').read_text()
        if not verify(linked_source, [api for api in entries if api['name'] not in sfs_slots]):
            return 1
        subprocess.run([str(cc), '-std=gnu11', '-Werror=implicit-function-declaration', '-ffreestanding', '-D__KERNEL_BUILD__',
                        '-D__cdecl=', '-I' + str(out / 'include')]
                       + ['-I' + str(ROOT / d) for d in dirs]
                       + ['-c', str(linked / 'kapi/kapi_generated.c'), '-o', str(tmp / 'kapi.o')], check=True)
        nm = cc.with_name('powerpc-elf-nm')
        undefined = {line.split()[-1] for line in subprocess.check_output(
            [str(nm), '-u', str(tmp / 'kapi.o')], text=True).splitlines()}
        if 'sys_reboot' not in undefined:
            print('missing sys_reboot provider'); return 1
        missing = {'kapi_mem_stat', 'kapi_sys_ver_info', 'boot_image_info', 'exec_tvram_read_allowed', 'pm_console_clear', 'tvram_putchar_at',
                   'tvram_putkanji_at', 'tvram_readchar_at', 'tvram_reverse_cell',
                   'tvram_scroll', 'shm_alloc', 'shm_free', 'shm_lock',
                   'font_lease_app_open', 'font_lease_app_read_at',
                   'font_lease_app_close', 'unicode_file_lookup',
                   'surface_api_bundle', 'surface_api_query', 'surface_api_lease', 'surface_api_unlease'} - undefined
        if missing:
            print('missing providers:', sorted(missing)); return 1
        missing = GFX2_QUERY_PROVIDERS - undefined
        if missing:
            print('missing gfx2 query providers:', sorted(missing)); return 1
        missing = (DISPLAY_PROVIDERS | KCG_PROVIDERS | DB_PROVIDERS | LOOP_PROVIDERS) - undefined
        if missing:
            print('missing display or KCG providers:', sorted(missing)); return 1
        sfs_providers = {'platform_serialfs_session_begin', 'platform_serialfs_session_end'}
        if not sfs_providers <= undefined:
            print('missing link SFS providers:', sorted(sfs_providers - undefined))
            return 1
        av_providers = {line.split()[1] for line in (ROOT / 'tools/checks/av_wiring.txt').read_text().splitlines()
                        if line and not line.startswith('#') and len(line.split()) == 2}
        if av_providers - undefined:
            print('missing AV providers:', sorted(av_providers - undefined))
            return 1
        unexpected = undefined - (av_providers | sfs_providers | ALLOWED_UNDEFINED | LOOP_PROVIDERS | GFX2_QUERY_PROVIDERS | DISPLAY_PROVIDERS | KCG_PROVIDERS | DB_PROVIDERS)
        if unexpected:
            print('unexpected providers:', sorted(unexpected)); return 1
        print('undefined providers:', len(undefined))
        result = absent_db(out, cc, dirs)
        if result != 0:
            print('absent DB probe failed:', result)
            return 1
        if '--mutate' in sys.argv:
            check_planar_link(tmp, cc, dirs)
            for mutation in ('empty', 'zero', 'group-empty', 'ime-empty', 'reboot-absent', 'slot-swap', 'present-absent', 'gfx2-present-absent', 'check-op-bound', 'pci-absent', 'mem-absent', 'db-absent'):
                path = tmp / mutation
                path.mkdir()
                mutated = generate(path, mutation, link=False)
                if verify((mutated / 'kapi/kapi_generated.c').read_text(), entries):
                    print(mutation + ': SURVIVED'); return 1
                print(mutation + ': RED')
            for mutation, expected in [('db-null-nosys', 10), ('db-hook-inverted', 'provider reference')]:
                path = tmp / mutation
                path.mkdir()
                result = absent_db(generate(path, mutation), cc, dirs)
                if result != expected:
                    print(mutation, 'expected', expected, 'got', result)
                    return 1
                print(mutation + ': RED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
