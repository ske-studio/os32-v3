/* ======================================================================== */
/*  KCG.C — 漢字キャラクタジェネレータ (KCG) ドライバ                       */
/*                                                                          */
/*  PC-98内蔵フォントROMから文字パターンを読み出してグラフィック描画          */
/*                                                                          */
/*  I/Oポートアクセス方式:                                                   */
/*    1. A1h ← JISコード下位バイト                                          */
/*    2. A3h ← JISコード上位バイト - 0x20                                   */
/*    3. A5h ← ライン番号 (0-15) + L/Rビット                                */
/*    4. A9h → パターンデータ (1バイト = 8ピクセル分)                         */
/*                                                                          */
/*  ANK文字:                                                                 */
/*    PC-98ではANK文字はJIS行0x29に配置 (半角漢字扱い)                       */
/*    例: ASCII 'A' (0x41) → JISコード 0x2941                               */
/*    ポート A3h ← 0x29-0x20 = 0x09                                         */
/*    出典: PC9800Bible §2-6-1 「半角漢字の場合」                            */
/*                                                                          */
/*  漢字: JIS上位=0x21-0x7E, 16x16ドット (左右各8ドット)                    */
/*                                                                          */
/*  Shift-JIS → JIS変換も内蔵                                               */
/*                                                                          */
/*  出典: PC9800Bible §2-6-5                                                */
/* ======================================================================== */

#include "kcg.h"
/* #include "gfx.h" removed */
#include "../kernel/kcg_module.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "endian_le.h"   /* フォント書庫ヘッダは LE の外部形式 */

/* スケール係数 (デフォルト=1, 最大4) */
int kcg_scale = 1;

/* キャッシュ用のポインタ定義 (memmap.h: MEM_FONT_CACHE_BASE = 0x01000) */
#include "memmap.h"
#define KANJI_CACHE_SIZE   (32 * 94 * 94)   /* 282,752 Bytes */
#define KANJI_FETCHED_SIZE (94 * 94)        /*   8,836 Bytes */
#define ANK_CACHE_SIZE     (16 * 256)       /*   4,096 Bytes */

static u8 *kanji_cache   = (u8 *)P2V_CONST((MEM_FONT_CACHE_BASE));
static u8 *kanji_fetched = (u8 *)P2V_CONST((MEM_FONT_CACHE_BASE + KANJI_CACHE_SIZE));
static u8 *ank_cache     = (u8 *)P2V_CONST((MEM_FONT_CACHE_BASE + KANJI_CACHE_SIZE + KANJI_FETCHED_SIZE));
static u8 *ank_fetched   = (u8 *)P2V_CONST((MEM_FONT_CACHE_BASE + KANJI_CACHE_SIZE + KANJI_FETCHED_SIZE + ANK_CACHE_SIZE));

static int kcg_boot_phase_open = 1;
static int font_loaded, cache_initialized;

int kcg_font_loaded(void) { return font_loaded; }

void kcg_init(void)
{
    kcg_scale = 1;
    /* Loaded glyphs are immutable after boot; void ABI cannot return NOSYS. */
    if (!kcg_boot_phase_open) return;
    if (cache_initialized) return;
    cache_initialized = 1;
    /* キャッシュフラグの初期化 — kmemset (rep stosd) で高速化 */
    kmemset(kanji_fetched, 0, KANJI_FETCHED_SIZE);
    kmemset(ank_fetched, 0, 256);
}

/* ======================================================================== */
/*  スケール設定                                                             */
/* ======================================================================== */
void kcg_set_scale(int scale)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    kcg_scale = scale;
}

/* ======================================================================== */
/*  ANKパターン読み出し (8x16, 16バイト)                                    */
/*  ch: ASCII/ANKコード (0x00-0xFF)                                         */
/*                                                                          */
/*  PC-98のANK文字はJIS行0x29に格納 (半角漢字コード)                        */
/*  ASCII 0x20-0x7F → JIS 0x2920-0x297F                                    */
/*  半角カナ 0xA1-0xDF → JIS 0x2A21-0x2A5F (推定)                          */
/* ======================================================================== */
void kcg_read_ank(u8 ch, u8 *buf)
{
    int line;
    u8 *cache_ptr;

    if (font_loaded && ank_fetched[ch]) {
        cache_ptr = ank_cache + (ch * 16);
        for (line = 0; line < 16; line++) buf[line] = cache_ptr[line];
        return;
    }

    kcg_rom_read_ank(ch, buf);
}

/* ======================================================================== */
/*  漢字パターン読み出し (16x16, 32バイト)                                  */
/*  jis_code: JISコード (例: 0x2422 = 'あ')                                */
/*  buf: 32バイト出力 [line*2+0]=左8dot, [line*2+1]=右8dot                  */
/* ======================================================================== */
void kcg_read_kanji(u16 jis_code, u8 *buf)
{
    int line;
    u8 hi = (u8)(jis_code >> 8);
    u8 lo = (u8)(jis_code & 0xFF);
    int idx;
    u8 *cache_ptr;

    /* JISコードのバリデーション (0x2121 - 0x7E7E) */
    if (hi >= 0x21 && hi <= 0x7E && lo >= 0x21 && lo <= 0x7E) {
        idx = (hi - 0x21) * 94 + (lo - 0x21);
        if (font_loaded && kanji_fetched[idx]) {
            cache_ptr = kanji_cache + (idx * 32);
            for (line = 0; line < 32; line++) buf[line] = cache_ptr[line];
            return;
        }
    }

    kcg_rom_read_kanji(jis_code, buf);
}

/* ======================================================================== */
/*  外部フォントファイル (.kcgfont) ロード                                  */
/*                                                                          */
/*  ホスト側で gen_kcg_font.py を使って事前レンダリングした1bitビットマップ  */
/*  をKCGキャッシュメモリに直接ロードする。                                  */
/*                                                                          */
/*  フォーマット:                                                            */
/*    [0:4]   マジック "KCG1"                                               */
/*    [4:8]   展開後サイズ (u32 LE)                                         */
/*    [8:12]  フラグ (bit0: LZ4圧縮)                                       */
/*    [12:16] 予約                                                           */
/*    [16..]  ペイロード (キャッシュメモリレイアウト互換)                    */
/*                                                                          */
/*  ペイロード配置:                                                          */
/*    kanji_cache  (282,752 B) + kanji_fetched (8,836 B)                    */
/*    + ank_cache  (4,096 B) + ank_fetched (256 B)                          */
/*    = 295,940 B (展開後)                                                   */
/*                                                                          */
/*  戻り値: 0=成功, -1=ファイルエラー, -2=フォーマット不正, -3=LZ4エラー    */
/* ======================================================================== */

#include "../fs/vfs.h"
#include "../lib/lz4.h"
#include "kprintf.h"
#include "memmap.h"

#define KCG_FONT_MAGIC  0x3147434B  /* "KCG1" リトルエンディアン */
#define KCG_FLAG_LZ4    0x01
#define KCG_PAYLOAD_SIZE (KANJI_CACHE_SIZE + KANJI_FETCHED_SIZE + \
                          ANK_CACHE_SIZE + 256)

/*
 * LZ4圧縮データの boot 専用 scratch。
 * MEM_KCG_LZ4_TEMP_BASE〜END の 344KB が利用可能。
 * 圧縮フォントデータは ~180KB なので十分。
 *
 * boot 内部読込だけに限定し、閉鎖後は BB・mailbox を上書きしない。
 * 上限は VRAM の直前 (MEM_CONV_END)。カーネルスタックを 0x1FC000 へ
 * 退避する前は 0x8F000 (スタックガード) で頭打ちだった。
 */
#define LZ4_TEMP_BUF     ((u8 *)P2V_CONST(MEM_KCG_LZ4_TEMP_BASE))
#define LZ4_TEMP_MAX     (MEM_KCG_LZ4_TEMP_END - MEM_KCG_LZ4_TEMP_BASE)  /* 344KB */

/*
 * チャンク分割読み込みヘルパー。
 * vfs_read_fd の大量データ一括読み込みで ext2/HostDrvFS がクラッシュする
 * 問題を回避するため、CHUNK_SIZE ずつ分割して読み込む。
 */
#define LOAD_CHUNK_SIZE 1024

static int kcg_read_chunked(int fd, u8 *dst, int total)
{
    int done = 0;
    int n;
    int chunk;
    while (done < total) {
        chunk = total - done;
        if (chunk > LOAD_CHUNK_SIZE) chunk = LOAD_CHUNK_SIZE;
        n = vfs_read_fd(fd, dst + done, chunk);
        if (n <= 0) break;
        done += n;
    }
    return done;
}

/* boot 内部読込だけが scratch を使える。kcg_init でも再開しない。 */

void kcg_boot_phase_close(void)
{
    if (!kcg_boot_phase_open) return;
    kcg_boot_phase_open = 0;  /* 先に scratch を失効、TRUSTED も NOSYS */
    kmemset(P2V(MEM_GFX_BB_BASE), 0, MEM_GFX_BB_SIZE);
    kmemset(P2V(MEM_AUTOPLAY_MAILBOX_BASE), 0, MEM_AUTOPLAY_MAILBOX_SIZE);
}

int kcg_load_font(const char *path)
{
    int fd;
    u8 hdr[16];
    u32 magic, payload_size, flags;
    int n, compressed_size, ret;

    if (!kcg_boot_phase_open || kcg_module_attempted()) return OS32_ERR_NOSYS;

    kprintf(0x07, "[KCG] loading: %s\n", path);

    fd = vfs_open(path, 0);
    if (fd < 0) {
        return -1;
    }

    /* ヘッダ読み込み (16バイト) */
    n = vfs_read_fd(fd, hdr, 16);
    if (n < 16) {
        vfs_close(fd);
        return -1;
    }

    magic = le32_rd(&hdr[0]);
    if (magic != KCG_FONT_MAGIC) {
        vfs_close(fd);
        kprintf(0x07, "[KCG] invalid magic\n");
        return -2;
    }

    payload_size = le32_rd(&hdr[4]);
    flags = le32_rd(&hdr[8]);

    if (payload_size != KCG_PAYLOAD_SIZE) {
        vfs_close(fd);
        kprintf(0x07, "[KCG] size mismatch\n");
        return -2;
    }

    if (flags & KCG_FLAG_LZ4) {
        /* === LZ4圧縮データ === */
        compressed_size = (int)vfs_get_size(fd) - 16;
        if (compressed_size <= 0 || compressed_size > (int)LZ4_TEMP_MAX) {
            vfs_close(fd);
            kprintf(0x07, "[KCG] bad size: %d\n", compressed_size);
            return -1;
        }
        kprintf(0x07, "[KCG] LZ4: %d bytes\n", compressed_size);

        /* 圧縮データをチャンク分割で boot scratch に読み込み */
        n = kcg_read_chunked(fd, LZ4_TEMP_BUF, compressed_size);
        vfs_close(fd);

        if (n < compressed_size) {
            kprintf(0x07, "[KCG] short: %d/%d\n", n, compressed_size);
            return -1;
        }

        kprintf(0x07, "[KCG] read OK, decoding...\n");

        /* LZ4展開 → キャッシュメモリに直接書き込み */
        ret = lz4_decode(LZ4_TEMP_BUF, compressed_size,
                         kanji_cache, KCG_PAYLOAD_SIZE);

        if (ret < 0) {
            kprintf(0x07, "[KCG] LZ4 err: %d\n", ret);
            return -3;
        }

        kprintf(0x07, "[KCG] loaded (LZ4): %d -> %d\n",
                compressed_size, ret);
    } else {
        /* === 非圧縮データ: チャンク分割でキャッシュメモリに読み込み === */
        n = kcg_read_chunked(fd, kanji_cache, KCG_PAYLOAD_SIZE);
        vfs_close(fd);

        if (n < (int)KCG_PAYLOAD_SIZE) {
            kprintf(0x07, "[KCG] short: %d\n", n);
            return -1;
        }

        kprintf(0x07, "[KCG] loaded: %u bytes\n", payload_size);
    }

    font_loaded = 1;
    return 0;
}

/* End of kcg.c */

