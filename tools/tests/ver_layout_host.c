#include "support.h"
#include "os32_ver_info.h"
#define OFFSET(type, field) __builtin_offsetof(type, field)
void _start(void)
{
    CHECK(sizeof(OS32_VerLine) == 68);
    CHECK(sizeof(OS32_VerInfo) == 1364);
    CHECK(OFFSET(OS32_VerLine, kind) == 0);
    CHECK(OFFSET(OS32_VerLine, text) == 4);
    CHECK(OFFSET(OS32_VerInfo, count) == 0);
    CHECK(OFFSET(OS32_VerInfo, lines) == 4);
    CHECK(OS32_VER_LINES == 20 && OS32_VER_TEXT == 64 && OS32_VER_COLS == 52);
    CHECK(OS32_VER_KIND_MASK == 255);
    CHECK(OS32_VER_HEAD == 0 && OS32_VER_DEVICE == 1);
    CHECK(OS32_VER_IDENTITY == 2 && OS32_VER_DEBUG == 3);
    CHECK(OS32_VER_INDENT == 256);
    CHECK(((OS32_VER_DEVICE | OS32_VER_INDENT) & OS32_VER_KIND_MASK) == OS32_VER_DEVICE);
    say("PASS ver layout (C89): 68/1364, offsets, 20 rows, kind/INDENT\n");
    die(0);
}
