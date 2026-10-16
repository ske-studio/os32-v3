状態: **設計中** (2026-10-16、実装・検査未実施)。

## 目的
[T3票](../TASK_T3_LAYOUT.md) §2-2・§4 T3ab の恒等写像とguardを担当する。
A/Bの区間を使う形に直し、新配置fixtureと現在のPG開始時の意味を分けて確認する。

## 触るファイルと関数
- `kernel/paging.c:214` fixed_paging_valid、`:245` boot_range_valid、`:262` paging_init。
- 同`:479` paging_map_ledger_backing、`:848` paging_addrspace_create_n。
- 同`:1371` paging_memmap_selftest、`:1583` paging_master_audit、`:1634` paging_as_audit。
- 触ってはいけないもの: memmapの値、台帳供給、USER/lease/V86の契約、DMA装置制御。

## 変えること
- **旧・新の両配置を実装し、`MEM_LAYOUT_V5` で選ぶ（既定0）**。PD/PTの使用11頁と枠全体、guard、stack、shellを別区間として参照する。
- 新分岐はPG=0で構築→予約/NP適用→PT自身とstackのsup/RW確認→CR3→PG=1を実装。
- 新配置のSLACK/ledger 2頁はPG開始時から恒等sup/RW/WB、guardのみNP。旧配置のNP期待はGまで保持。
- FIXED backing入口を新配置では既存写像の検査にする。ARENA_TOPの動的backing経路は保持。
- shellは全ASでsupervisor共有、APP_BAND私有PTと分離。旧shell帯一括NPの仮定を区間化する。

## この段の検査
ホスト模型（T3b0の `tools/tests/layout_boot_fixture.h` など）でマクロ0/1の旧・新両配置を検査する。
実装時のみ、下記の直接試験を `MUTATE=0`、`make kernel`、`python3 tools/check_select.py --lint`。
変異は最後の Opus レビュー後の判断で実行し、一度に一欠陥、正常対照PASSと対応観測FAILを対にする。
- 新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へ組み、リンクまで通す（配備しない。Aで上書き変数をC/生成規則へ結線し、中間成果物も分離）。
- 製品ソースを取り込むホスト試験（`memory_boot_host.c`・`memmap_boot_host.c` など、その段の直接試験）を `-DMEM_LAYOUT_V5=0` と `=1` の両方で作って回す。模型だけで代替しない。
- `tools/tests/memmap_boot_host.c`・`layout_boot_fixture.h` の両配置模型も追随。
- `tools/tests/test_paging_bounds.py`、`test_memmap_boot.py`（b0の `layout_boot_fixture.h` を利用）。
- 足す・直す観測: PG直後/AS作成後の物理一致・USERなし・RW/WB、両guardのNP、予備PT保持。
- 旧配置の期待は不変、新配置はFIXED/ARENA_TOPの両方で確認する。
- 代表変異: PT自身NP化、SLACK一括NP、metadataのUSER混入を一つずつ検出。

## 段の終わりの条件
既定0で現配置を起動でき、段の前後で固定番地・manifestの固定区間・展開先とSQLiteの宛先・ASSERTは一致する。
浮く終端（`__bss_end`・KHEAPの始まりなど）は予算内の移動を許し、移動量を報告する。
固定部分の不一致や予算超過はこの段の完了に数えず、差と未確認を次段へ渡す。ゲスト未実施を合格扱いにしない。

## 次の段へ渡すもの・未確認
Dへ区間ごとのPTE期待とbacking入口の契約を渡す。
未確認: 新配置の実PG起動・AS生成はGまで未受入。旧NP期待をCだけでpresentへ変更しない。
