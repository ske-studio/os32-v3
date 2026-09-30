/* ======================================================================== */
/*  PIPE_BUFFER.C -- パイプバッファ管理                                       */
/*                                                                          */
/*  シングルタスクOS向けのパイプバッファ。                                    */
/*  kmalloc で動的確保し、使用後に kfree で解放する。                          */
/*  静的配列を廃止し BSS を 128KB 削減。                                      */
/* ======================================================================== */

#include "pipe_buffer.h"
#include "fd_redirect.h"   /* res_owner_get */
#include "kmalloc.h"

/* パイプバッファ管理構造 (ポインタ + メタデータのみ。BSS = 数十バイト) */
static u8 *pipe_ptr[PIPE_BUF_COUNT];
static u32 pipe_len[PIPE_BUF_COUNT];
static int pipe_used[PIPE_BUF_COUNT];
static int pipe_owner[PIPE_BUF_COUNT];   /* 確保した実行レベル */

void pipe_buffer_init(void)
{
    int i;
    for (i = 0; i < PIPE_BUF_COUNT; i++) {
        pipe_ptr[i] = (u8 *)0;
        pipe_used[i] = 0;
        pipe_len[i] = 0;
        pipe_owner[i] = 0;
    }
}

int pipe_alloc(void)
{
    int i;
    for (i = 0; i < PIPE_BUF_COUNT; i++) {
        if (!pipe_used[i]) {
            /* 使用時に kmalloc で動的確保 */
            pipe_ptr[i] = (u8 *)kmalloc(PIPE_BUF_SIZE);
            if (!pipe_ptr[i]) {
                return -1;  /* メモリ不足 */
            }
            pipe_used[i] = 1;
            pipe_len[i] = 0;
            pipe_owner[i] = res_owner_get();
            return i;
        }
    }
    return -1;
}

void pipe_free_owned(int owner)
{
    int i;
    for (i = 0; i < PIPE_BUF_COUNT; i++) {
        if (pipe_used[i] && pipe_owner[i] == owner) {
            pipe_free(i);
        }
    }
}

void pipe_free(int id)
{
    if (id >= 0 && id < PIPE_BUF_COUNT) {
        if (pipe_ptr[id]) {
            kfree(pipe_ptr[id]);
            pipe_ptr[id] = (u8 *)0;
        }
        pipe_used[id] = 0;
        pipe_len[id] = 0;
        pipe_owner[id] = 0;
    }
}

u8 *pipe_get_buf(int id)
{
    if (id >= 0 && id < PIPE_BUF_COUNT && pipe_ptr[id]) {
        return pipe_ptr[id];
    }
    return (u8 *)0;
}

u32 pipe_get_capacity(int id)
{
    if (id >= 0 && id < PIPE_BUF_COUNT && pipe_ptr[id]) {
        return PIPE_BUF_SIZE;
    }
    return 0;
}

u32 pipe_get_len(int id)
{
    if (id >= 0 && id < PIPE_BUF_COUNT) {
        return pipe_len[id];
    }
    return 0;
}

void pipe_set_len(int id, u32 len)
{
    if (id >= 0 && id < PIPE_BUF_COUNT) {
        pipe_len[id] = (len > PIPE_BUF_SIZE) ? PIPE_BUF_SIZE : len;
    }
}

void pipe_clear(int id)
{
    if (id >= 0 && id < PIPE_BUF_COUNT) {
        pipe_len[id] = 0;
    }
}
