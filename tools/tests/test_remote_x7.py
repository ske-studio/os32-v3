#!/usr/bin/env python3
CHECK = dict(name='remote-x7', tier=1,
    covers=['kernel/console/con_sink.c', 'include/con_sink.h',
            'tools/tests/con_sink_host.c', 'tools/tests/test_con_sink.py',
            'tools/tests/test_remote_x7.py', 'kernel/console/console.c',
            'kernel/port_service.c', 'platform/pc98/console.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'], needs=[], budget=3.5)

import argparse
import pathlib
import tempfile
import test_con_sink as base

CASE = r'''
static void case_capture_x7(void)
{
    u32 before, after, pending, i;
    u8 out[CON_SINK_REC_MAX];
    reset_all(); res_owner_set(1); rshell_active = 1; boot_serial_mirror = 1;
    check(con_sink_capture(CON_SINK_CAPTURE_BEGIN) == 0, "x7 begin");
    check(!con_sink_is_enabled() && !rshell_active && !boot_serial_mirror, "x7 flags");
    check(con_sink_capture(CON_SINK_CAPTURE_BEGIN) == OS32_ERR_BUSY, "x7 double");
    con_sink_stat(0, &before);
    for (i = 0; i < 2048; i++) con_sink_push_print("x", 1, 7);
    con_sink_stat(&pending, &after);
    check(pending == 8192 && after == before, "x7 boundary");
    con_sink_push_print("y", 1, 7);
    con_sink_stat(&pending, &after);
    check(pending == 8192 && after - before == 1, "x7 drop");
    check(con_sink_capture(CON_SINK_CAPTURE_FREEZE) == OS32_ERR_NOSPC, "x7 overflow failure");
    con_sink_push_print("z", 1, 7);
    con_sink_stat(&pending, &after);
    check(pending == 8192 && after - before == 1, "x7 frozen");
    check(con_sink_read(out, sizeof(out)) > 0, "x7 read");
    res_owner_set(2);
    check(con_sink_capture(CON_SINK_CAPTURE_END) == OS32_ERR_INVAL, "x7 owner");
    res_owner_set(1); con_sink_owner_exit(1);
    check(!con_sink_capture_active() && con_sink_reader_get() == CON_SINK_NO_READER &&
          rshell_active && boot_serial_mirror, "x7 cleanup");
    check(con_sink_capture(CON_SINK_CAPTURE_BEGIN) == 0, "x7 restart");
    con_sink_push_clear(); con_sink_push_cursor(1, 2); con_sink_push_print("ok", 2, 7);
    check(con_sink_capture(CON_SINK_CAPTURE_FREEZE) == 0, "x7 freeze");
    check(con_sink_read(out, sizeof(out)) == 9, "x7 mixed read");
    check(con_sink_capture(CON_SINK_CAPTURE_END) == 0, "x7 end");
}
'''

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__], cwd=ctx.root)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    fixture = base.HOST_SRC.read_text().replace('int main(void)\n{', CASE + '\nint main(void)\n{').replace('    failures = 0;\n    report("con_sink ring', '    failures = 0;\n    case_capture_x7();\n    report("con_sink ring')
    source = base.KERNEL_SRC.read_text()
    with tempfile.TemporaryDirectory(prefix='os32-x7-') as d:
        tmp = pathlib.Path(d)
        fixture_path = tmp / 'host.c'
        fixture_path.write_text(fixture)
        base.HOST_SRC = fixture_path
        result = base.run(tmp, source, None)
        print(result.stdout, end='')
        assert result.returncode == 0, result.stderr
        if args.mutate:
            for old, new, expected in [
                ('if (con_sink_drop_count != g_capture_drop_start) rc = OS32_ERR_NOSPC;',
                 'if (0) rc = OS32_ERR_NOSPC;', 'x7 overflow failure'),
                ('con_sink_drop_count++;', '(void)0;', 'x7 drop')]:
                assert source.count(old) == 1
                result = base.run(tmp, source.replace(old, new), None)
                assert result.returncode == 1 and expected in result.stdout, result.stdout
                print('RED runtime:', expected)
