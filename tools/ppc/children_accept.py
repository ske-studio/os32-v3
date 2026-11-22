"""Exercise child discovery between block registration and peerless resident shell startup."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import tempfile

from ata_accept import guest
from kboot_accept import build_media
from run import ROOT, ROM


def replace_once(source, old, new):
    assert source.count(old) == 1, old
    return source.replace(old, new)


def image(out, user, shell, case):
    kernel = 'platform/powermac/ppc_kernel.c'
    code = (ROOT / kernel).read_text()
    # Register a readable ISO child at the last block-registration step.
    # It borrows the read-only media already held by rd0.
    register = '        pm_loop_boot();'
    fixture = '''
        static Device child;
        child = *dev_find("rd0");
        child.name = "rd1";
        if (dev_register(&child)) kboot_stop("child fixture");
'''
    source = out / 'kernel.c'
    code = replace_once(code, register, register + fixture)
    host = '        (void)pm_hostfs_boot(info->tb_hz);'
    if case == 'late-children':
        mount = '        kernel_mount_children(vfs_devname("/"));'
        code = replace_once(code, mount + '\n', '')
        code = replace_once(code, host, host + '\n' + mount)
    probe_host = '        kprintf(7, "CHILD before host child=%d\\r\\n", vfs_is_mounted("/rd1"));'
    source.write_text(replace_once(code, host, probe_host + '\n' + host))
    header = Path('include/kernel_mount_children.h')
    code = (ROOT / header).read_text()
    if case == 'invert-root':
        code = replace_once(code, 'if (match) continue;', 'if (!match) continue;')
    (out / header.name).write_text(code)

    # Observe the VFS duplicate-device rejection, independent of the enumerator.
    vfs = 'fs/vfs.c'
    reject = 'mounts[i].dev_id == dev_id)\n            return VFS_ERR_EXIST;'
    copy = out / 'vfs.c'
    copy.write_text('#include "kprintf.h"\n' + replace_once((ROOT / vfs).read_text(), reject,
        'mounts[i].dev_id == dev_id) {\n'
        '            if (!kstrcmp(prefix, "/rd0")) kprintf(7, "CHILD duplicate rd0\\r\\n");\n'
        '            return VFS_ERR_EXIST;\n        }'))

    # The resident shell issues this open from main. Report the real return value
    # via the kernel serial log since shell console output goes to the framebuffer.
    fd = 'fs/vfs_fd.c'
    entry = 'int vfs_open(const char *path, int mode)\n{'
    probe = '''
    if (path && !kstrcmp(path, "/rd1/sys/shell.bin")) {
        int fd = vfs_open_internal(path, mode, res_owner_get(), 0, 0, VFS_FD_GENERIC);
        kprintf(7, "CHILD root=%s host=%d child=%d readable=%d\\r\\n",
                vfs_devname("/"), vfs_is_mounted("/host"), vfs_is_mounted("/rd1"), fd >= 3);
        kprintf(7, "CHILD shell reached\\r\\n");
        return fd;
    }
'''
    observer = out / 'fd.c'
    observer.write_text('#include "kprintf.h"\n' + replace_once((ROOT / fd).read_text(), entry, entry + probe))
    return build_media(out, '-j6', 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                 'PPC_RES_MUT_SRC=userland/shell/main.c', 'PPC_RES_MUT_FILE=' + str(shell),
                 'KB_MUT_SRC=' + vfs, 'KB_MUT_FILE=' + str(copy),
                 'KB_MUT_EXTRA_SRC=' + kernel, 'KB_MUT_EXTRA_FILE=' + str(source),
                 'KB_MUT_THIRD_SRC=' + fd, 'KB_MUT_THIRD_FILE=' + str(observer))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file() or (not args.build_only and
            not (ROM.is_file() and shutil.which('genisoimage') and shutil.which('qemu-system-ppc'))):
        print('SKIP: missing PowerPC child acceptance tools')
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-children-'))
    shell = work / 'main.c'
    entry = 'int main(int argc, char **argv, KernelAPI *api)\n{'
    shell.write_text(replace_once((ROOT / 'userland/shell/main.c').read_text(), entry,
        entry + '\n    int child_fd = api->sys_open("/rd1/sys/shell.bin", 0);'
                '\n    if (child_fd >= 3) api->sys_close(child_fd);'))
    hashes = set()
    for case in (('normal', 'invert-root', 'late-children') if args.mutate else ('normal',)):
        out = work / case
        out.mkdir()
        invert = case == 'invert-root'
        media = image(out, work / 'user', shell, case)
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert digest not in hashes, case
        hashes.add(digest)
        print(case, digest, media.elf, flush=True)
        if not args.build_only:
            lines = guest(media, '7450', None, absent=True, done='CHILD shell reached')
            assert lines.count('CHILD duplicate rd0') == int(invert)
            child = int(not invert)
            assert f'CHILD root=rd0 host=1 child={child} readable={child}' in lines
            before_host = int(case == 'normal')
            assert [line for line in lines if line.startswith('CHILD before host')] == [
                f'CHILD before host child={before_host}']
            if case == 'late-children':
                print('RED child mounted after host')
            else:
                print('RED duplicate root' if invert else 'GREEN child and peerless shell')
    print(f'PowerMac children {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
