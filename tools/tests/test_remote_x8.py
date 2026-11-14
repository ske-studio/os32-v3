#!/usr/bin/env python3
CHECK = dict(name='remote-x8', tier=1,
    covers=['kernel/console/console.c', 'kernel/console/con_sink.c',
            'include/con_sink.h', 'kernel/port_service.c',
            'platform/pc98/console.c', 'tools/tests/con_sink_host.c',
            'tools/tests/test_con_sink.py', 'tools/tests/test_remote_x8.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'], needs=[], budget=3.5)

import argparse
import pathlib
import tempfile
import test_con_sink as base

CASE = r'''
static void case_capture_x8(void)
{
    u8 out[CON_SINK_REC_MAX];
    for (int flags = 1; flags < 4; flags++) {
        reset_all(); res_owner_set(1); g_v86 = 0;
        rshell_active = flags & 1; boot_serial_mirror = (flags >> 1) & 1;
        cursor_x = cursor_y = 0; copied_len = 0;
        check(con_sink_capture(CON_SINK_CAPTURE_BEGIN) == 0, "x8 begin");
        shell_putchar('A', 7); shell_print("B", 7);
        shell_print_utf8("C", 7); console_write("D", 1, 7);
        check(copied_len == 0, "x8 capture no mirror");
        check(g_text_plane[0] == 'A' && g_text_plane[1] == 'B' &&
              g_text_plane[2] == 'C' && g_text_plane[3] == 'D', "x8 CUI render");
        check(con_sink_capture(CON_SINK_CAPTURE_FREEZE) == 0, "x8 freeze");
        int n = con_sink_read(out, sizeof(out));
        check(n == 16 && out[0] == CON_SINK_REC_PRINT && out[2] == 1 && out[3] == 'A' &&
              out[4] == CON_SINK_REC_PRINT && out[7] == 'B' &&
              out[8] == CON_SINK_REC_PRINT && out[11] == 'C' &&
              out[12] == CON_SINK_REC_PRINT && out[15] == 'D', "x8 sink records");
        check(con_sink_capture(CON_SINK_CAPTURE_END) == 0, "x8 end");
        check(rshell_active == (flags & 1) && boot_serial_mirror == ((flags >> 1) & 1),
              "x8 flags restored");
        shell_putchar('E', 7);
        check(copied_len == 0, "x8 x86 character never mirrored");
        shell_print("F", 7);
        check(copied_len == 1 && copied[0] == 'F', "x8 mirror restored");
    }
    rshell_active = boot_serial_mirror = 0;
}
'''

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__], cwd=ctx.root)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    fixture = base.HOST_SRC.read_text().replace('int main(void)\n{', CASE + '\nint main(void)\n{').replace('    failures = 0;\n    report("con_sink ring', '    failures = 0;\n    case_capture_x8();\n    report("con_sink ring')
    source = base.KERNEL_SRC.read_text()
    with tempfile.TemporaryDirectory(prefix='os32-x8-') as d:
        tmp = pathlib.Path(d)
        fixture_path = tmp / 'host.c'
        fixture_path.write_text(fixture)
        base.HOST_SRC = fixture_path
        result = base.run(tmp, source, None)
        print(result.stdout, end='')
        assert result.returncode == 0, result.stderr
        if args.mutate:
            old = 'rshell_active = boot_serial_mirror = 0;'
            assert source.count(old) == 1
            for replacement in ('boot_serial_mirror = 0;', 'rshell_active = 0;'):
                result = base.run(tmp, source.replace(old, replacement), None)
                assert result.returncode == 1 and 'FAIL x8 capture no mirror' in result.stdout, result.stdout
                print('RED runtime: capture mirrors with ' + replacement)
