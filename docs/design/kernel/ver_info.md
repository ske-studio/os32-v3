# kernel/ver_info.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN (D1 で移した)
ソース: `kernel/ver_info.h`

- 単一利用者・単一タスク・協調切替。取得のための再検出、待ち、装置操作、権限強化は加えない。

- カーネルだけが SYS_VERSION を取り込み、新 KAPI の見出し行へ埋める。CUI・GUI は両方とも実行中カーネルの値を表示。

- Build / Commit / Image CRC は版と別の識別子として保持。ユーザーランド再配備だけでカーネル版は変わらない。

- 部品境界（下の VerSnapshot はカーネル内部、公開 ABI ではない）:

```c
/* kernel/ver_info.h: private snapshot + prototypes */
typedef struct {
    u32 cpu_measured, pit_valid, pit_hz, pit_input;
    u32 ser_ready, ser_mode, ser_baud, ser_fifo, pcm_present;
    GFX_ScreenInfo gfx; u32 gfx_active, gfx_kind;
    u32 api_version; char build[32];
    int boot_ok, debug_ok; BootImageInfo boot; MemStat debug;
    ServicesVerState services; /* kernel/sqlite_engine.h の内部型 */
} VerSnapshot;
void ver_collect(VerSnapshot *s); /* kernel/ver_collect.c, bounded reads */
void ver_format(const VerSnapshot *s, OS32_VerInfo *out); /* cold, bounded */
```

- IRQ 全体禁止で整形しない。協調切替中に yield せず保存値を読む。IRQ 更新数値は単一 u32 の読取りでよく、全装置同時刻の保証はしない。
- 再較正・FM レジスタ読書き・FIFO probe・ネットワーク通信・動的割当はゼロ。表示失敗でも UI から戻れる。
