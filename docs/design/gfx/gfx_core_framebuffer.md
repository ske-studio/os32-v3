# sys/hal/gfx/gfx_core_framebuffer.inc — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§2-3・§2-5 e4) の契約を今のソースと照らして要約
ソース: `sys/hal/gfx/gfx_core_framebuffer.inc` (`sys/hal/gfx/gfx_core.c` が取り込む)

カーネルの描画先 (内部 fb)、互換の `gfx_get_framebuffer`、旧 planar ポインタと backend の BB 記述子の結線を持つ。どれも SURFACE 台帳の CLIENT から引き、固定番地を持たない。

## 内部 fb (`gfx_kernel_framebuffer`)

- 起動前 (`gfx_started` = 0、prepare の後を含む) は **boot の PC98 CLIENT** を返す (640×400・planar・pitch 80)。起動後は選択中 backend の CLIENT を返す。
- 起動後の寸法は選択モードの幅・pitch・形式と、高さだけ `gfx_current_height` (200 行起動なら 200)。台帳の CLIENT の高さは 200 行でも 400 のまま (`docs/design/gfx/gfx_core.md` の「4. 選択モードと容量を分けて大きさを決める」)。
- planes[i] は CLIENT の backing の kernel alias (RAM は `P2V`、VRAM・MMIO は `P2V_IO`) + `plane_offset[i]`。PC98 は plane 間隔 32,000 B で、200 行でも詰めない。
- CLIENT が無い・closing なら planes[0] が NULL で `OS32_ERR_INVAL`。

## 互換の `gfx_get_framebuffer`

- USER からの呼び出しは `gfx_framebuffer_bridge` (`docs/design/gfx/gfx_core_bridge.md`) へ渡し、kernel alias を返さない。
- frame を持たない TRUSTED (CPL0) の呼び出しは**選択中** backend の CLIENT の alias と、選択モードの寸法を返す。init 前でも選択 backend の geometry (packed なら 640×480) を返す。init 前の公開 query も選択 backend の形式を返す。

## 結線 (`gfx_bind_client`)

- 旧 planar ポインタ `bb[0..3]`・`bb_b/r/g/i` と `g_backend` の `bb_base`・`bb_size`・`bb_pitch`・`bb_format` を、選択中 backend の CLIENT からまとめて設定する。ここが唯一の結線点。
- モードが ready でない・CLIENT が無い・closing なら全ポインタを NULL・`bb_size` 0 にする (前のモードのポインタを残さない)。
- 再 init の regen 後、init 後の probe 失敗で PC98 へ落ちた直後、prepare なしの backend 切り替えの後も必ずここを通り、選択中の CLIENT を指す。
- `gfx_selected_selfcheck` (`sys/hal/gfx/gfx_core_surface.inc`) が結線と台帳の一致を検査する。

## 検査

`test_gfx_kernel_fb.py (旧版 68b79d89963f)` が実 gfx_core・3 backend・台帳・paging・surface_query/lease を連結し、reserve 前の NULL、reserve 後の 32,000 B 間隔、各 backend の prepare → init → init_200 → shutdown、AUTO の Cirrus → PEGC → PC98 fallback、PEGC DISPLAY の UC 登録を確かめる。
