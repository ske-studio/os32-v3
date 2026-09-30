/* include/kbd.h — KernelAPI用キーボード公開ヘッダ */
/* 完全な定義は drivers/kbd.h にある。 */
/* kapi_generated.c は -Idrivers を持つのでそちらが先に解決されるが、 */
/* 万が一このファイルが先に見つかった場合のフォールバック */
#ifndef __KBD_H
#include "../drivers/kbd.h"
#endif
