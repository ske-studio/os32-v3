"""Small ILP32 harness for VER_INFO's real source slices."""
import argparse
import pathlib
import subprocess
import tempfile
import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=['native', 'qemu'], default='qemu')
    parser.add_argument('--mutate', action='store_true')
    return parser.parse_args()


def function(source, signature):
    start = source.index(signature)
    start_body = source.index('{', start)
    depth = 1
    end = start_body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


SUPPORT = r'''
#include "types.h"
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); for (;;) {} }
static void say(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
static int equal(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(1); } } while (0)
'''


def execute(fixture, runner, files=None, dialect='gnu11', assembly=None):
    with tempfile.TemporaryDirectory(prefix='ver-host-') as directory:
        tmp = pathlib.Path(directory)
        (tmp / 'support.h').write_text(SUPPORT)
        for name, content in (files or {}).items():
            (tmp / name).write_text(content)
        (tmp / 'fixture.c').write_text(fixture)
        cmd = ['gcc', '-m32', '-march=i386', '-std='+dialect, '-Os',
               '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
               '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
               '-nostdlib', '-static', '-no-pie', '-I'+str(tmp),
               '-I'+str(ROOT/'include'), '-I'+str(ROOT/'kernel'),
               '-I'+str(ROOT/'sdk/include/os32'), str(tmp/'fixture.c'), '-o', str(tmp/'host')]
        if assembly:
            obj = tmp/'assembly.o'
            subprocess.run(['nasm', '-f', 'elf32', str(ROOT/assembly), '-o', str(obj)], check=True)
            cmd.insert(-2, str(obj))
        host32.build(cmd, check=True, capture_output=True, text=True)
        result = host32.run([str(tmp/'host')], runner=runner, capture_output=True, text=True)
        print(result.stdout, end='')
        return result
