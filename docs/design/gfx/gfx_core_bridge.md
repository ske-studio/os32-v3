# sys/hal/gfx/gfx_core_bridge.inc — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§2-3・§2-5 e6) の契約を今のソースと照らして要約
ソース: `sys/hal/gfx/gfx_core_bridge.inc` (`sys/hal/gfx/gfx_core.c` が取り込む)

旧公開の void `gfx_get_framebuffer` を USER から呼んだときの互換の橋 (`gfx_framebuffer_bridge`)。USER には kernel alias を返さず、取り消せる CLIENT の lease の VA を返す。

## 契約

- TRUSTED と USER の区別は**保存された呼び手の由来** (`caller_access_get`) で決め、現在の CPL・CR3 で決めない。TRUSTED は内部 fb (`gfx_kernel_framebuffer`) の alias をそのまま写す。
- 出力 (`GFX_Framebuffer`) は書く前に B1 で範囲を検査する (`check_caller_write_range`)。
- USER は表示方針 (`gfx_policy_blocks_map`)・CLIENT の公開元 (`gfx_surface_source`、init 前は not-ready)・query の授権と ref 照合を通ってから lease する。
- **互換の token は AS あたり 1 本**で、8 本の lease 枠の内数 (`AS_LEASE_GFX_COMPAT` の印を lease の flags に持つ。PTE の属性には混ぜない)。同じ sid・同じ世代の印付き lease があれば再利用し、2 本目を取らない。世代が変わった (再 init・revoke の後) 印付き lease は返してから取り直す。
- 返す planes は lease の VA + `plane_offset[i]`、寸法は内部 fb と同じ。
- 失敗では今回取った token だけを返し、出力は全欄 0 (書けるなら) にして pointer を残さない。USER の失敗では当該 slot に終了要求 (`abort_req`) を立て、実際の終了は syscall の出口で行う。TRUSTED の失敗では終了要求を立てない。
- 公開元の構築から copyout まで callback・scheduling を挟まない。

## 新しい SDK との関係

新しい SDK の checked attach (`userland/lib/gfx/libos32gfx_core.c`) は query と lease を自分で呼び、この橋を呼ばない (二重取得しない)。CPL0 (gshell を含む) と v70 未満の KAPI だけが `gfx_get_framebuffer` を使う。

## 検査

`tools/tests/test_gfx_attach.py` が kernel 側 (query・lease・台帳・paging・3 backend) と SDK の core・描画 C を連結し、互換 token の再利用、TRUSTED の不正出力で終了要求を立てないこと、USER に kernel alias を返さないこと、失敗時の出力消去と終了要求を確かめる。
