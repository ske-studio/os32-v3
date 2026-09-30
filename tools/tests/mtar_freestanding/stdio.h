/* ========================================================================
 *  tools/tests/mtar_freestanding/stdio.h — ILP32 フリースタンディングの穴埋め
 *
 *  ext2_write_io_host.c が **実物の lib/microtar/microtar.c** を取り込むため
 *  だけの最小のシム。この環境には 32bit の glibc ヘッダが無く、ホスト試験は
 *  -m32 -ffreestanding -nostdlib で組む (u32 が unsigned long なので ILP32 が
 *  必須)。microtar.c / microtar.h は <stdio.h> を無条件に include するが、
 *  MTAR_NO_STDIO を付ければ実際に要るのは sprintf ("%o" / "%06o") だけ。
 *  実体は ext2_write_io_host.c が持つ。
 * ======================================================================== */
#ifndef OS32_TEST_SHIM_STDIO_H
#define OS32_TEST_SHIM_STDIO_H
int sprintf(char *dst, const char *fmt, ...);
#endif
