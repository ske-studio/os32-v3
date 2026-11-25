/* ======================================================================== */
/*  E2TEST.C — ext2 DIND 書き込み検証テストプログラム                        */
/*                                                                          */
/*  DIND (Double Indirect Block) 境界を跨ぐファイル書き込みと読み戻しの      */
/*  整合性を検証する。擬似乱数パターンを使用し、全バイトの一致を確認する。    */
/*                                                                          */
/*  テスト1: 280KB 新規作成 (DIND境界 268KB 跨ぎ)                            */
/*  テスト2: 280KB 上書き (既存ブロック上書きパス)                            */
/*  テスト3: 372632バイト 新規作成 (元バグサイズ)                             */
/*  テスト4: 372632バイト チャンク分割書き込み (64KBチャンク)                  */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

/* crt0_c.c で定義される KernelAPI ポインタ */
extern KernelAPI *kapi;
#define api kapi

/* テストファイルパス (/tmp はext2上に存在する) */
#define TEST_FILE "/tmp/e2test.dat"

/* テストサイズ定義 */
#define SIZE_280K  286720UL    /* 280 * 1024 = DIND境界跨ぎ */
#define SIZE_372K  372632UL    /* 元バグサイズ */
#define CHUNK_SIZE 65536UL     /* 64KB チャンクサイズ */

/* テスト結果カウンタ */
static int total_tests = 0;
static int passed_tests = 0;

/* ======================================================================== */
/*  擬似乱数パターン生成                                                     */
/*  各バイト = (offset * 7 + 0x5A) & 0xFF                                   */
/*  全ゼロや連番では見逃すバグを検出するため。                               */
/* ======================================================================== */
static void fill_pattern(unsigned char *buf, unsigned long size)
{
    unsigned long i;
    for (i = 0; i < size; i++) {
        buf[i] = (unsigned char)((i * 7 + 0x5A) & 0xFF);
    }
}

/* ======================================================================== */
/*  バイト比較 — 不一致箇所を報告                                            */
/* ======================================================================== */
static int verify_data(const unsigned char *expected,
                       const unsigned char *actual,
                       unsigned long size)
{
    unsigned long i;
    unsigned long mismatch_count = 0;
    unsigned long first_mismatch = 0;

    for (i = 0; i < size; i++) {
        if (expected[i] != actual[i]) {
            if (mismatch_count == 0) {
                first_mismatch = i;
            }
            mismatch_count++;
        }
    }

    if (mismatch_count == 0) {
        api->kprintf(ATTR_GREEN, "  verify: %lu/%lu bytes match ... PASS\n",
                     size, size);
        return 1;
    }

    api->kprintf(ATTR_RED, "  verify: FAIL — %lu mismatches\n", mismatch_count);
    api->kprintf(ATTR_RED, "  first mismatch at offset %lu: "
                 "expected 0x%02X, got 0x%02X\n",
                 first_mismatch,
                 (unsigned int)expected[first_mismatch],
                 (unsigned int)actual[first_mismatch]);

    /* 最大8箇所まで詳細を表示 */
    {
        unsigned long shown = 0;
        for (i = first_mismatch; i < size && shown < 8; i++) {
            if (expected[i] != actual[i]) {
                api->kprintf(ATTR_RED, "    [%lu] exp=0x%02X got=0x%02X\n",
                             i,
                             (unsigned int)expected[i],
                             (unsigned int)actual[i]);
                shown++;
            }
        }
    }

    return 0;
}

/* ======================================================================== */
/*  ファイル書き込み (一括)                                                   */
/* ======================================================================== */
static int write_file_bulk(const char *path, const unsigned char *data,
                           unsigned long size)
{
    int fd;
    int ret;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        api->kprintf(ATTR_RED, "  open(WRONLY) failed: fd=%d\n", fd);
        return -1;
    }

    ret = write(fd, (const char *)data, (int)size);
    close(fd);

    if (ret < 0) {
        api->kprintf(ATTR_RED, "  write failed: ret=%d\n", ret);
        return -1;
    }

    api->kprintf(ATTR_WHITE, "  write: %lu bytes in 1 call ... OK\n", size);
    return 0;
}

/* ======================================================================== */
/*  ファイル書き込み (チャンク分割)                                           */
/* ======================================================================== */
static int write_file_chunked(const char *path, const unsigned char *data,
                              unsigned long size, unsigned long chunk_size)
{
    int fd;
    unsigned long offset;
    int chunk_idx;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        api->kprintf(ATTR_RED, "  open(WRONLY) failed: fd=%d\n", fd);
        return -1;
    }

    offset = 0;
    chunk_idx = 0;
    while (offset < size) {
        unsigned long remain = size - offset;
        unsigned long to_write = (remain < chunk_size) ? remain : chunk_size;
        int ret;

        ret = write(fd, (const char *)(data + offset), (int)to_write);
        if (ret < 0) {
            api->kprintf(ATTR_RED, "  chunk[%d] write failed: ret=%d\n",
                         chunk_idx, ret);
            close(fd);
            return -1;
        }

        api->kprintf(ATTR_WHITE, "  chunk[%d]: %lu bytes at offset %lu ... OK\n",
                     chunk_idx, to_write, offset);
        offset += to_write;
        chunk_idx++;
    }

    close(fd);
    return 0;
}

/* ======================================================================== */
/*  ファイル読み戻し                                                         */
/* ======================================================================== */
static int read_file(const char *path, unsigned char *buf,
                     unsigned long expected_size)
{
    int fd;
    int total_read;
    int ret;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        api->kprintf(ATTR_RED, "  open(RDONLY) failed: fd=%d\n", fd);
        return -1;
    }

    total_read = 0;
    while ((unsigned long)total_read < expected_size) {
        int remain = (int)(expected_size - (unsigned long)total_read);
        ret = read(fd, (char *)(buf + total_read), remain);
        if (ret <= 0) {
            break;
        }
        total_read += ret;
    }

    close(fd);

    if ((unsigned long)total_read != expected_size) {
        api->kprintf(ATTR_RED, "  read: expected %lu, got %d bytes\n",
                     expected_size, total_read);
        return -1;
    }

    api->kprintf(ATTR_WHITE, "  read back: %lu bytes ... OK\n", expected_size);
    return 0;
}

/* ======================================================================== */
/*  ファイルサイズ検証 (stat)                                                */
/* ======================================================================== */
static int check_file_size(const char *path, unsigned long expected_size)
{
    OS32_Stat os_st;

    if (api->sys_stat(path, &os_st) < 0) {
        api->kprintf(ATTR_RED, "  stat failed\n");
        return -1;
    }

    if ((unsigned long)os_st.st_size != expected_size) {
        api->kprintf(ATTR_RED, "  stat: size=%lu, expected %lu\n",
                     (unsigned long)os_st.st_size, expected_size);
        return -1;
    }

    api->kprintf(ATTR_WHITE, "  stat: size=%lu ... OK\n", expected_size);
    return 0;
}

/* ======================================================================== */
/*  テスト1: 新規ファイル 280KB (DIND境界跨ぎ)                               */
/* ======================================================================== */
static int test1_new_280k(unsigned char *wbuf, unsigned char *rbuf)
{
    api->kprintf(ATTR_CYAN, "\n--- Test 1: New file 280KB (DIND boundary) ---\n");

    fill_pattern(wbuf, SIZE_280K);

    if (write_file_bulk(TEST_FILE, wbuf, SIZE_280K) < 0) return 0;
    if (check_file_size(TEST_FILE, SIZE_280K) < 0) return 0;
    if (read_file(TEST_FILE, rbuf, SIZE_280K) < 0) return 0;

    return verify_data(wbuf, rbuf, SIZE_280K);
}

/* ======================================================================== */
/*  テスト2: 同ファイル上書き 280KB                                          */
/* ======================================================================== */
static int test2_overwrite_280k(unsigned char *wbuf, unsigned char *rbuf)
{
    unsigned long i;

    api->kprintf(ATTR_CYAN, "\n--- Test 2: Overwrite 280KB ---\n");

    /* 異なるパターンで上書き */
    for (i = 0; i < SIZE_280K; i++) {
        wbuf[i] = (unsigned char)((i * 13 + 0xA5) & 0xFF);
    }

    if (write_file_bulk(TEST_FILE, wbuf, SIZE_280K) < 0) return 0;
    if (check_file_size(TEST_FILE, SIZE_280K) < 0) return 0;
    if (read_file(TEST_FILE, rbuf, SIZE_280K) < 0) return 0;

    return verify_data(wbuf, rbuf, SIZE_280K);
}

/* ======================================================================== */
/*  テスト3: 新規ファイル 372632バイト (元バグサイズ)                         */
/* ======================================================================== */
static int test3_new_372k(unsigned char *wbuf, unsigned char *rbuf)
{
    api->kprintf(ATTR_CYAN,
                 "\n--- Test 3: New file 372632 bytes (original bug size) ---\n");

    fill_pattern(wbuf, SIZE_372K);

    /* 前テストのファイルを削除してから新規作成 */
    unlink(TEST_FILE);

    if (write_file_bulk(TEST_FILE, wbuf, SIZE_372K) < 0) return 0;
    if (check_file_size(TEST_FILE, SIZE_372K) < 0) return 0;
    if (read_file(TEST_FILE, rbuf, SIZE_372K) < 0) return 0;

    return verify_data(wbuf, rbuf, SIZE_372K);
}

/* ======================================================================== */
/*  テスト4: チャンク分割書き込み 372632バイト (64KBチャンク)                  */
/* ======================================================================== */
static int test4_chunked_372k(unsigned char *wbuf, unsigned char *rbuf)
{
    api->kprintf(ATTR_CYAN,
                 "\n--- Test 4: Chunked write 372632 bytes (64KB chunks) ---\n");

    fill_pattern(wbuf, SIZE_372K);

    /* 前テストのファイルを削除 */
    unlink(TEST_FILE);

    if (write_file_chunked(TEST_FILE, wbuf, SIZE_372K, CHUNK_SIZE) < 0) return 0;
    if (check_file_size(TEST_FILE, SIZE_372K) < 0) return 0;
    if (read_file(TEST_FILE, rbuf, SIZE_372K) < 0) return 0;

    return verify_data(wbuf, rbuf, SIZE_372K);
}

/* ======================================================================== */
/*  エントリポイント                                                         */
/* ======================================================================== */
int main(int argc, char **argv, KernelAPI *k)
{
    unsigned char *wbuf;
    unsigned char *rbuf;

    (void)argc; (void)argv; (void)k;

    api->kprintf(ATTR_CYAN, "=== ext2 DIND Write Test ===\n");
    api->kprintf(ATTR_WHITE, "KAPI version: %d\n", kapi->version);

    /* 書き込み用バッファと読み戻し用バッファを確保 (最大372KB × 2) */
    wbuf = (unsigned char *)malloc(SIZE_372K);
    rbuf = (unsigned char *)malloc(SIZE_372K);

    if (!wbuf || !rbuf) {
        api->kprintf(ATTR_RED, "malloc failed! (need %luKB x 2)\n",
                     SIZE_372K / 1024);
        if (wbuf) free(wbuf);
        if (rbuf) free(rbuf);
        /* 372KB x 2 が取れない = 前提が無い。不合格ではなく SKIP。 */
        return os32_test_summary_skip(api, "e2test",
                                      "cannot allocate the 372KB buffers");
    }

    api->kprintf(ATTR_WHITE, "Buffers allocated: wbuf=0x%X rbuf=0x%X\n",
                 (unsigned int)wbuf, (unsigned int)rbuf);

    /* テスト実行 */
    total_tests++; if (test1_new_280k(wbuf, rbuf)) passed_tests++;
    total_tests++; if (test2_overwrite_280k(wbuf, rbuf)) passed_tests++;
    total_tests++; if (test3_new_372k(wbuf, rbuf)) passed_tests++;
    total_tests++; if (test4_chunked_372k(wbuf, rbuf)) passed_tests++;

    /* クリーンアップ */
    unlink(TEST_FILE);
    free(wbuf);
    free(rbuf);

    /* サマリ */
    return os32_test_summary(api, "e2test", passed_tests, total_tests);
}
