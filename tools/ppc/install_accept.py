"""Exercise the three foreground disk entry points from an ISO and a blank IDE disk."""
import argparse
import hashlib
import re
from pathlib import Path
import keep_tmp

from kboot_accept import build_media, mutant_settings
from link_sh_run import ROOT, Session

MGMT = 'kernel/dev/install_management.c'
EXEC = 'exec/exec.c'
CASES = {
    'path': (EXEC, 'kstrcmp(absolute, paths[i]) == 0', 'absolute[0] != 0'),
    'owner': (MGMT, 'r->owner != res_owner_get() || start !=', '0 || start !='),
    'start': (MGMT, '|| start != r->plan.root_ext2.start_lba', '|| 0'),
    'length': (MGMT, 'length != r->plan.root_ext2.sectors ||', '0 ||'),
}


def replace(source, before, after):
    text = (ROOT / source).read_text()
    assert text.count(before) == 1, source
    return text.replace(before, after)


def build(out, case):
    out.mkdir()
    user = out/'user'
    # Observe both real installer mains in their active USER frames at their
    # banner. During hdprep the child also attempts the parent's exact range.
    printer = 'kapi/kapi_print.c'
    anchor = '    console_vprintf((u8)fixed.value[0], format, args);'
    meter = out/'print.c'
    meter.write_text('#include "kstring.h"\n'
                     'volatile unsigned install_banners, install_auth, install_kind, owner_accepted;\n'
                     'int wrap_ide_write_sectors(int, u32, u32, const void *);\n'
                     'int wrap_ext2_format_at(int, u32, u32);\n' + replace(printer, anchor, '''
    if (count && strings[0]) {
        const char *name = strings[0];
        while (*name == ' ') ++name;
        int hdd = !kstrncmp(name, "OS32 HDD Installer", 18);
        int cd = !kstrncmp(name, "OS32 CD Installer", 17);
        if (hdd || cd) {
            install_auth = wrap_ide_write_sectors(0, 0, 0, 0) == 0;
            install_kind = cd ? 2 : 1;
            if (install_banners == 3)
                owner_accepted = wrap_ext2_format_at(0, 16448, 16384) == 0;
            ++install_banners;
        }
    }
''' + anchor))
    cmd = 'userland/shell/cmd_hdprep.c'
    anchor = '    rc = g_api->ext2_format_at(p.drive, p.start, p.len);'
    shell = out/'hdprep.c'
    shell.write_text(replace(cmd, anchor, '''
    g_api->exec_run("/sbin/install.bin");
    int start_rc = g_api->ext2_format_at(p.drive, p.start+1, p.len);
    int len_rc = g_api->ext2_format_at(p.drive, p.start, p.len-1);
    g_api->kprintf(7, "Range checks start=%d length=%d\\n", start_rc == 0, len_rc == 0);
    (void)g_api->kbd_getchar();
''' + anchor))
    settings = ['IMAGE=kboot-link-sh', 'KB_SH_OUT='+str(user),
                'KB_ROOT_BINS='+str(user/'install.bin'),
                'KB_MUT_EXTRA_SRC='+printer, 'KB_MUT_EXTRA_FILE='+str(meter),
                'PPC_RES_MUT_SRC='+cmd, 'PPC_RES_MUT_FILE='+str(shell)]
    if case in CASES:
        settings += mutant_settings(out, case, *CASES[case])
    return build_media(out, '-j8', *settings)


class RejectedMutation(AssertionError):
    pass


def require_case(condition, label):
    if not condition:
        raise RejectedMutation(label)


def runtime(media, out):
    disk = out/'blank.raw'
    with disk.open('wb') as f:
        f.truncate(20 * 1024 * 1024)
    original = disk.read_bytes()
    s = Session(media, '7450', out/'guest', 300)
    s.usb_devices += ('-drive', f'file={disk},format=raw,if=ide,index=0,cache=writethrough')
    try:
        s.start()
        s.wait(s.ready, 'resident CUI READY', 60)
        s.close_rshell()
        for i, (path, kind) in enumerate((('/sbin/install.bin', 1), ('/sbin/cdinst.bin', 2),
                                          ('/bin/install.bin', 1)), 1):
            s.keys(path+'\n')
            s.wait(lambda: s.word('install_banners') == i and s.word('exec_nest_level') == 1,
                   'installer returned '+path, 30)
            assert s.word('install_kind') == kind
            require_case(s.word('install_auth') == int(i < 3), 'path')
            assert disk.read_bytes() == original
        s.keys('hdprep 8\n')
        s.wait(lambda: "Type 'yes'" in s.screen('confirm'), 'hdprep confirmation', 30)
        s.keys('yes\n')
        result = re.compile(r'Range checks start=([01]) length=([01])')
        s.wait(lambda: result.search(s.screen('range')), 'range results', 120)
        start, length = result.search(s.screen('range')).groups()
        assert s.word('install_banners') == 4
        unchanged = disk.read_bytes() == original
        require_case(s.word('owner_accepted') == 0, 'owner')
        require_case(start == '0', 'start')
        require_case(length == '0', 'length')
        assert unchanged
        s.key('ret')
        s.wait(lambda: 'hdprep: verified ext2' in s.screen('verified'), 'mount/write/read/end', 90)
        data = disk.read_bytes()
        start, end = 16448*512, (16448+16384)*512
        assert data[4*512:start] == original[4*512:start] and data[end:] == original[end:]
        assert data[start+1080:start+1082] == b'\x53\xef'
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    hashes = set()
    with keep_tmp.temporary_directory(prefix='ppc-install-') as work:
        for case in ['normal'] + (list(CASES) if args.mutate else []):
            out = Path(work)/case
            media = build(out, case)
            digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
            assert digest not in hashes
            hashes.add(digest)
            print(case, digest, flush=True)
            if not args.build_only:
                try:
                    runtime(media, out)
                except RejectedMutation as error:
                    if str(error) != case:
                        raise
                    print(case, 'runtime RED', flush=True)
                else:
                    assert case == 'normal', f'{case} survived'
    print(f'PPC install {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    main()
