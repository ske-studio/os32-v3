/* ======================================================================== */
/*  crc32.c — 標準 CRC-32                                                   */
/*                                                                          */
/*  計算そのものは lib/crc32_core.inc (カーネル / ユーザーランド共用の核)。  */
/*  ここはカーネル側の公開窓口で、                                          */
/*    - 一括版 crc32_calc()  … 戻り値・保存形式は**従来のまま**             */
/*    - ストリーム版 crc32_stream_*() … 未確定の内部状態を入出力する        */
/*  の 2 つを出す。表は 64 バイトの nibble 表なので、以前ここで断っていた    */
/*  「256 エントリ (1KB) をカーネルに常駐させる」問題は起きない             */
/*  (選定の根拠は crc32_core.inc の頭、票 H1 / 設計書 §4.1)。               */
/* ======================================================================== */
#include "crc32.h"
#include "crc32_core.inc"

u32 crc32_calc(const void *data, u32 size)
{
    return crc32_core_final(crc32_core_update(CRC32_INIT, data, size));
}

u32 crc32_stream_init(void)
{
    return CRC32_INIT;
}

u32 crc32_stream_update(u32 state, const void *data, u32 size)
{
    return crc32_core_update(state, data, size);
}

u32 crc32_stream_final(u32 state)
{
    return crc32_core_final(state);
}
