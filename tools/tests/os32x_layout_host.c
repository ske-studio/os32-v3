/* =========================================================================
 *  OS32X_LAYOUT_HOST.C — OS32X ヘッダ v3 の配置照合 (exec / shlib ローダ /
 *                        常駐シェルの起動) を実物のソースで確かめる
 *
 *  対象票: docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md (方針 v2 の 4 / 受入)
 *  実行:   python3 -B tools/tests/test_kapi_layout.py [--mutate]
 *
 *  exec_launch (アプリも常駐シェルも同じ経路) と kernel/shlib.c の shlib_init
 *  は、どちらも exec/os32x_hdr.c の os32x_layout_check() 1 本で判定する。
 *  ここはそれを **1 行も写さず #include** して、
 *    v2 → 断る / v3 で値違い → 断る / 一致 → 通す
 *  と、実読込長・header_size の境界を見る。
 *
 *  [C1] C89 / GNU89。
 * ========================================================================= */

#include <stdio.h>
#include <string.h>

#include "../../exec/os32x_hdr.c"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

static void make_hdr(OS32Header *h, u32 version, u32 hsize, u32 off)
{
    memset(h, 0, sizeof(*h));
    h->magic = OS32X_MAGIC;
    h->version = version;
    h->header_size = hsize;
    h->min_api_ver = OS32X_HDR_V3_MIN_API;
    h->load_addr = 0x500000UL;
    h->kapi_data_off = off;
}

int main(void)
{
    OS32Header h;
    const u32 K = (u32)KAPI_DATA_FIELDS_OFF;
    const u32 FULL = 4096u;

    printf("=== 票 TASK_KAPI_DATA_FIELDS: OS32X ヘッダ v3 の照合 ===\n");

    printf("== 生成物と定数 ==\n");
    check(KAPI_DATA_FIELDS_OFF == 0x4B8, "KAPI_DATA_FIELDS_OFF = 8 + 4 x 300 = 0x4B8");
    check(KAPI_FUNC_CAPACITY == 300, "KAPI_FUNC_CAPACITY = 300");
    check(KAPI_FUNC_COUNT <= KAPI_FUNC_CAPACITY, "関数数は容量以内");
    check(OS32X_HDR_V3_SIZE == 48 && OS32X_HDR_VERSION == 3, "ヘッダ v3 は 48 バイト");
    check(OS32X_HDR_V3_MIN_API >= 63, "v3 の最低 KAPI は 63 以上");

    printf("== v2 以前は断る (配置を示す欄が無い) ==\n");
    make_hdr(&h, 2, OS32X_HDR_V2_SIZE, 0);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_OLD, "v2 (44B) → OLD");
    make_hdr(&h, 1, OS32X_HDR_V1_SIZE, 0);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_OLD, "v1 (40B) → OLD");
    /* v2 だが偶然 0x2C に v3 と同じ値が載っていても通さない */
    make_hdr(&h, 2, OS32X_HDR_V2_SIZE, K);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_OLD,
          "v2 で 0x2C が偶然一致しても OLD (version を先に見る)");

    printf("== v3 で値違いは断る ==\n");
    make_hdr(&h, 3, OS32X_HDR_V3_SIZE, K - 4u);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_MISMATCH,
          "v3 で kapi_data_off が 4 小さい (v62 風) → MISMATCH");
    make_hdr(&h, 3, OS32X_HDR_V3_SIZE, K + 4u);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_MISMATCH,
          "v3 で kapi_data_off が 4 大きい → MISMATCH");
    make_hdr(&h, 3, OS32X_HDR_V3_SIZE, 0);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_MISMATCH,
          "v3 で kapi_data_off = 0 → MISMATCH");

    printf("== v3 で一致なら通す ==\n");
    make_hdr(&h, 3, OS32X_HDR_V3_SIZE, K);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_OK, "v3 一致 → OK");
    check(os32x_layout_check(&h, OS32X_HDR_V3_SIZE, K) == OS32X_LAYOUT_OK,
          "読めた長さがちょうど 48 でも OK");
    make_hdr(&h, 4, OS32X_HDR_V3_SIZE + 4u, K);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_OK,
          "将来の v4 (末尾追記) でも配置が一致すれば OK");

    printf("== 長さの境界 ==\n");
    make_hdr(&h, 3, OS32X_HDR_V3_SIZE, K);
    check(os32x_layout_check(&h, OS32X_HDR_V3_SIZE - 1u, K) == OS32X_LAYOUT_SHORT,
          "読めた長さが 47 → SHORT (後ろの古いバッファを信じない)");
    check(os32x_layout_check(&h, 0, K) == OS32X_LAYOUT_SHORT, "読めた長さ 0 → SHORT");
    make_hdr(&h, 3, OS32X_HDR_V2_SIZE, K);
    check(os32x_layout_check(&h, FULL, K) == OS32X_LAYOUT_SHORT,
          "version 3 なのに header_size 44 → SHORT");
    make_hdr(&h, 2, OS32X_HDR_V2_SIZE, K);
    check(os32x_layout_check(&h, OS32X_HDR_V2_SIZE, K) == OS32X_LAYOUT_OLD,
          "44 バイトしか無い v2 ファイル → OLD");
    check(os32x_layout_check((const OS32Header *)0, FULL, K) == OS32X_LAYOUT_SHORT,
          "NULL → SHORT");

    printf("== 理由の文言 (表示用) ==\n");
    check(strcmp(os32x_layout_reason(OS32X_LAYOUT_MISMATCH),
                 "KAPI data layout mismatch") == 0, "MISMATCH の文言");
    check(strcmp(os32x_layout_reason(OS32X_LAYOUT_OK), "ok") == 0, "OK の文言");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
