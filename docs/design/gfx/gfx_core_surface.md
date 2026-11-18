# gfx/gfx_core_surface.inc — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§2-2・§2-3・§2-5 e5) の契約を今のソースと照らして要約
ソース: `gfx/gfx_core_surface.inc` (`gfx/gfx_core.c` が取り込む)

起動時の SURFACE 予約の入口、query/lease の公開元、再 init・V86 帰りの revoke → regen、CLIENT の gshell への移譲を持つ。予約の表と手順は platform の provider (PC-98 は `platform/pc98/gfx_surfaces.c`) が持つ。

## 公開元 (`gfx_surface_source`)

- 役割は CLIENT と DISPLAY だけ。DISPLAY の本数は provider が答える (PC98 は B/R/G/I の 4 記録、PEGC・Cirrus は 1)。PC98 の DISPLAY は各面の先頭番地 (`display_provider_display_first`) の順に並べる。
- 再 init 中 (`gfx_surface_unready` のビット)・closing・別 backend・別役割の SURFACE は出さない。
- 起動 (`gfx_started`) 前、または `perm_max` が NONE の面を含むときは not-ready (`OS32_ERR_INVAL`、count 0) で、USER は query も lease もできない。

## 再 init (`gfx_reinit_surface_roles`)

二段で行い、段の間は公開元が not-ready。

1. revoke 段 (finish=0): 選択中 backend の CLIENT・DISPLAY (display_only なら DISPLAY だけ) に unready と pending の印を付け、全 AS からその sid の lease を外す (`lease_revoke_surface`)。master の CR3・IRQ 保存の区間で行う。ここでは世代を進めない。
2. regen 段 (finish=1): pending の面を `ledger_surface_regen` で新しい世代にし、成功した面だけ unready を外す。失敗は `gfx_reinit_fail_count` に数えて not-ready のまま。最後に `gfx_bind_client` で結線し直す。

- 順序は **貸与停止 → 全 AS の revoke → (TLB 同期) → 参照 0 → backend・geometry 更新 → 新世代の公開**。regen は参照が残る・master でない・closing・geometry 不正なら拒否する (`ledger_surface_regen` 側)。
- revoke は**選択中 backend の CLIENT・DISPLAY だけ**。別 backend の面 (例えば PEGC の CLIENT だけ持つ第三の AS) の PTE・lease・世代は変えない。
- revoke が一部の AS で失敗しても、同じ AS の後続の lease と他の AS の revoke は続ける。旧 token を生き返らせない。
- 両段とも呼び手の CR3 と IF を戻す (呼び手が revoke された lease を持っていても)。
- 200 行でも登録は 640×400・面間隔 32,000 B・同じ backing のまま (200 行は各面の前半を使う)。
- 世代が上限 (`LEDGER_SURFACE_GEN_MAX`) に達したら周回させず not-ready に留め、kernel の BB は保持する。

## TVRAM と V86 帰り

- `gfx_reinit_tvram` は backend によらず TVRAM の SURFACE を revoke → regen する (GUI に入るたび。停めた CUI の親が RO の view を持っていることがある)。
- `gfx_v86_return` は DISPLAY だけの二段 → TVRAM の順に行う。V86 の後は DISPLAY・TVRAM を取り直す必要がある。

## CLIENT の gshell への移譲 (`gfx_client_to_gshell`)

- 選択中の CLIENT の所有者が gshell でなければ: unready にして revoke、参照 0 かつ世代が上限でなければ `ledger_surface_transfer` → regen (世代 +1) で公開。どれかが失敗すれば移譲せず not-ready、kernel の backing は残す。
- 所有者が既に gshell なら何もしない (GUI → CUI → GUI で物理を取り直さない、世代も変えない)。
- 最後に TVRAM の revoke → regen と、gui 段の監査を行う。

## 検査

`tools/tests/test_gfx_reinit.py` が実 gfx_core・3 backend・lease・surface_query・paging・pgalloc を 3 つの AS で連結し、revoke と regen の順序、段の間の not-ready、第三 AS の不変、旧 token の INVAL・旧 ref の STALE、200 行の geometry、revoke 失敗の継続、移譲の 1 回性を確かめる。
