# include/video_service.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `include/video_service.h`

`include/video_service.h` と `hwdev/gf/service.c` に内部サービス、`hwdev/gf/internal.h` に部品間契約を置き、P0 で固定する。HAL に GF のレジスタ・ROM・recover token を出さない。
```c
struct video_device; /* 私有 context と関数表、装置ごとに一つ */
struct video_mode { u32 id, width, height, pitch, format, dac_bits, clock_khz; };
struct video_info { u32 caps, mode_count, current_mode, active; };
struct video_fb { u32 phys, bytes, pitch, format; };
int video_query(struct video_device *d, struct video_info *out);
int video_mode_info(struct video_device *d, u32 index, struct video_mode *out);
int video_start(struct video_device *d, u32 mode_id, const u8 *rgb256);
int video_framebuffer(struct video_device *d, struct video_fb *out);
int video_palette(struct video_device *d, u32 first, u32 count, const u8 *rgb);
int video_display_start(struct video_device *d, u32 byte_offset);
int video_blank(struct video_device *d, int blank);
int video_stop(struct video_device *d);
```
成功 0、引数・範囲不正は既存 `OS32_ERR_INVAL`、未対応は `OS32_ERR_NOSYS`、装置処理失敗は既存下層と同じ -1。新しい失敗分類を作らず、最後の段・対象・既存 rc を保持する。
`video_start` は初回だけ devinit→modeset。stop は局所後始末→quiesce、続けて上層が 98 切替を行う。停止済み stop・同値 blank は成功。start-offset は BAR1 相対 byte、0 だけ成功、非零は NOSYS。
mode ID と配列 index は区別する。format は既存 PACKED8、RGB は各 8bit。入力は呼出し中だけ借用し必要値を shadow へコピーする。情報取得は I/O・割当て・再初期化をせず、失敗時 out をゼロにする。
FB の bytes は表示用予約 307,200 B で BAR1 全長とは別。palette の first/count と FB の offset/pitch/height を既存範囲内で検査し、不正なら I/O 前に返す。active/能力には成功済みの機能だけを載せる。
Trident は自身の mode/FB/DAC 情報を返し、未対応 start-offset/blank は NOSYS。GF=01 と Trident の 0FACh 選択値はボード側へ分ける（`T/TASK_TRIDENT_DRIVER.md:426–444`）。独立 DPMS と VBE 状態 blob の完全復元は初版に含めない。
Trident fake で契約成立、ABI と選択門・失敗出口を確認
## 索引

- [hwdev/gf/](../hwdev/gf/README.md)
- [drivers/video/geforce/](../drivers/video/geforce/README.md)
- [hwdev/gf/internal.h](../hwdev/gf/internal.md)
