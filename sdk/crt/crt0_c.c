#include "os32api.h"
#include "os32/help.h"

/* 実名は os32_kapi_v89 (生成ヘッダの `#define kapi OS32_KAPI_CRT_SYMBOL`)。
 * v62 以前にコンパイルした .o は `kapi` を参照したままなので、ここと
 * リンクすると未定義参照で落ちる = 作り直し忘れの検出 (票
 * TASK_KAPI_DATA_FIELDS、ユーザー決裁 2026-09-24)。 */
KernelAPI *kapi;

/* KAPI データ欄の配置の刻印 (ヘッダ v3)。非ロードの .os32_kapi_layout に
 * KAPI_DATA_FIELDS_OFF を置き、mkos32x.py が OS32X ヘッダの kapi_data_off
 * へ写す。crt0 を通るバイナリはすべてここで刻印を持つ。 */
OS32_KAPI_LAYOUT_STAMP();

/* entry frame の 4 語目 (T7b-b2)。常駐 shell (gshell) には kernel の内部入口の
 * 表 (exec/shell_service.h の ShellService) の番地、app には 0 が渡る。
 * KAPI ではない (表は KAPI / trampoline / import 表に載らない)。main の署名は
 * 変えない — 使うのは常駐 shell だけで、magic / version を確かめてから読む
 * (userland/gshell/src/ffi.rs)。 */
void *__os32_aux;

extern int main(int argc, char **argv, KernelAPI *api);
extern void _init(void);
extern void __os32_libc_exit(int) __attribute__((weak));

/* --help / -h / /? の簡易チェック (strcmp依存回避) */
static int _is_help_flag(const char *s)
{
    /* "--help" */
    if (s[0]=='-' && s[1]=='-' && s[2]=='h' && s[3]=='e' &&
        s[4]=='l' && s[5]=='p' && s[6]=='\0') return 1;
    /* "-h" */
    if (s[0]=='-' && s[1]=='h' && s[2]=='\0') return 1;
    /* "/?" */
    if (s[0]=='/' && s[1]=='?' && s[2]=='\0') return 1;
    return 0;
}

void _start_c(int argc, char **argv, KernelAPI *api, void *aux) {
    int rc;

    kapi = api;
    __os32_aux = aux;

    _init();

    if (argc > 1 && _is_help_flag(argv[1]) && os32_help_show(argv[0]) == 0)
        rc = 0;
    else
        rc = main(argc, argv, kapi);
    if (__os32_libc_exit)
        __os32_libc_exit(rc);
    kapi->sys_exit(rc);
    for (;;) {}
}
