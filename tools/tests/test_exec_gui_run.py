"""Exercise the real exec_run with a fake launcher, without libc."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]

def function(source):
    start = source.index('int exec_run(const char *cmdline)\n{')
    return source[start:source.index('\n}', start) + 2]
PRE = r'''
typedef struct { int gui; } AppSlot;
#define EXEC_KIND_NONE 0
#define EXEC_KIND_INVALID 1
#define EXEC_ERR_INVALID -1
static int g_last_kind, g_last_code, exec_nest_level, launches;
static AppSlot slot;
static int appslot_cur(void) { return 2; }
static AppSlot *appslot_get(int id) { (void)id; return &slot; }
static int exec_launch(const char *s, int gui) { (void)s; (void)gui; launches++; return 7; }
static int exec_map_launch_err(int rc) { return rc == EXEC_ERR_INVALID ? EXEC_KIND_INVALID : 2; }
'''
POST = r'''
void _start(void) {
    int fail = 0;
    exec_nest_level = 1; slot.gui = 1;
    fail |= exec_run("x") != EXEC_ERR_INVALID || launches != 0 || g_last_kind != EXEC_KIND_INVALID;
    slot.gui = 0; fail |= exec_run("x") != 7 || launches != 1;
    slot.gui = 1; exec_nest_level = 0; fail |= exec_run("x") != 7 || launches != 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(fail) : "memory");
    for (;;) {}
}
'''

def run(source):
    with tempfile.TemporaryDirectory(prefix='exec-gui-run-') as d:
        src = Path(d) / 'test.c'; exe = Path(d) / 'test'
        src.write_text(PRE + function(source) + POST)
        subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Werror', '-ffreestanding',
                        '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                        str(src), '-o', str(exe)], check=True)
        return host32.run([str(exe)], timeout=30).returncode

source = (ROOT / 'exec/exec.c').read_text()
assert run(source) == 0
print('GUI exec_run: GREEN')
if '--mutate' in sys.argv:
    old = 'exec_nest_level != 0 && caller && caller->gui'
    assert source.count(old) == 1
    rc = run(source.replace(old, 'exec_nest_level != 0 && caller && !caller->gui'))
    assert rc != 0
    print('gui-guard-inverted: runtime RED rc=' + str(rc))
