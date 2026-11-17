#ifndef PPC_SH_STDIO_H
#define PPC_SH_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct sh_file FILE;
extern FILE *stdout;
#define _IOLBF 1
#define BUFSIZ 128

int printf(const char *fmt, ...);
int snprintf(char *out, size_t size, const char *fmt, ...);
int sprintf(char *out, const char *fmt, ...);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
int fflush(FILE *stream);

#endif
