#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
    int i;
    char *buf;
    time_t t;

    printf("Hello from OS32 libc test! argc=%d\n", argc);
    for (i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }

    /* _sbrkの実装待ちなのでここはコメントアウトまたは ENOMEM になる */
    buf = malloc(256);
    if (buf) {
        strcpy(buf, "malloc success!");
        printf("%s\n", buf);
        free(buf);
    } else {
        printf("malloc failed (expected until sbrk is implemented).\n");
    }

    t = time(NULL);
    printf("Current time: %s", asctime(localtime(&t)));

    return 0;
}
