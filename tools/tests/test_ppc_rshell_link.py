#!/usr/bin/env python3
"""P3 provider forwarding, PLN1 absence and FG1 endpoint.

期待の出所: endpoint は AC-CTX-1 (ARCH_CTX_DESIGN)。provider の委譲と PLN1 の不在は
PowerPC P3 の確定設計 §3「KAPI → platform → link/mailbox」(docs に写すまでは設計の約束として扱う)。
"""
CHECK = dict(name='ppc-rshell-link', tier=1, needs=[], budget=20,
    covers=['platform/powermac/device_host.c', 'platform/powermac/host_unavailable.c',
            'arch/ppc/exec_reclaim.c', 'arch/ppc/exec_kapi.c', 'arch/ppc/exec_kapi.h',
            'tools/tests/test_ppc_rshell_link.py','include/platform_host.h', 'net/link.h', 'net/host_mailbox.h',
            'kapi/kapi_host_notice.c', 'kapi/kapi_host_notice.h', 'arch/ppc/exec_reclaim.h',
            'platform/powermac/kapi_table.h', 'include/redir_access.h',
            'kapi/range_internal.h', 'lib/kstring_c.c', 'lib/kstring.h', 'exec/exec.h',
            'kernel/idle_work.h', 'include/types.h', 'include/callconv.h',
            'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
            'tools/tests/host32.py'], basis=['AC-CTX-1'])
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import host32

ROOT = Path(__file__).resolve().parents[2]


def exercise(tmp, stub=False, mutation=None):
    provider = 'platform/powermac/' + ('host_unavailable.c' if stub else 'device_host.c')
    paths = [provider, 'arch/ppc/exec_reclaim.c', 'arch/ppc/exec_kapi.c']
    text = '\n'.join((ROOT / p).read_text() for p in paths)
    if mutation:
        anchor, replacement = mutation
        assert text.count(anchor) == 1, anchor
        text = text.replace(anchor, replacement)
    if stub:
        text += '\n' + (ROOT / 'kapi/kapi_host_notice.c').read_text()
    # Any attempted automatic interrupt enable is observable at runtime.
    text = '#include "os32_kapi_shared.h"\n#define arch_irq_enable() (++enables)\nstatic int enables;\n' + text
    harness = r'''
#include "os32_kapi_shared.h"
static int calls, cleanup;
void idle_work_run(void) { ++calls; }
i32 link_host_open(const char *p, u32 n, int o) { return p && n == 3 && o == 7 ? 17 : -99; }
i32 link_host_write(i32 h, const void *p, u32 n, int o) { return h == 17 && p && n == 3 && o == 7 ? 3 : -99; }
i32 link_host_read_stage(i32 h, u32 n, const u8 **p, int o) { static const u8 b[] = "abc"; *p = b; return h == 17 && n == 3 && o == 7 ? 3 : -99; }
i32 link_host_status(i32 h, u32 *s, u32 *n, int o) { (void)s; (void)n; return h == 17 && o == 7 ? OS32_ERR_AGAIN : -99; }
i32 link_host_close(i32 h, int o) { return h == 17 && o == 7 ? 0 : -99; }
void link_host_owner_exit(int o) { cleanup = o; }
int main(void) {
 const u8 *out = 0; u32 s = 0, n = 0;
 if (ppc_kapi_wait_once() || calls != 1 || enables) return 1;
 if (ppc_kapi_wait_once() || calls != 2 || enables) return 2;
 int absent = STUB;
 int err = OS32_ERR_NOSYS;
 if (platform_host_open("abc", 3, 7) != (absent ? err : 17)) return 3;
 if (platform_host_write(17, "abc", 3, 7) != (absent ? err : 3)) return 4;
 if (platform_host_read_stage(17, 3, &out, 7) != (absent ? err : 3)) return 5;
 if (!absent && (!out || out[0] != 'a')) return 6;
 if (platform_host_status(17, &s, &n, 7) != (absent ? err : OS32_ERR_AGAIN)) return 7;
 if (platform_host_close(17, 7) != (absent ? err : 0)) return 8;
 exec_host_owner_exit(7);
 if (cleanup != (absent ? 0 : 7) || calls != 2) return 9;
 NOTICE
 return 0;
}
'''
    notice = '''HostNotice notice = {0};
 if (host_mailbox_stage(1, 2, &notice) != OS32_ERR_NOSYS) return 10;
 host_mailbox_commit(1, 2);
 if (host_mailbox_latest_serial()) return 11;
 if (kapi_host_notice_read(1, 2, &notice) != OS32_ERR_NOSYS || notice.serial) return 12;
 if (kapi_host_notice_serial()) return 13;'''
    if stub:
        text += r'''
int caller_access_get(struct caller_access *c) { (void)c; return 0; }
int check_caller_write_range(const struct caller_access *c, void *p, u32 n) { (void)c; (void)p; (void)n; return 1; }
int copy_to_caller(const struct caller_access *c, void *p, const void *s, u32 n) { (void)c; (void)p; (void)s; (void)n; return 0; }
int ring3_user_range_ok(u32 a, u32 n) { (void)a; (void)n; return 1; }
void *kmemcpy(void *p, const void *s, u32 n) { unsigned char *d = p; const unsigned char *b = s; while (n--) *d++ = *b++; return p; }
'''
    text += harness.replace('STUB', str(int(stub))).replace('NOTICE', notice if stub else '')
    source = tmp / 'test.c'
    source.write_text(text)
    binary = tmp / 'test'
    includes = ['.', 'include', 'arch/ppc', 'platform/powermac', 'exec', 'net',
                'kernel', 'sdk/include/os32', 'kapi', 'lib', 'mm', 'kernel/console']
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-D__cdecl=',
                    '-ffunction-sections', '-fdata-sections'] +
                   ['-I' + str(ROOT / p) for p in includes] +
                   [str(source), '-Wl,--gc-sections', '-o', str(binary)], check=True)
    return host32.run([str(binary)]).returncode


def main():
    missing = [tool for tool in ('gcc', 'qemu-i386') if not shutil.which(tool)]
    if missing:
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ' ppc-rshell-link: unavailable: ' + ', '.join(missing))
        return 1 if required else 0
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        assert exercise(tmp) == 0
        assert exercise(tmp, stub=True) == 0
        if '--mutate' in sys.argv:
            for label, mutation in [
                ('host NOSYS', ('return link_host_open(req, len, owner);', 'return OS32_ERR_NOSYS;')),
                ('owner zero', ('platform_host_owner_exit(owner);', 'platform_host_owner_exit(0);')),
                ('endpoint missing', ('    idle_work_run();', '    ;')),
                ('automatic enable', ('    idle_work_run();', '    arch_irq_enable(); idle_work_run();'))]:
                assert exercise(tmp, mutation=mutation) > 0, label
                print('RED ' + label)
    print('PASS PPC provider, owner, PLN1 mailbox and endpoint')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
