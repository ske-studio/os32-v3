状態: **受入完了** (2026-10-17) — 6b4e37e で main へ。NP21/W 64MB の受入 (2026-10-16) と実機 Ra266 の回帰 T3bR (2026-10-17、全項目 PASS) が済んだ。

## 目的
[T3票](../../../tasks/v3/TASK_T3_LAYOUT.md) §2-1・§2-2・§2-3・§4 T3ab の一体切替を行う。
A〜Fの準備を同じworktreeで受け、上位票 §2-1 の案5を実配置として初めて有効にする。

## 触るファイルと関数
- A〜Fで触った全ファイル：マクロ切替・旧分岐削除、C/ASM/linker/boot/R1の写しと生成規則（各票の一覧）。
- `sdk/kapi.json` の `generations.memory_layout`、既存生成経路による `build/out/generations-manifest.json` と対応生成物。
- `tools/tests/memory_boot_host.c`・`memmap_boot_host.c`・`layout_boot_fixture.h`、exec/nanoの新帯期待と番地直書き試験。
- `tools/tests/dma_pool_host.c:25`、`pcm_cs4231_host.c:176`、`dma8237_host.c:84`（池/バンク期待）。
- `tools/tests/test_mem_stat.py:276` mainを含むfixture、`test_memmap_gen.py:179`・`:215` 写し/地図期待。
- `tools/tests/paging_bounds_host.c:82` MMIO PT、`test_dma_pool.py:48` 変異の境界前提。
- `tools/tests/vk32_host.c:30` WINDOW、`test_vk32_crc.py:52` LOAD_END。各試験名は下記。
- 触ってはいけないもの: 案5の決定、Unicode/KAPI機能追加、T3b1以後の範囲。生成物の手編集は禁止。

## 変えること
- `MEM_LAYOUT_V5` を1にして案5へ切替後、A〜Fの旧分岐とマクロそのものを削除。G後に残さない（T3票 §4）。
- 配置値・新分岐はA〜Fで完成済み。Gの配置実装は既定を1にし、旧分岐とマクロを消すことだけ。値や未結線の新分岐をここで追加しない。
- 配置世代を更新しkernel/loader/shell/SDK/CRT/shlib/userland/対kdebugを一組で再生成。C/ASM/linker/boot/R1の共通生成規則からも切替マクロを撤去する。
- 両配置に追随済みの製品試験を新配置だけの期待へ整理する。T3b0の模型 `layout_boot_fixture.h` の旧地図入力は参照として残してよい。

## この段の検査
実装時のみ、下記の直接試験を `MUTATE=0`、`make kernel`、`python3 tools/check_select.py --lint`。
変異は最後の Opus レビュー後の判断で実行し、一度に一欠陥、正常対照PASSと対応観測FAILを対にする。
- 撤去前に両配置を確認する。新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へ組み、リンクまで通す（配備しない。Aで上書き変数をC/生成規則へ結線し、中間成果物も分離）。
- 撤去前に、製品ソースを取り込むホスト試験（`memory_boot_host.c`・`memmap_boot_host.c` など、その段の直接試験）を `-DMEM_LAYOUT_V5=0` と `=1` の両方で作って回す。模型だけで代替しない。撤去後はマクロ指定なしで新配置の別出力リンクと製品ホスト試験を再実行する。
- `tools/tests/test_dma_pool.py`、`test_pcm_cs4231.py`、`test_dma8237.py`、`test_mem_stat.py`、`test_memmap_gen.py`。
- 同 `test_paging_bounds.py`、`test_vk32_crc.py`、`test_vmkernel_layout.py`。Gで触った直接試験だけを実行する。
- 足す・直す観測: MMIO PT=0x3EB000、旧池境界前提の撤去、64KiB整列池の全域/端点、展開窓とBSS非交差。
- `test_memory_boot.py`・`test_memmap_boot.py`（旧NP→present）、`test_exec_budget.py`・`test_exec_r1.py`・`test_nano_adapter.py`（シェル新帯）。
- 世代は `test_kapi_layout.py`・`test_kapi_out.py`・`test_deploy_set.py`、generations-manifest整合は `test_memmap_gen.py`。
- A〜Fの新配置模型を実ELF/manifestと照合し、最小RAM判定・旧予約残留なし・二重供給なしを確認する。
- 旧分岐残留検査: 製品のC・ASM・ld・生成物で `MEM_LAYOUT_V5` と旧番地を表す名前の残留が0件であること（T3b0 `layout_boot_fixture.h` の旧地図入力だけは参照として除外）。
- 代表変異: 旧pool下限、metadata重複、SLACKのNP化、旧世代入口許可を一つずつ検出。

## 段の終わりの条件
旧分岐・`MEM_LAYOUT_V5` の残留なし。通常/R1のlink ASSERT・manifest・予算一致、圧縮像≤508KiB。同世代一式の配備予行と旧世代入口前拒否。
NP21/W 64MB起動、地図監査0fail、PG直後/AS生成後のSLACK/shell/metadata恒等sup/RW/WB・guard NP。
SLACK全頁割当/返却で固定頁混入0、SQLite DB/FEP正常、BSS/stack guard成立、PEGC通常経路・LGY-98 L0〜L3回帰。
KDEBUG=1で対の部品がRUNNINGかつ実試験実行。配備/ゲスト受入はPM側、未実施ならT3ab受入は未完了。

## 次の段へ渡すもの・未確認
Opusへ差分と直接試験結果を渡し、レビュー・検査・全体検査の要否判断を受ける。
T3b1へ監査/予算/世代、bRへ実機FFF窓・PCM/82557、c1へ2heap詳細受入を渡す。b2/b3・F-9も未閉鎖。
失敗時は旧新を混ぜず直前の一式へ戻す。[V1]・[D1]〜[D3]の配備手順を守る。
