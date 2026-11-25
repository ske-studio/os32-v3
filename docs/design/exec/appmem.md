# sys/kern/exec/appmem.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§3-2・§3-5 f2〜f5) の契約を今のソースと照らして要約
ソース: `sys/kern/exec/appmem.c`・`sys/kern/exec/appmem_map.c`・`sys/kern/exec/appmem_unmap.c` (型と定数は `sys/kern/exec/appmem.h`・`sys/include/appmem_types.h`)

アプリ帯の仮想の使い道を AS ごとの **extent 表** で持ち、`mem_map` / `mem_unmap` (KAPI `wrap_mem_map`・`wrap_mem_unmap`) と exec_heap の伸長・返却がこの表を通る。PTE・PFN の操作は `sys/kern/mm/paging_app.c` (設計は `docs/design/mm/paging_app.md`) が持ち、この部品は表と計画だけを持つ。

## 表と型

- `struct appmem_extent` は `{u32 base, end, kind, flags}` の 16 B、`struct appmem_table` は固定 32 本 (`APPMEM_EXTENT_MAX`) の 512 B。AS (`struct addrspace` の `appmem`) に埋め込む。実行中に伸ばさない。
- 表は base の昇順に詰め、空 slot は末尾に全欄 0。整列していない・重なる・帯 (`MEM_EXEC_LOAD_ADDR` から `MEM_APP_BAND_MAX_TOP`) の外・kind 不明・空 slot の後ろに有効な行がある表は `table_count` が EINVAL とし、どの口も触らない。
- kind (`enum appmem_kind`): `APPMEM_LIBC_INITIAL`・`APPMEM_EXEC_INITIAL`・`APPMEM_ANON`・`APPMEM_EXEC_ARENA`・`APPMEM_EXEC_LARGE`。カーネル内部の識別で、公開引数にはしない。公開の `mem_map` が作るのは ANON だけ。
- `flags` は map の flags とは別の内部属性。EXEC_LARGE の識別は extent と base だけで持ち、USER が書ける領域の値を使わない。
- image・BSS の端ページ・stack・guard・shlib・lease は表の行にせず、`struct appmem_layout` (`img_end`・`primary_mapped_end`・`exec_heap_cur_end`・`guard_b`) で除外する。layout は起動時にカーネルが保存した値だけで、呼び手の値を使わない。`img_end` 以外はページ整列。

## 起動時の登録 (`appmem_init`)

- `[page_align(img_end), primary_end)` を LIBC_INITIAL、`[MEM_EXEC_HEAP_BASE, exec_end)` を EXEC_INITIAL として登録する。image の端数ページは image のまま (返却しない)。
- layout の `primary_mapped_end` は `page_align(img_end)` にする。これで LIBC_INITIAL を unmap した穴を flags 0 の map が再利用できる。

## map の計画 (`appmem_prepare`)

| 入力 | 扱い |
|---|---|
| bytes | 0 は EINVAL。ページへ切り上げる前に `bytes > 0xFFFFFFFF - 4095` を拒否 (丸めの桁あふれ) |
| map_flags | 0・`APPMEM_MAP_EXACT`・`APPMEM_MAP_TOPDOWN`・両方。それ以外のビットは EINVAL。両方なら EXACT が優先 |
| hint | NULL は「希望なし」。EXACT で NULL は EINVAL。非 NULL はページ整列・`MEM_APP_BAND_MAX_TOP` 未満・`hint + size` が桁あふれしないこと |

- 窓は 2 つ。**flags 0** は `[primary_mapped_end, MEM_EXEC_HEAP_BASE)` を**上端から下向き**に探し、primary の break の直上を先に塞がない。**TOPDOWN** は `[exec_heap_cur_end, guard_b)` を guard の直下から下向きに探し、exec_heap の現在端より下へは出ない。両方とも同じ降順の穴探し (`find_hole`) を窓で切って使う。
- hint は flags に関係なくどちらかの窓に丸ごと入れば有効 (flags 0 でも上の窓を希望できる)。窓から外れる hint (image・shlib・lease・stack/guard・現在の exec_heap・低位・帯の外) は EINVAL で、別の穴へ逃がさない。
- 有効な hint の範囲が空いていれば先にそれを採る。埋まっていれば EXACT は ENOVA (別の穴へ逃がさない)、EXACT でなければ窓の穴探しへ進む。穴が無ければ ENOVA。
- 隣と併合するのは kind と flags が同じ行だけ。EXEC_LARGE は同じ flags でも併合しない。`MEM_EXEC_HEAP_BASE` をまたいで併合しない (exec_heap が空でも)。
- 併合後の本数が 32 を超えると EFULL。満杯でも片側併合なら通り、両側併合で 1 本減る。
- 失敗では表も出力の計画も変えない。成功しても表は変えない (計画を返すだけ)。

## 公開の確定 (`appmem_plan_valid`・`appmem_publish`)

- 計画は表の行数・範囲が空いていること・併合の結果 (first・remove_count・併合後の範囲) を表から計算し直して照合する。古い計画・行数が同じでも中身の変わった計画は拒否し、publish は何もしない。
- publish は先頭・途中・末尾への挿入、片側・両側の併合を行い、空いた末尾 slot を全欄 0 にする。

## map の取引 (`appmem_map`)

順序は固定: 呼び手の文脈 (`paging_app_context`) → 計画 → 計画の照合 → PTE の準備 (`paging_app_stage`) → 計画の照合 (2 回目) → IRQ を保存して commit → 表の publish → active なら CR3 再ロード → IRQ を戻す。

- 準備が PT・データページの不足で失敗したら ENOSPC (`APPMEM_ENOSPC`)。このとき当該 AS の trim を要求する (`appslot_trim_request_as`)。
- 2 回目の照合で外れたら準備を abort して EINVAL。どの失敗でも表・PTE・PD・所有ページは呼ぶ前と同じ。
- prepare から publish まで同じ表の上で直列・非再入で、callback・AS 切り替えを挟まない。計画は呼び手のスタックに置き、AS や AppSlot には持たない。

## unmap (`appmem_unmap_prepare`・`appmem_unmap`)

- bytes はページの倍数で 0 でない、base は整列、`base + bytes` が桁あふれしないこと。
- 範囲は既存の行で**隙間なく**覆われ、全部が許す kind であること。一つでも穴・image・stack・shlib・lease (表の外) を含めば全部拒否 (EINVAL)。複数の行をまたいでもよい。
- 公開の口 (`appmem_unmap`) が許す kind は ANON と LIBC_INITIAL だけ (`APPMEM_PUBLIC_UNMAP_MASK`)。EXEC_* は全部拒否し、表を変えない。ANON と LIBC_INITIAL は隣でも併合しない。
- 残る片は最大 2 (左と右) で、元の kind と flags (EXEC_LARGE の識別を含む) を保つ。中抜きで 32 本を超えるなら EFULL で全部不変。31 本からの中抜きは通る。
- 順序: 計画 → 照合 → PTE の検査 (`paging_app_unmap_prepare`) → 照合 → IRQ を保存して取り下げ (`paging_app_unmap_withdraw`) → IRQ を戻して返却 (`paging_app_unmap_free`) → 表の publish。返却が失敗したら表は変えず、AS は毒状態になる (`docs/design/mm/paging_app.md`)。
- カーネル内部の口: `appmem_exec_unmap` は ARENA・LARGE の行を**丸ごと**だけ返す (INITIAL は teardown まで返さない)。`appmem_exec_trim` は ARENA の末尾だけを返し、残りは ARENA の kind と flags を保つ。

## 誤りコード

| 内部 | 意味 | 公開 (`appmem_error_public`) |
|---|---|---|
| `APPMEM_EINVAL` (-1) | 引数・文脈・表の不整合、拒否 | `OS32_ERR_INVAL` |
| `APPMEM_ENOVA` (-2) | 仮想の穴が無い・EXACT の衝突 | `OS32_ERR_INVAL` |
| `APPMEM_EFULL` (-3) | extent 32 本を超える | `OS32_ERR_FULL` |
| `APPMEM_ENOSPC` (-4) | PT・データの物理ページ不足 | `OS32_ERR_NOSPC` |

EINVAL・ENOVA・EFULL は物理を確保する前に決まる。

## 検査

`tools/tests/test_appmem.py` (表と計画、実 `sys/kern/exec/appmem.c` を ILP32 でリンク) と `tools/tests/test_appmem_map.py` (取引と teardown、実 paging・pgalloc と連結)。
