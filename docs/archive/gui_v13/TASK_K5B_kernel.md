# K5b-K: アプリ 4 本の同時実行 — カーネル側の実装 (KAPI v44)

> 発行: PM (2026-09-11) / 状態: **受入完了 (2026-09-12)**
> レーン: K (C89、カーネル背骨) / 前提: [K5 決裁](TASK_K5_multiapp.md#決裁-2026-09-11ユーザーレビュアー枯渇のため-pm-の材料提示に基づく) 済み、設計は [K5a §設計 D0〜D11](TASK_K5_multiapp.md)
> 契約: T2, T2a, T3, T4, U8 / 版数: **v44** ([KAPI_SPEC §3-2](../../KAPI_SPEC.md) に予約済み)
> 排他: `kernel/**` `exec/**` `kapi/**` `fs/fd_redirect.c` (所有者 ID) `include/memmap.h` `sdk/kapi.json` + 生成物、`tools/tests/` の新規。
> **触らない**: `userland/gshell/**` (W レーン、K5b-W)、`drivers/**` の I/O 手順、`v86`、`shell.bin`。

## ゴール

K5a の設計 (D0〜D11) をカーネルに実装し、**gshell (W レーン) が K5b-W で使える KAPI と機構**を
揃える。同時に生きる GUI アプリは最大 4 本、動くのは常に 1 本、切替は `OP_WAIT` で park された
フレームからの resume だけ。プリエンプションは足さない (決裁 D9-1/7)。

## 作業 (設計の項番と対応)

1. **物理ページとアドレス** (D1、I1〜I14): アプリごとに `pgalloc_alloc_n()` で 3 領域 (本体+sbrk /
   exec_heap / ユーザスタック) を取り、固定仮想 0x500000〜へ写す。`paging_addrspace_map_user_range_phys`
   (P1) と `paging_addrspace_clear_app_band` (P2) を足し、**I6 (アプリ PT が master の identity PTE で
   初期化される) を必ず落とす**。`EXEC_DYN_RESERVE` の穴は役目を終えるので撤去。identity 前提の
   14 か所を全部直し、仮想レイアウト (`app.ld` / `memmap.h` の RING3_*) は動かさない。
2. **AppSlot と切替** (D2、決裁 (b)): `exec_ctx_stack` のスタックに加えて **アプリ ID 2〜5 の表** (AppSlot)
   を持ち、`OP_WAIT` の syscall フレーム (pushad + iret 分) と CR3、exec_heap 状態、guard を保存する。
   単一カーネルスタックのまま。`ring3_resume(frame)` (P4) は `int80_stub` 末尾を切り出す。
   IF の扱いは `int80_stub` の注記 (`sti` で入り `cli` で出る) を崩さない。
3. **所有者 ID** (D3、決裁 D9-2): `cur_res_owner` を「ネスト段」から「ID (1 = シェル、2〜5 = アプリ)」へ。
   GUI と CUI で 1 池。`fd_redirect_reset_owned` / `vfs_close_owned` / `pipe_free_owned` / `gui_owner_exit` は
   ID で閉じることを確認し、`shm_free_owned` (P3) を足し、`db_cleanup_all` → `db_cleanup_owned(id)` (P5)。
   CUI (`shell.bin` の入れ子 `exec_run`) では段 = ID になる互換を保つ。`MAX_EXEC_NEST=4` では ID 5 が
   入らないので表の大きさを見直す。
4. **KAPI v44** (D8、決裁 D9-5/6/8): `exec_start(cmdline) → app_id / 0 / 負`、`exec_resume(app_id, wait_ret)`、
   `exec_park()` (戻らない。`OP_WAIT` 以外の op から呼ばれたら `OS32_ERR_INVAL` を返して C3 を増やす)、
   `exec_kill(app_id)`、任意で `exec_app_state(app_id)`。**owner 1 からのみ** (`gui_register` と同じ判定)。
   `exec_run` は CPL=3 から呼べるまま (ID は池から。5 本目は `ERR_FULL`)。追加は**末尾追記のみ**
   [ABI2]、`sdk/kapi.json` を編集して再生成、版数 42 → 44 は **スキル `os32-kapi-add` の手順で** [ABI1]〜[ABI3]
   (v43 はネットワークに予約済みで飛ばす)。`make clean` → `make all`、`make external` も回す。
5. **印とカウンタ** (D0 / C1〜C6、受入 G7): `AppSlot.parked_from_wait` (park 時に `g_cur_gui_op == GUI_OP_WAIT`
   のときだけ立てる)、`exec_resume` は印のあるフレームだけを起こし、無ければ `ring3_resume_bad_frame_count`
   を増やして `OS32_ERR_INVAL`。`ring3_switch_count` (resume 成功)、`ring3_transition_count` (start の iret、
   終了 / fault / kill の master 復帰)、`ring3_park_reject_count`。すべて `fault_kill_count` と同じ
   カーネルシンボル (KAPI にしない)。
6. **終了・fault・CTRL+STOP・起動失敗** (D4): それぞれで**その ID だけ**畳んで WM (owner 1 の top-level) へ
   戻る。`ring3_fault_kill` の「master CR3 復帰 → AS 破棄 → longjmp」を ID 単位に。CTRL+STOP は走っている
   アプリ宛 (`exec.c:265`) のまま。止まっているアプリは `exec_kill` で畳む。
7. **音の排他** (決裁 D9-4、受入 G10): 音は**フォーカスに追従して排他**。カーネルは「音の所有者」を 1 つ持ち、
   W レーンがフォーカス切替時に呼ぶ口 `snd_focus(app_id)` (KAPI か内部関数かは設計で決めて PM に報告) で、
   それまでの所有者の音を止めて状態 (BGM の MML と再生位置、persist、master) を AppSlot に退避し、新しい
   所有者に退避済みの状態があれば復元する。同時には鳴らさない。終了時はその ID の状態だけ捨てる。
   `snd_cleanup()` (`exec.c:545`) の呼び先を ID 単位に。`kernel/snd_engine.h` の既存 API を読んで、
   退避できる最小の状態を決める (YM2203 のレジスタ影像を持つか、MML の再生位置で足りるか)。
8. **メモリ勘定** (D5、決裁 D9-3): `heap_size = 0` の既定は変えない。入らなければ `EXEC_ERR_NOMEM` /
   `OS32_ERR_FULL` で拒否し、既存アプリには触らない (スワップしない)。8MB で GUI アプリが 1 本立つ
   ことは受入 G6 で確かめる (実機は PM/テスター)。
   - **sbrk 物理は二段構え** (決裁 2026-09-11)。`heap_size = 0` の CPL=3 プログラムは、3 領域を
     従来式 (sbrk 上端 = `guard_a`) で見積もって `pgalloc` の空きに収まるなら段 1 = 従来どおり張り、
     収まらなければ段 2 = 最低分 `MEM_EXEC_SBRK_MIN` (256KB) に落とす。段 2 でも入らなければ従来どおり拒否。
   - `heap_size` を明示したプログラムの挙動は変えない (最低分のまま)。CUI (`exec_run`) と GUI
     (`exec_start`) は `exec_launch()` の同じ場所を通るので規則は 1 つ。判定は `exec_sbrk_pick_tier()`。
   - どちらの段で走ったかは `exec_sbrk_tier_last` / `exec_sbrk_tier_count[2]` (KAPI にせず
     `fault_kill_count` と同じカーネルシンボル)。試験は `tools/tests/test_sbrk_tier.py`、記録は
     `tools/tests/k5b_kernel_tdd.md` 回 4。

## ホスト試験 (実装と同じコミットで)

- `tools/tests/test_app_band_pde.py` / `paging_app_band_selftest()` に P1/P2 の検査 (仮想≠物理の写像、
  アプリ帯の PTE が空から始まること = I6)。
- 所有者 ID の回収: `tools/tests/test_vfs_fd_sqlite.py` 系の作法で、ID 2 と 3 が開いた FD / redirect /
  pipe / shm / db を ID 2 の終了で **2 の分だけ**回収すること。
- AppSlot と印: `tools/tests/multiapp_model_host.c` の状態機械を**実物の AppSlot 管理コード**に差し替えて
  同じ 84 検査が通ること (モデルは設計、これは実装の検査)。印なし resume の拒否 (C6) を負例で。
- 新挙動ごとに RED→GREEN を `tools/tests/k5b_kernel_tdd.md` に記録。`make check` への登録は PM。

## 完了条件

- `make clean && make all && make external && make check` が EXIT=0 (`CROSS_DIR=~/opt/cross` が要る場合あり)。
- `sdk/kapi.json` の version が 44、生成物と `KAPI_VERSION` が一致 (`check-kapi-version`)。
- 上記ホスト試験がすべて GREEN で、RED の記録がある。
- 変更ファイル一覧、KAPI 追加の最終署名、`snd_focus` の形 (KAPI か内部か)、既知の未確認を ROLES §5 の書式で報告。
- **配備・コミット・push・エミュレータ・ローカル AI・`*.ini` は禁止。** ゲスト受入 (G1〜G10) は PM とテスターが行う。

## 受入 (ゲスト、PM/テスター)

[K5 §K5b の G1〜G10](TASK_K5_multiapp.md#段階-k5b--実装-k5a-凍結後に発注)。K レーン単体では G7 (カウンタと印)、
G8 (CUI 回帰: `shell.bin` の入れ子 exec、v86、regress 6 本) を先に見る。G1〜G6 / G9 / G10 は K5b-W と合わせて。

## 実機初回起動 (2026-09-11) — 差し戻し

`38266a7` を 15MB 構成の NP21/W にテスターが配備。kselftest 44/0 は通るが、常駐シェルの
読み込みで `FATAL: shell.bin load failed` (`kernel/kernel.c:582`)、直後に `14000` (出所未特定)、
EIP `kernel_main+0xd4c` で停止。rshell が上がらないため回帰・v86・GUI 台本は**未実施**。
ゲストは配備前のバックアップへ戻した。見当: is_shell 経路が per-app 物理化 / 新しい
`EXEC_ERR_NOMEM` 判定に巻き込まれている (`git show 38266a7 -- exec/exec.c` の 1045/1090 行付近)。
コーダーへ差し戻し (ホスト試験に「シェル起動は per-app 経路を通らない」を追加させる)。
`ring3_resume` の実機検証は、この修正の再配備後が最初になる。

## 起動修正 (2026-09-11、`8eb7eed`) と、そこで見つかった申し送り

真因は `fs/ext2_file.c` の端数ブロック溢れ (§4-32)。is_shell 経路の物理配置は K5b-K でも元のままだった。
コーダーが差分を読んで列挙した「K5b-K 前と変わってはいけない経路」の残件 (**未修正・要判断**):

| # | 事項 | 重さ | 決裁 |
|---|---|---|---|
| A1 | **`--cpl0` の子 × 生きている CPL=3 アプリ**: `exec_cpl0_claim()` は帯 `[0x500000, mem_end)` を identity で丸ごと `pgalloc_mark_used`、`release` で丸ごと free。K5b-K 以後は CPL=3 アプリの per-app 物理も同じ pgalloc から取るので、GUI アプリが park 中に `--cpl0` の子を起動すると (a) 子がアプリの物理を上書き、(b) 解放時に生きているアプリのページまで free。gshell 配下でしか到達しない | **P1 相当** (受入前に決着) | ユーザー → **実装済み** (コミット SHA は PM が入れる) |
| A2 | 入れ子 `exec_run` の上限が `MAX_EXEC_NEST`(=4) から ID の池 (2〜5) に変わった。GUI 4 本が生きていると CUI の入れ子は `OS32_ERR_FULL` (設計どおり)。`exec.h:28` の `MAX_EXEC_NEST` は死に定数 (**掃除済み** 2026-09-11) | 低 (掃除) | PM |
| A3 | `EXEC_DYN_RESERVE` の穴は CPL=0 の子のためだけに残り、V86 バッキングと per-app 物理が同じ pgalloc を食い合う。sbrk 段 1 (収まるなら張る) と組むと、アプリ起動直後の `v86` が以前は通った所で落ちうる | 中 (G8 の v86 で観測) | PM/テスター |
| A4 | `resolved` / `hdrbuf` が関数 static で全段共有。ヘッダ先読みと本体読み込みの間に他の exec が挟まると親の起動を静かに壊す (現状はその窓に何も入らない) | 低 (注記) | PM |

**決裁 (2026-09-11、A1)**: **CPL=3 アプリが 1 本でも生きていたら `--cpl0` の子は拒否** (`exec_run` が
`OS32_ERR_FULL` か専用エラーで断る)。`--cpl0` は特権が要る例外用途で、GUI のアプリを全部閉じてから
使えば足りる。実装は小さく、機構は増やさない。→ 起動修正の再配備・G8 の結果を見てからコーダーへ
(ホスト試験: 生存アプリあり → 拒否、なし → 従来どおり、RED→GREEN)。

**実装 (2026-09-11、実装コミット `f850bb6`)**: `appslot_cpl0_admit(is_shell)` を
`exec/appslot.{c,h}` に足し、`exec_launch` の `want_ring3 == 0` の枝 (シェルを除く) で
`exec_cpl0_claim()` **より前**に呼んで、`appslot_live() > 0` なら `OS32_ERR_FULL` を返す。
claim も alloc も 1 つも行わないので AppSlot・pgalloc・資源の所有者はどれも動かない。
ホスト試験は `tools/tests/multiapp_impl_host.c` のケース 19 (26 検査)、記録は
`tools/tests/k5b_kernel_tdd.md` 回 6。**ゲスト未検証** (テスターの再配備待ち)。

## 実機受入 (2026-09-11、`b86abf8` = K5b-K + 起動修正 + sbrk 二段構え、15MB 構成、テスター実行 / PM 判定)

| 項目 | obs | 判定 |
|---|---|---|
| 全体ゲート | `make clean` / `all` / `external` / `check` すべて `exit=0` | 合格 |
| 配備 | `os32-cycle deploy` exit=0、`vmkernel.lz4` 453,523 B 一致、`ver` = **API v44** Build Sep 11 07:43 | 合格 (新カーネル起動) |
| G8 回帰 6 本 | kselftest 44/0、klibc 49/0、alloc_demo 全通過、ring3_fault → kill 後 `ver` 生存、pipe 29/4、screenshot | 合格 |
| G8 v86 | `v86 -t` → `result : OK` | 合格 |
| GUI アプリ 1 本 (台本 36 手) | `os32gui` → Run `gui_bench` → 窓が出る (スクショ) → クリック後 `CLICK n = 2` → ESC → CUI 復帰 | 合格 (**per-app 物理ページで CPL=3 アプリが動く**) |
| カウンタ | `exec_sbrk_tier_last=1` (段 1)、`ring3_transition_count=16`、`ring3_switch_count=0` (W 未実装)、`park_reject=0`、`resume_bad_frame=0`、`appslot_reclaim_count=8`、`fault_kill_count=1` | 整合 |

未実施: G1〜G7 / G9 / G10 (W レーン待ち)、G6 の 8MB 構成、A1 (`--cpl0` 拒否) の実機。

## K7 (8MB の shlib data NOMEM、2026-09-11)

原因: `exec_ring3_pages()` が 3 領域 + PD + アプリ PT しか数えず、その後に同じ pgalloc から取る
**付随ページ** (共有ライブラリの `.data/.bss` 複製 4 枚) を勘定に入れていなかった。8MB の空き 768 に
段 1 の枚数が **ちょうど 768** で収まり、段 1 を採ると空きが 0 → `shlib_addrspace_attach()` が失敗
(`exec_sbrk_tier_last = 1`)。修正: `shlib_data_pages()` を足し、`exec_ring3_extra_pages()` を
`exec_ring3_pages()` に加算 — 段 1 / 段 2 の判定と `appslot_start_admit()` の両方に効く (8MB は段 2 へ倒れる)。
