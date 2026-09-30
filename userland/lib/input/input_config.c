/* ======================================================================== */
/*  INPUT_CONFIG.C — キーコンフィグ ファイル保存・読み込み (P2)                */
/*                                                                          */
/*  バインディング配列をバイナリファイルとして保存・読み込みする。              */
/*                                                                          */
/*  ファイルフォーマット:                                                     */
/*    [0x00] u32 magic   = 'I','N','P','B' (0x42504E49)                     */
/*    [0x04] u16 version = 1                                                 */
/*    [0x06] u16 count   = バインディング数                                   */
/*    [0x08] InputConfigEntry[count]  各12バイト                              */
/*                                                                          */
/*  InputConfigEntry (ファイル上のレイアウト — 構造体非依存):                 */
/*    u8  action_id                                                          */
/*    u8  device                                                             */
/*    u8  modifier_mask                                                      */
/*    u8  _pad                                                               */
/*    u16 code                                                               */
/*    u16 _pad2                                                              */
/*    i32 scale (fix16_t のバイナリ表現)                                     */
/* ======================================================================== */

#include "libos32input.h"
#include <string.h>             /* memcpy, memset */

/* ====================================================================== */
/*  外部参照 (input_core.c で定義)                                          */
/* ====================================================================== */

extern InputBinding g_inp_bindings[];
extern int          g_inp_num_bindings;

/* ====================================================================== */
/*  定数                                                                    */
/* ====================================================================== */

#define INP_CONFIG_MAGIC   0x42504E49UL  /* 'INPB' リトルエンディアン */
#define INP_CONFIG_VERSION 1

/* KernelAPIポインタ (input_core.c で定義) */
extern KernelAPI *g_api;

/* ファイルヘッダ (8バイト) */
typedef struct {
    u32 magic;
    u16 version;
    u16 count;
} InputConfigHeader;

/* ====================================================================== */
/*  API: キーコンフィグをファイルに保存                                      */
/* ====================================================================== */

int input_save_config(const char *path)
{
    InputConfigHeader hdr;
    int fd;
    int ret;

    if (!g_api || !path) {
        return -1;
    }

    fd = g_api->sys_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) {
        return -1;
    }

    /* ヘッダ書き込み */
    hdr.magic   = INP_CONFIG_MAGIC;
    hdr.version = INP_CONFIG_VERSION;
    hdr.count   = (u16)g_inp_num_bindings;

    ret = g_api->sys_write(fd, &hdr, sizeof(hdr));
    if (ret < (int)sizeof(hdr)) {
        g_api->sys_close(fd);
        return -1;
    }

    /* バインディング配列書き込み */
    if (g_inp_num_bindings > 0) {
        ret = g_api->sys_write(fd, g_inp_bindings,
                               sizeof(InputBinding) * g_inp_num_bindings);
        if (ret < (int)(sizeof(InputBinding) * g_inp_num_bindings)) {
            g_api->sys_close(fd);
            return -1;
        }
    }

    g_api->sys_close(fd);
    return 0;
}

/* ====================================================================== */
/*  API: ファイルからキーコンフィグを読み込み                                 */
/* ====================================================================== */

int input_load_config(const char *path)
{
    InputConfigHeader hdr;
    int fd;
    int ret;
    int count;

    if (!g_api || !path) {
        return -1;
    }

    fd = g_api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    /* ヘッダ読み込み */
    ret = g_api->sys_read(fd, &hdr, sizeof(hdr));
    if (ret < (int)sizeof(hdr)) {
        g_api->sys_close(fd);
        return -1;
    }

    /* マジック検証 */
    if (hdr.magic != INP_CONFIG_MAGIC) {
        g_api->sys_close(fd);
        return -1;
    }

    /* バージョン検証 */
    if (hdr.version != INP_CONFIG_VERSION) {
        g_api->sys_close(fd);
        return -1;
    }

    /* バインディング数の範囲チェック */
    count = (int)hdr.count;
    if (count < 0 || count > INPUT_MAX_BINDINGS) {
        g_api->sys_close(fd);
        return -1;
    }

    /* 既存バインディングを全解除 */
    g_inp_num_bindings = 0;

    /* バインディング配列読み込み */
    if (count > 0) {
        ret = g_api->sys_read(fd, g_inp_bindings,
                              sizeof(InputBinding) * count);
        if (ret < (int)(sizeof(InputBinding) * count)) {
            g_api->sys_close(fd);
            g_inp_num_bindings = 0;
            return -1;
        }
    }

    g_inp_num_bindings = count;

    g_api->sys_close(fd);
    return count;
}
