/* ========================================================================
 *  crc32_core.inc — CRC-32 のストリーム核 (カーネル / ユーザーランド共用)
 *
 *  票: H1 (docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §4.1)
 *
 *  既存 lib/crc32.c の crc32_calc() と**同じ CRC-32**:
 *    反転多項式 0xEDB88320 / 内部初期値 0xFFFFFFFF / 最後に 0xFFFFFFFF を XOR。
 *    Python の zlib.crc32、PNG、zip、userland/lib/save/save_meta.c の
 *    save_crc32 と同じ値になる。
 *
 *  ここが提供するのは「**未確定の内部状態**を入出力する更新関数」:
 *
 *      state = CRC32_INIT;
 *      state = crc32_core_update(state, chunk1, len1);
 *      state = crc32_core_update(state, chunk2, len2);
 *      crc   = crc32_core_final(state);        <- 最後の XOR は **1 回だけ**
 *
 *  チャンクごとに crc32_core_final() を掛けて XOR で畳むのは**別物**であり、
 *  全体の CRC にはならない。分割読みの途中で確定させないこと。
 *
 *  依存はコンパイラだけ (u8 / u32 の typedef を includer が用意する)。
 *  カーネルは lib/crc32.c 経由、外部プログラムは直接 #include する。
 *  CRC のためだけに KernelAPI を増やさない ([ABI2] / 票 H1)。
 *
 *  **表サイズ**: 16 要素 (64 バイト) の nibble 表。
 *    - ビット版 (1 バイトあたり 8 回の分岐) の約 2.2 倍速 (ホスト x86-64 /
 *      gcc -O2 / 8MB で 0.064s -> 0.029s の実測)。分岐の重い 386 では
 *      差はこれより開く側に出る。
 *    - 256 要素 (1KB) 版はさらに約 2 倍速いが、hsync の CRC は必ず
 *      HostDrv のハイパーコールと ext2 の読取に同伴するので、1 バイトあたり
 *      数サイクルの差は測れない。常駐イメージに 1KB を置く価値は無い
 *      (lib/crc32.c が元から 1KB 表を断っていた理由と同じ)。
 *    - 実測は**ホスト上の相対値**であり PC-98 実機の測定ではない。
 *
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 * ======================================================================== */

#ifndef LIB_CRC32_CORE_INC
#define LIB_CRC32_CORE_INC

/* 内部状態の初期値。**完成した CRC ではない** */
#define CRC32_INIT      0xFFFFFFFFUL
/* 確定時の XOR。1 ストリームにつき 1 回だけ掛ける */
#define CRC32_XOROUT    0xFFFFFFFFUL

/* nibble 表: crc32_core_nib[i] = 反転多項式で i を 4 ビット送った値。
 * tools/tests/hsync_h1_host.c が実行時に組み直して一致を確かめる。 */
static const u32 crc32_core_nib[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

/* 未確定の内部状態を受け取り、size バイトを送り込んだ内部状態を返す。
 * size == 0 なら state をそのまま返す (空チャンクは無害)。 */
static u32 crc32_core_update(u32 state, const void *data, u32 size)
{
    const u8 *bytes = (const u8 *)data;
    u32 crc = state;
    u32 i;

    if (!bytes) return crc;
    for (i = 0; i < size; i++) {
        crc ^= (u32)bytes[i];
        crc = (crc >> 4) ^ crc32_core_nib[crc & 0x0FUL];
        crc = (crc >> 4) ^ crc32_core_nib[crc & 0x0FUL];
    }
    return crc;
}

/* 内部状態を完成した CRC-32 にする。ストリームの最後に 1 回だけ。 */
static u32 crc32_core_final(u32 state)
{
    return state ^ CRC32_XOROUT;
}

#endif /* LIB_CRC32_CORE_INC */
