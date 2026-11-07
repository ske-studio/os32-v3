"""Real freestanding ILP32 renderer: word-vector and native variadic entries."""
CHECK = dict(name='kprintf-va-host', tier=4,
    covers=['lib/kprintf.c', 'include/kprintf.h', 'include/console.h',
            'include/types.h', 'sdk/include/os32/*.h', 'tools/tests/host32.py'],
    needs=[], budget=2)

import pathlib
import sys
import tempfile
import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "kprintf.h"
static char output[2048], saved[2048];
static unsigned used;
static int failed;
u8 kprintf_attr_to_pc98(u8 a) { return a; }
void console_write(const char *s, u32 n, u8 a) {
    if (a != 7) failed = 1;
    while (n--) { if (used < sizeof(output)-1) output[used++] = *s++; else failed = 1; }
    output[used] = 0;
}
void shell_print(const char *s, u8 a) {
    while (*s) console_write(s++, 1, a);
}
void shell_print_utf8(const char *s, u8 a) { shell_print(s, a); }
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void check(const char *expected) {
    if (!equal(output, expected)) failed = 1;
}
#define BOTH(fmt, expected, ...) do { \
    const u32 words[] = {0, ##__VA_ARGS__}; \
    used = 0; output[0] = 0; \
    kvprintf(7, fmt, words + 1); check(expected); \
    for (unsigned j = 0; j <= used; j++) saved[j] = output[j]; \
    used = 0; output[0] = 0; \
    kprintf(7, fmt, ##__VA_ARGS__); check(expected); check(saved); \
} while (0)
static void tests(void) {
    BOTH("", "");
    BOTH("plain %% end%", "plain % end%");
    BOTH("suffix%0", "suffix");
    BOTH("%d|%i|%u|%x|%X|%c|%q|%d", "-42|-2147483648|4294967295|abcdef|ABCDEF|Z|q|19",
         (u32)-42, 0x80000000u, 0xffffffffu, 0xabcdefu, 0xabcdefu, (u32)'Z', 123u, 19u);
    BOTH("%06d|%6d|%-6d|%-06d|%08x|%4u|%04i|%5c", "-00042|   -42|-42   |-42   |000000ab|   7|0003|A",
         (u32)-42, (u32)-42, (u32)-42, (u32)-42, 0xabu, 7u, 3u, (u32)'A');
    BOTH("%ld|%li|%lu|%lx|%lX", "-1|-2|4294967295|ab|CD",
         (u32)-1, (u32)-2, 0xffffffffu, 0xabu, 0xcdu);
    BOTH("%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d", "1234567890123456",
         1u,2u,3u,4u,5u,6u,7u,8u,9u,0u,1u,2u,3u,4u,5u,6u);
    /* Pointer arguments must retain their pointer type in the native call. */
    const char *s = "hi";
    const u32 words[] = {(u32)s, (u32)s, (u32)s, 0, (u32)s, 7};
    const char *fmt = "%s|%5s|%-5s|%s|%02s|%d";
    const char *expected = "hi|   hi|hi   |(null)|hi|7";
    used = 0; kvprintf(7, fmt, words); check(expected);
    used = 0; kprintf(7, fmt, s, s, s, (const char *)0, s, 7u); check(expected);
}
void _start(void) {
    tests();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failed) : "memory");
    __builtin_unreachable();
}
'''

MUTANTS = [
    ('vector advancement', 'return *args->words++;', 'return *args->words;'),
    ('native advancement', 'return va_arg(*args->ap, u32);', 'return 0;'),
    ('native strings', "if (spec == 's') return (u32)va_arg(*args->ap, const char *);",
     "if (spec == 's') { (void)va_arg(*args->ap, const char *); return 0; }"),
    ('unknown slot', '(void)print_arg(args, *p);', '(void)0;'),
    ('native entry', 'print_format(attr, fmt, &input);\n    va_end(ap);',
     '(void)attr; (void)input;\n    va_end(ap);'),
]


def execute(source):
    with tempfile.TemporaryDirectory(prefix='kprintf-va-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'format.c').write_text(source)
        (tmp / 'host.c').write_text(HARNESS)
        exe = tmp / 'host'
        host32.build(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                      '-Werror', '-ffreestanding', '-fno-pie', '-no-pie',
                      '-fno-stack-protector', '-nostdlib', '-D__cdecl=',
                      '-I' + str(ROOT / 'include'), str(tmp / 'format.c'),
                      str(tmp / 'host.c'), '-o', str(exe)], check=True)
        return host32.run([str(exe)], runner='qemu', timeout=20).returncode


def main(mutate=False):
    source = (ROOT / 'lib/kprintf.c').read_text()
    assert execute(source) == 0
    print('PASS vector/native/expected output (qemu, freestanding ILP32)', flush=True)
    if mutate:
        for name, old, new in MUTANTS:
            assert source.count(old) == 1, name
            rc = execute(source.replace(old, new))
            assert rc == 1, (name, rc)
            print('RED runtime:', name, flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    sys.exit(main('--mutate' in sys.argv))
