#include <stdio.h>
#include "runtime.h"

extern KernelAPI *kapi;
struct sh_file { int fd; };
static FILE stdout_file = { 1 };
FILE *stdout = &stdout_file;

struct output {
    char data[BUFSIZ];
    size_t used;
    int failed;
};

static void flush_output(struct output *out)
{
    size_t sent = 0;
    while (sent < out->used) {
        int n = kapi->sys_write(1, out->data + sent, (u32)(out->used - sent));
        if (n <= 0) {
            out->failed = 1;
            break;
        }
        sent += (size_t)n;
    }
    out->used = 0;
}

static void put_output(void *ctx, size_t index, char ch)
{
    struct output *out = ctx;
    (void)index;
    if (out->failed) return;
    out->data[out->used++] = ch;
    if (out->used == sizeof out->data) flush_output(out);
}

int printf(const char *fmt, ...)
{
    struct output out = { { 0 }, 0, 0 };
    va_list args;
    int count;
    va_start(args, fmt);
    count = sh_vformat(put_output, &out, fmt, args);
    va_end(args);
    flush_output(&out);
    return out.failed ? -1 : count;
}

int snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list args;
    int count;
    va_start(args, fmt);
    count = sh_vsnprintf(out, size, fmt, args);
    va_end(args);
    return count;
}

int sprintf(char *out, const char *fmt, ...)
{
    va_list args;
    int count;
    va_start(args, fmt);
    count = sh_vsnprintf(out, (size_t)-1, fmt, args);
    va_end(args);
    return count;
}

int setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    (void)stream; (void)buffer; (void)mode; (void)size;
    return 0;
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}
