/* H1-H4 の既存ケース用。製品と同じ SDK の型・生成定数で shell を供給する。
 * fd=1000 は贋 FS の FD 範囲外。FDRECOV のケースでは無効にして実ファイルを使う。
 * 本体ファイルの I/O カウンタ・障害注入はこの別の入力に消費させない。 */
#include "os32_generations.h"
#define FIXTURE_SHELL_FD 1000
static int fixture_shell_enabled = 1;
static u32 fixture_shell_pos;

static OS32Header fixture_shell_header(void)
{
    OS32Header h;
    memset(&h, 0, sizeof(h));
    h.magic = OS32X_MAGIC;
    h.header_size = OS32X_HDR_SIZE;
    h.version = OS32X_HDR_VERSION;
    h.kapi_data_off = KAPI_DATA_FIELDS_OFF;
    h.kapi_abi_generation = OS32_KAPI_ABI_GENERATION;
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    h.shlib_protocol = OS32_SHLIB_PROTOCOL;
    return h;
}

static int fixture_shell_read(void *buf, u32 size)
{
    OS32Header h = fixture_shell_header();
    u32 n = sizeof(h) - fixture_shell_pos;
    if (n > size) n = size;
    memcpy(buf, (u8 *)&h + fixture_shell_pos, n);
    fixture_shell_pos += n;
    return (int)n;
}
