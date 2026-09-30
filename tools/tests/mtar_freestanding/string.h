/* tools/tests/mtar_freestanding/string.h — 穴埋め (stdio.h の説明を参照)。
 * memset / memcpy / strlen は lib/kstring.h が先に宣言している (u32 =
 * unsigned long なので glibc の size_t 版とは型が違う)。ここで重ねて宣言
 * すると衝突するので、kstring.h に無い strcpy だけを出す。 */
#ifndef OS32_TEST_SHIM_STRING_H
#define OS32_TEST_SHIM_STRING_H
char *strcpy(char *dst, const char *src);
#endif
