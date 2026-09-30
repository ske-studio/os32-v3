/* ======================================================================== */
/*  PATH.C — パス解決 実装                                                  */
/* ======================================================================== */

#include "path.h"
#include "kstring.h"

/* ======== コールバック ======== */
static PathDeviceValidator device_validator = (PathDeviceValidator)0;

void path_set_device_validator(PathDeviceValidator fn)
{
    device_validator = fn;
}

/* ======== カレントドライブ/パス ======== */
static char cur_drive[PATH_DRIVE_LEN] = "fdd0";
static char cur_cwd[PATH_MAX_LEN]     = "/";

/* ======== ユーティリティ ======== */
/* ローカル再実装だった path_slen/path_scpy は kstring に統一 (SR5)。
 * path_scpy は kstrlcpy (n=バッファ全体サイズ, NUL 終端保証) の
 * ガード付きラッパー。 */
static int path_slen(const char *s)
{
    return (int)kstrlen(s);
}

static void path_scpy(char *dst, const char *src, int max)
{
    /* max<=0 の u32 キャストは巨大長に化けるので先に弾く */
    if (max <= 0) return;
    kstrlcpy(dst, src, (u32)max);
}

/* ======== API ======== */

void path_init(void)
{
    path_scpy(cur_drive, "fdd0", PATH_DRIVE_LEN);
    path_scpy(cur_cwd, "/", PATH_MAX_LEN);
}

const char *path_get_drive(void)
{
    return cur_drive;
}

int path_set_drive(const char *name)
{
    /* デバイスが存在するか確認 (コールバックが登録されている場合) */
    if (device_validator) {
        if (!device_validator(name)) return -1;
    }
    path_scpy(cur_drive, name, PATH_DRIVE_LEN);
    /* ドライブ変更時はルートに戻る */
    path_scpy(cur_cwd, "/", PATH_MAX_LEN);
    return 0;
}

const char *path_get_cwd(void)
{
    return cur_cwd;
}

void path_set_cwd(const char *p)
{
    path_scpy(cur_cwd, p, PATH_MAX_LEN);
}

int path_parse(const char *input, ParsedPath *out)
{
    int i, colon_pos;

    /* コロンを探す */
    colon_pos = -1;
    for (i = 0; input[i]; i++) {
        if (input[i] == ':') {
            colon_pos = i;
            break;
        }
    }

    if (colon_pos > 0 && colon_pos < PATH_DRIVE_LEN) {
        /* "fdd0:path" → ドライブ指定あり */
        for (i = 0; i < colon_pos && i < PATH_DRIVE_LEN - 1; i++)
            out->drive[i] = input[i];
        out->drive[i] = 0;
        path_scpy(out->path, &input[colon_pos + 1], PATH_MAX_LEN);
    } else {
        /* ドライブ指定なし → カレントドライブ */
        path_scpy(out->drive, cur_drive, PATH_DRIVE_LEN);
        path_scpy(out->path, input, PATH_MAX_LEN);
    }

    /* パスが空なら "/" にする */
    if (out->path[0] == 0) {
        out->path[0] = '/';
        out->path[1] = 0;
    }

    return 0;
}

const char *path_basename(const char *path)
{
    const char *last = path;
    while (*path) {
        if (*path == '/' && *(path + 1))
            last = path + 1;
        path++;
    }
    return last;
}
