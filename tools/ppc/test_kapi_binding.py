#!/usr/bin/env python3
"""Check PowerMac's typed bindings for the absent providers and their complete link dependencies."""
CHECK = dict(name='ppc-kapi-binding', tier=1,
             covers=['sdk/kapi_target_ppc.py', 'tools/ppc/test_kapi_binding.py'],
             inputs=['sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py',
                     'sdk/kapi.json'], needs=[], budget=10)
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

# Fixed allowlist: exec/*.c (including exec_*.inc), KBOOT_SHELL=1 TUs,
# input_service, kbd_inject, con_sink, pipe_buffer, kapi_strings, kstring.
ALLOWED_UNDEFINED = frozenset("""
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
sys_time
tick_count
vfs_chdir
vfs_close
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
    return True


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
    if mutation == 'zero':
        text = generator.read_text().replace('{absent_result(api)};', '0;')
        generator.write_text(text)
    out = tmp / 'gen'
    subprocess.run([sys.executable, '-B', str(generator), '--target', 'ppc-powermac',
                    '--out-dir', str(out), str(ROOT / 'sdk/kapi.json')], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return out


def main():
    entries = [x for x in json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
               if x['name'] in ABSENT_PROVIDER_SLOTS]
    assert len(entries) == 200
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
        subprocess.run([str(cc), '-std=gnu11', '-ffreestanding', '-D__KERNEL_BUILD__',
                        '-D__cdecl=', '-I' + str(out / 'include')]
                       + ['-I' + str(ROOT / d) for d in dirs]
                       + ['-c', str(out / 'kapi/kapi_generated.c'), '-o', str(tmp / 'kapi.o')], check=True)
        nm = cc.with_name('powerpc-elf-nm')
        undefined = {line.split()[-1] for line in subprocess.check_output(
            [str(nm), '-u', str(tmp / 'kapi.o')], text=True).splitlines()}
        unexpected = undefined - ALLOWED_UNDEFINED
        if unexpected:
            print('unexpected providers:', sorted(unexpected)); return 1
        print('undefined providers:', len(undefined))
        if '--mutate' in sys.argv:
            for mutation in ('empty', 'zero', 'group-empty', 'ime-empty'):
                path = tmp / mutation
                path.mkdir()
                mutated = generate(path, mutation)
                if verify((mutated / 'kapi/kapi_generated.c').read_text(), entries):
                    print(mutation + ': SURVIVED'); return 1
                print(mutation + ': RED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
