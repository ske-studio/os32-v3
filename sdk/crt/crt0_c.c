#include "os32api.h"
#include "os32/help.h"

/* 実名は os32_kapi_v63 (生成ヘッダの `#define kapi OS32_KAPI_CRT_SYMBOL`)。
 * v62 以前にコンパイルした .o は `kapi` を参照したままなので、ここと
 * リンクすると未定義参照で落ちる = 作り直し忘れの検出 (票
 * TASK_KAPI_DATA_FIELDS、ユーザー決裁 2026-09-24)。 */
KernelAPI *kapi;

/* KAPI データ欄の配置の刻印 (ヘッダ v3)。非ロードの .os32_kapi_layout に
 * KAPI_DATA_FIELDS_OFF を置き、mkos32x.py が OS32X ヘッダの kapi_data_off
 * へ写す。crt0 を通るバイナリはすべてここで刻印を持つ。 */
OS32_KAPI_LAYOUT_STAMP();

extern int main(int argc, char **argv, KernelAPI *api);
extern void _init(void);

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

void _start_c(int argc, char **argv, KernelAPI *api) {
    int ret;

    kapi = api;

    _init();

    /* --help 自動ハンドリング: manページがあれば表示して終了 */
    if (argc > 1 && _is_help_flag(argv[1])) {
        if (os32_help_show(argv[0]) == 0) {
            kapi->sys_exit(0);
            while (1) {}
        }
        /* manページが見つからない場合は通常のmain()に処理を委譲 */
    }

    ret = main(argc, argv, kapi);

    kapi->sys_exit(ret);
    while (1) {}
}
