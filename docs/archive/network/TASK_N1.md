# TASK_N1 — Host Services 基盤: ワイヤ v2、`net/link.c` の非ブロッキング化、KAPI v51、Agent v2

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

コア受入 (ユーザー決裁 2026-09-14「b」)。F6 (64KB 超のストリーム) は N3 の wget で実サービスとして再確認する保留。設計の正典は [TASK_N0.md](TASK_N0.md) 第 5 版 (§1a ABI、§1b ワイヤ v2、§2 状態機械、§3 TDD と受入)。本票は N0 §3 を実装単位に落としたもので、契約は N0 が勝つ。

## 0. 範囲 (HOST_SERVICES_PLAN §7 の N1)

| 段 | 成果物 | 試験 |
|---|---|---|
| 1 | `tools/host_agent.py` v2: 20B ヘッダ (LE 直列化、op ごとの payload 長検査)、3 way HELLO (候補 1 件、Agent nonce、`req_sess` / `req_epoch` の写し、`sess.txt` 永続採番と枯渇停止、`agent` 世代)、rid 台帳 (ACTIVE ≤ 2 / RELEASED / HOLE、`high_water`、規則 (1)〜(5)、epoch 切替で HOLE → RELEASED)、STATUS への再提示 / PROCESSING / TOMBSTONE、制御結果は RESPONSE flags bit0、RELEASE と bit0 ACK、WINDOW 待ちの配送開始、WDATA の (sess, rid, seq) 重複排除と累積 ACK、宣言長で本文完了 → RESPONSE、`GET` / `TIME` / `PING` / `ECHO <len>`。トランスポートは既存の NP2NETSOCK (4B BE 長 + フレーム) に加えて **UNIX ソケット** (`--unix <path>`、ホスト TDD 用) | `tools/tests/test_host_agent.py`: Python の贋 OS32 で N0 §3 の Agent 側項目 (台帳の (1)〜(5)、HOLE、墓標 8 件境界、3 way の各段の消失 / 遅延 / 旧 SYN、枯渇停止、flags 分離、WDATA 重複 / 欠落、RELEASE 先着) |
| 2 | `net/link.c` / `net/link.h`: v2 ヘッダ、ハンドル 2 本 + リング 1 本、N0 §2c の状態機械、`link_tick` (100Hz、`ne2k_timer_tick` の後、`kernel/isr_handlers.c`)、反射モードでは起動しない、制御 / 通常キューの交互送信 + RELEASE 専用スロット、NIC 受理 tick からの RTO、`T_probe` の STATUS、3 way HELLO と再同期、cli 区間 + `gen` 再確認、`host_read` の成功確定点。既存の同期自己試験 (`link_hello` / `link_request` / `link_l1_bulk` / `link_l2_stream` / `link_service_get`、`drivers/lgy98.c:132` の呼び手) は**非同期 API + hlt 待ちの上に書き直す** | 段 4 のハーネス |
| 3 | KAPI v51: `sdk/kapi.json` に 5 本を末尾追記 (`host_open` / `host_status` / `host_read` / `host_write` / `host_close`、スキル `os32-kapi-add` の手順、生成物は生成器で)、`kapi/kapi_host.c` に `__cdecl` ラッパー (ポインタ検査は `ring3_user_range_ok`、出力ポインタは全部検証してから書く)、`exec_reclaim_owned` に `host_owner_exit` (`launch_owner_exit` / `con_sink_owner_exit` と同じ位置)、`docs/KAPI_SPEC.md` §3-2 の v51 行を「実装済み」に | 段 4 のハーネスがラッパー経由 + ディスパッチャの早期検査 (`kapi_argptr`) 経由の両方で叩く |
| 4 | `tools/tests/net_link_host.c`: `net/link.c` を `#include`、NIC (RX キュー、TX 受理可否 = 1 tick 1 フレームの条件、tick) と `cli` / `sti` を贋物に、**実 Agent** (段 1 を UNIX ソケットでサブプロセス起動)。N0 §3 の全項目 (往復 2 の R1〜R10、往復 3 の B1〜B8、往復 4 の R1〜R4、その他) を**個別のケース名**で | `build/sdk.mk` に `check-net-link-host` を足し `check` の列に登録 |
| 5 | `userland/tests/host_test.c` (N0 §3 のゲスト項目: `GET /pattern/65536` の AGAIN ループと内容一致、`GET /notfound` = 404、`TIME`、`ECHO 5` + write、Agent 再起動 → STALE → close → open)、`build/programs.mk` / `build/app.conf` (api 51) / `userland/deploy.yaml` | ゲスト受入は PM / テスター (コーダーは触らない) |
| 7 | **移植性調査** `docs/tasks/portability/SURVEY_N1.md` (ユーザー指示 2026-09-14): ワイヤ v2 と `link.c` / KAPI v51 の実装で触れた・見つけた **CPU アーキテクチャ依存** (他アーキテクチャ、例えば ARM への移行時の注意点) を**詳細に**調査して記す。観点: (a) 直列化とアライメント (ワイヤ / ディスク上の構造を LE アクセサで読んでいるか、構造体キャスト・非アラインアクセスの残存箇所を `net/` `drivers/ne2000.c` `fs/` `kapi/` で grep して列挙)、(b) 割込み制御 (`cli` / `sti` / `hlt` の直書きの箇所と抽象化の有無)、(c) ポート I/O (`in` / `out` の箇所 = ARM では MMIO)、(d) メモリ順序とキャッシュ (NIC リング・DMA バッファ・共有バッファで x86 の強い順序に依存している箇所、[HW2] の 64KB 境界)、(e) システムコールの引数渡し (`int 0x80` + ユーザースタックからの引数コピー、`kapi_argptr` の早期検査 = レジスタ渡しの ISA でどう変わるか)、(f) 物理番地の前提 (`memmap.h` 以外に絶対番地を書いた箇所)、(g) タイマと割込みコントローラ (100Hz `link_tick` の前提)、(h) エンディアンと型幅 (`u16` / `u32` の仮定、`int` の幅)。各項目に「N1 で直した」「残っている (場所と理由)」「移植時にやること」を書く。調査は**コードを grep して具体的な行を挙げる** (推測で書かない) | — |
| 6 | `tools/tests/n1_tdd.md` (RED → GREEN の記録、ケース名と N0 の指摘番号の対応表)、`docs/tasks/network/LINK_PLAN.md` §5-1 の進捗、本票 §3 の自己申告 | — |

既存の `check-net-l0`〜`l3` (`tools/net_l*_test.py`、ゲスト観測) は v2 で回帰させる (PM がゲストで実行)。`check-net-m2` の反射試験は回帰対象。

## 1. 規約

[C1] C89、[C2] kstr*、[C3] `__cdecl`、[C4] 定数は 3 層 (ワイヤ定数は `net/link.h`、KAPI 側の共有定数は `sdk/include/os32/os32_kapi_shared.h`)。[ABI1〜3] `kapi.json` が正典、末尾追記、版 50 → 51 (`make clean` → `make all` は PM / テスターが行う)。**ワイヤ上の構造は LE アクセサで読み書き** (構造体キャストと非アラインアクセスをしない — 移植性の習慣)。コーダーは commit / deploy / エミュレータ / ini / `.env` / `make all` / `make check` を触らない (自分のホスト試験ターゲットと Python 試験の直接実行は可)。

## 2. 受入 (PM)

`make check` (ホスト TDD 全部)、`kernel-lgy98-link` を配備して `host_agent.py` v2 を WSL2 で起動、`host_test` の全項目、`check-net-l0`〜`l3` と `m2` の回帰、GUI 配下で `gui_busy` と同時に `host_test`。Codex 実装レビュー (網羅指示) の後に main へ。

## 3. 記録

### 3-1. コーダーの自己申告 (2026-09-14、worktree `agent-ad14aebaf0154a34d`、基点 `af48990`)

段 1〜7 を実装した。TDD の記録 (RED → GREEN、ケース名 ↔ TASK_N0 §3 の指摘番号の対応表、
決めたこと、既存 `check-net-l0`〜`l3` が読むシンボルの v2 での意味) は
[`tools/tests/n1_tdd.md`](../../../tools/tests/n1_tdd.md)、移植性調査 (段 7) は
[`docs/tasks/portability/SURVEY_N1.md`](../../tasks/portability/SURVEY_N1.md)。

**試験で確認したこと** (全部コーダーの手元で実行、出力は n1_tdd.md §5):

- `make check-host-agent` (`python3 -B tools/tests/test_host_agent.py`) — **25/25 PASS**。
  実 `host_agent.py` に、フレームを直接組む贋 OS32 をぶつけた。往復 2 の R1〜R5 / R7、
  往復 3 の B1〜B7、往復 4 の R1〜R3 と HELLO 各段の消失、payload 長検査、
  WDATA 重複排除、WINDOW の配送許可、TIME の標準形。
- `make check-net-link-host` (`python3 -B tools/tests/test_net_link.py --target`) —
  **29/29 PASS** + ホスト GNU89 `-Werror` + `i386-elf-gcc -Werror` の両コンパイル。
  実 `net/link.c` + 実 `kapi/kapi_host.c` を `#include` し、NIC (RX キュー・
  **1 tick 1 フレーム**の TX 受理・tick)、`cli`/`sti` (IF=0 中のタイマ延期と復元直後の
  tick、リング更新 / TX 構築の途中の贋 IRQ5)、ディスパッチャの早期検査
  (`kapi_argptr` は**生成物から読んで** `-D` で渡す) を贋物にし、対向は
  **実 Agent をサブプロセス**か台本。往復 2 の R1 / R3 / R4 / R6 / R8 / R9 / R10、
  往復 3 の B1 / B6 / B7 / B8 と TOMBSTONE / NO_SLOT、往復 4 の R4 と HELLO 各段、
  CPL=0 経路、owner 回収、`host_read` の「最後の 1 回は正の長さ・次が 0」、
  `host_write` の宣言長契約、リングの排他、EOF だけでは完了にしない、Go-Back-N。
- ソース本文の番人 2 件: `exec_reclaim_owned` の `host_owner_exit` の位置、
  `timer_handler` が `ne2k_timer_tick()` の直後に `link_tick()` を呼び、
  KAPI / `net/link.c` の中からは呼ばず、反射モードで `link_init` を止めていること。
- `python3 tools/check_kapi_version.py` — v51 で 4 か所一致、`KAPI_SPEC.md` の関数表も一致。
- 個別コンパイル (`i386-elf-gcc -Wall -Wextra -Werror`): `net/link.c`、`kapi/kapi_host.c`、
  `exec/exec.c`、`kernel/isr_handlers.c`、`drivers/lgy98.c`、`kapi/kapi_generated.c`。
  `userland/tests/host_test.c` はユーザランドのフラグで警告 0。

**確認していないこと** ([V4] — 実行していないので合否を主張しない):

- `make all` / `make clean` / `make check` / `make external` を**一度も走らせていない**
  (票の禁止事項)。KAPI 構造体が変わるので [ABI3] の clean rebuild は PM の担当。
- 配備 (`make deploy*`) とエミュレータ (NP21/W、`/api/*`、MCP) は**一切触っていない**。
- `userland/tests/host_test.c` は**コンパイルしか確かめていない**。実機で 1 度も
  走らせていないので、ゲスト項目 (`GET /pattern/65536` の AGAIN ループ、404、TIME、
  `ECHO 5` + write、Agent 再起動 → STALE → close → open) は**未検証**。
  Agent 再起動のケースは運用者が Agent を落とす必要があるので `host_test stale` で分けた。
- `check-net-l0`〜`l3` と `check-net-m2` は**実行していない** (ゲスト観測)。
  読むシンボルの意味は保つよう作ったが、実測での回帰は取っていない。
  v1 で合格していた L0〜L3 の実績は、ワイヤが変わったので**取り直しが要る**。
- 実 NIC / 実エミュレータ上でのタイミング (100Hz で RTO 200ms・T_probe 1 秒が
  足りるか、1 tick 1 フレームで L1 の 200 フレームが時間内に流れるか) は**未測定**。
  ホスト試験の贋 NIC は「1 tick 1 フレーム」を再現しているが、実機の遅延は入っていない。
- GUI 配下 (`gui_busy` と同時) は未検証。
- `host_agent.py` の実 HTTP 経路 (`GET http://...`) はホスト試験では `--offline` で
  止めているので、**実際にネットワークへ出る経路は 1 度も動かしていない**。

**PM が着地時に注意すべき共有ファイル**: `sdk/kapi.json` (末尾 5 本 + version 51 +
includes に `kapi_host.h`)、`build/sdk.mk` (`check-host-agent` / `check-net-link-host` を
`check` の列と `.PHONY` に追加)、`build/kernel.mk` (`kapi/kapi_host.c` を KERNEL_SRC へ)、
`build/app.conf` / `userland/deploy.yaml` (`host_test`)、`docs/KAPI_SPEC.md` (題名 v51・
§3-2 の v51 行・§4 の関数表 5 行 + data_fields を 0x35C / 0x360 へ)、`README.md` /
`docs/INDEX.md` (版数)、`sdk/include/os32/os32_kapi_shared.h` (`KAPI_VERSION`)、
`exec/exec.c` (`exec_reclaim_owned` の 1 行)、`kernel/isr_handlers.c` (`timer_handler`)。

### PM 記録 (2026-09-14)

- 着地 `995bb19` (基点 `af48990`、削除ファイル無し、conflict 無し) + `f5dca53` (`INC_KAPI` に `-Inet`: `kapi/kapi_host.c` の `link.h` が見つからず最初の `make all` が落ちた)。
- テスター: `make clean` → `make all` exit 0 (71s) → `make check` exit 0 (53s、`check-host-agent` / `check-net-link-host` を含む) → `make external` exit 0 (7s)。合否は `tools/emu_agent/logs/n1-build2/steps.jsonl` の obs で判定 (モデルの最終出力は unparseable だったが obs は 3 本とも exit=0)。
- 未実施: ゲスト受入 (§2)、Codex / 設計者の実装レビュー。v1.4 の体制 (ROLES §0) により、ゲスト受入は実装 PM の最初の仕事。

### PM ゲスト受入 (2026-09-14、kernel-lgy98-link を NHD 配備、Agent v2 を WSL2 で待受)

| 項目 | 結果 | 根拠 |
|---|---|---|
| 配備 | OK (`/boot/vmkernel.lz4` 475,863 B 一致) | `os32-cycle deploy`、session `n1-deploy` |
| kselftest | **86 / 1** (従来 87 / 0) | 落ちた項目 = `db_v50_selftest` bit 0 (`KAPI_SLOT_COUNT != 208`、`db_error_code` が末尾) — v51 の追記で崩れる検査。**修正対象 F1** |
| 起動時自己試験 (Agent 側) | L0 10 往復、L1 102,400 B、L2 131,072 B、L3 65,536 B / 404 / 実 HTTP 559 B / TIME 19 B すべて業務 RESPONSE まで通り、DATA も最後まで ACK (pcap: rid 11 ack=200、rid 12 ack=256、rid 13 ack=128) | `scratchpad/n1_link.pcap` (Agent 受信側)、`host_agent.log` |
| `check-net-l3` | OK (0 failures) | session `n1-lnet2` |
| `check-net-l0` | FAIL: `rt_ok=16` (期待 10) | `link_rt_ok` が業務 RESPONSE の総数 (L1〜L3 を含む) に変わった。**F2**: L0 専用の計数を足すか試験の期待を改める |
| `check-net-l1` / `l2` | FAIL: `l1_recv=131/200`、`l1_bytes=66114`、`l2_read=66114/131072` — **配送は完了しているのに計数が途中で止まる** (L1 と L2 が同じ 66114) | pcap の ACK 進行と矛盾 → **F3**: 計数の更新経路の欠陥 (配送ではない) |
| `link_retransmits=32` | pcap 上は REQUEST 16 / RELEASE 16 で再送ゼロ | **F4**: open / close 直後、NIC が初回送信を受理する前に RTO 期限を数えて `retries` / `retransmits` を増やしている疑い (`net/link.c` の `since(last_tx_tick)` / `since(rel_tick)` が未送信でも走る)。契約「RTO は NIC 受理 tick から」に反し再送予算が 1 減る |
| WINDOW | 1 tick に 2 通 (同じ ack で credit 14 → 20) が常態 | **F5** (non-blocker): 制御枠「rid ごと 1 周回 1 本」に反する。credit の揺れ 14 / 16 / 20 の由来も記録する |
| `hsync` → `host_test` (CPL=3、KAPI v51) | **PASS 26 / 26** (GET 65536 の AGAIN ループ、404、TIME、ECHO + write、不正引数) | `/api/cmd` の出力 |
| `host_test stale` (Agent 再起動 → STALE → close → open) | **未実施**: NP2NETSOCK は NP21/W が connect する向きで、Agent を落とすと再接続の有無が不明 (実施には NP21/W 再起動を伴う)。ホスト TDD の R6 ケースで代替 | — |
| `stat` (S3 の観測手段) | `/` = `dev=1(hd0) ino=2`、`settings.db` ino=273 / `.bak` ino=272 (別 inode)、`/nope` はエラー行 | `/api/cmd` の出力 |
| GUI 配下で `gui_busy` と同時の `host_test` | 未実施 (F1〜F4 の修正後に) | — |

F1〜F5 は Codex の実装レビュー所見と合わせて N1 のコーダーへ戻す (修正票 = 本票 §4)。

## 4. 修正 (N1-fix、実装レビュー + ゲスト受入の結果)

レビュアー: Fable 5.1 サブエージェント (Codex が 2 回不安定 → ROLES §5 の代替、2026-09-14)。判定 Request changes、blocker 4 件。commit 範囲 `af48990..f5dca53` 対象。

### blocker
- **F1** (kselftest 86/1): `kapi/kapi_db.c:1149` `if (KAPI_SLOT_COUNT != 208)` と `:1151` `if (KAPI_SLOT_DB_ERROR_CODE != KAPI_SLOT_COUNT - 1)` が v51 で崩れる (`KAPI_SLOT_COUNT` = 213、`DB_ERROR_CODE` = 207)。→ `!= 213`、`DB_ERROR_CODE != 207` (または `KAPI_SLOT_HOST_CLOSE == KAPI_SLOT_COUNT-1`) に。コメントの「201..207 / 末尾」も更新。ネットワーク無関係に全ビルドで踏む。
- **F2** (`check-net-l0` rt_ok=16): `net/link.c:408` `link_rt_ok++` が L0〜L3 を通じて累積し `net_l0_test.py:71` の `== 10` を恒常 FAIL。→ L0 専用カウンタを別に持つか、各自己試験入口で `link_rt_ok`/`link_rt_fail` を打ち直す。`n1_tdd.md §4` の「v1 の往復数と同義」は誤り。
- **F4** (`link_retransmits=32`、再送は pcap 上ゼロ): `net/link.c:583`(RELEASE)/`:591`(REQUEST)/`:597`(WDATA) の RTO 判定に、ハンドシェイクにある `!due` ガードが無い。さらに `host_open:897`/`host_write:1009`/`link_free_handle:1022` が `last_tx_tick`/`rel_tick = tick_count - LINK_RTO_TICKS` で期限を前倒し。timers→tx の順なので open/close 直後の最初の tick で未送信フレームを再送計上 (open 16 + close 16 = 32、厳密一致)。契約「RTO は NIC 受理 tick から」に反し retry 予算を 1 消費。→ 3 か所に `!e->req_due`/`!e->w_due`/`!e->rel_due` を追加し、前倒しの `- LINK_RTO_TICKS` を外す (due=1 が即送信を担保)。
- **N2** (ホスト TDD がカウンタ契約を検査せず F1/F2/F4 を素通り): (a) 無ドロップ往復で `link_retransmits==0` を assert、(b) L0→L1 連続で `link_rt_ok` の区間性を assert、(c) `db_v50_selftest()` を `make check` の対象に含める (または `check-kapi-version` に slot 数の整合を追加)。

### 要実機再測 (F4 修正後)
- **F3** (`l1_recv=131/200`、`l2_read=66114`): タイムアウトではなく**転送中の resync による中断**が症状の正体 (`link_wait_read_all` が STALE で打ち切る)。火種候補は F4 の retry 予算侵食と DATA overflow の回復遅延。ホスト TDD は贋 NIC が決定的なので再現しない。→ F4 修正後にゲストで `link_resyncs`/`link_rt_fail`/`link_tombstones`/`link_l2_overflow` を kernel.map 番地で読み、(a) resync か (b) overflow 起因かを切り分けて L1/L2 を再測。

### non-blocker
- **N1'** DATA overflow で in-order フレームを無 ACK 破棄 (`net/link.c:498`)。ack_seq が進まず Agent の再送が WINDOW 8 回停滞まで遅れる。→ credit をより保守的にするか overflow 時に WINDOW を credit 減で即再送。ゲストで `link_l2_overflow` を監視。
- **N3** `link_l2_eof`/`link_l1_done` が代入フラグで、試験の合格条件 `done/eof==1` が契約 (完了 = `recv_bytes==length`) と食い違う (`net/link.c:514`)。→ 試験の EOF チェックを情報行に格下げ、または EOF 消失を別扱いに。
- **F5** (WINDOW 2 通/tick): **欠陥として不成立**。PM が pcap をマイクロ秒で再確認: 対は 0.1ms 差・credit 14→20 で、隣接 tick のバッチ配送 (間に OS32 がリングを消費して空きが増えた) の可能性が高い。コード上 `want_window` は送出で 0 になり 1 tick 1 本。→ コーダーは `want_window` が 1 tick 内で再セットされないことだけ確認。

### 良い点 (レビュー)
3 way HELLO の req_sess/req_epoch 照合 (R2)・rid 台帳の規則 (1)〜(5)・HOLE・枯渇停止・flags bit0 の制御/業務分離・host_read の成功確定点・STALE close 無通知・B8 の 2 段検査は正しく実装。規約 [C1]〜[C4]/[ABI1〜3]/LE 直列化は問題無し。状態機械の反例 (B1〜B8/R1〜R4) はホスト TDD で個別に踏んでいる。

### 5. N1-fix コーダー自己申告 (2026-09-14、worktree `agent-ac9806510d71b3f80`、基点 `d584419`)

RED → GREEN の詳細・ケース名は [`tools/tests/n1_tdd.md`](../../../tools/tests/n1_tdd.md) §6。
`make` / 配備 / エミュレータは未実行 ([V4]、票の禁止事項)。手元の直接実行のみ。

| 項目 | 状態 | 変更 | RED → GREEN |
|---|---|---|---|
| **F1** | 済 | `kapi/kapi_db.c` `db_v50_selftest` (0): 数値直書きをヘッダ定数から導く形に (`KAPI_SLOT_COUNT != KAPI_SLOT_HOST_CLOSE + 1`、`DB_ERROR_CODE != 207`)、コメントも更新 | `test_kapi_db_v50.py v50_selftest` FAIL → PASS |
| **F2** | 済 | `net/link.c`: `link_counters_reset()` が `link_rt_ok`/`_fail` も打ち直す + `link_selftest` 入口で呼ぶ、L0 結果を新設 `link_l0_ok`/`link_l0_fail` へスナップショット。`net_l0_test.py` は `link_l0_ok`/`link_l0_fail` を読む。`n1_tdd.md §4` の記述訂正 | `n2_rt_ok_resets_between_selftest_sections` FAIL → PASS |
| **F4** | 済 | `net/link.c`: RTO 3 か所に `!rel_due`/`!req_due`/`!w_due` ガード、`host_open`/`host_write`/`link_free_handle` の `- LINK_RTO_TICKS` 前倒しを外す。**併せて `link_tx_round` の公平化バグ (NIC busy でも turn を進めていた) を修正** — F4 の parity 変化で `r2_R9` が露呈 | `n2_no_drop_roundtrip_has_zero_retransmits` FAIL → PASS、`r2_R9` 維持 |
| **N2** | 済 | (a)(b) `net_link_host.c` に 2 ケース (計 31)、(c) `kapi_db_v50_host.c` に `v50_selftest` ケース (計 23、`check-db-v50-host` 経由で `make check` 対象) | 上記 3 件 |
| **F3** | PM 再測 | 直接修正せず。F4 で改善見込み。ホスト試験で「未送信に RTO を課さない」を提示済み | — |
| **N3** | 済 | `net_l1_test.py`/`net_l2_test.py` の `EOF received==1` を合否から外し情報行へ (契約 = `recv==COUNT`/`read==TOTAL`) | ゲスト試験 (PM 実行) |
| **F5** | 確認済 | 欠陥不成立をコードで確認: `want_window` は `link_timers` (1 tick 1 回) だけが立て、WINDOW 送出で 0。同一 tick 内の再セット経路なし → rid ごと 1 tick 1 本。F4 の公平化でさらに厳密化 | — |
| **N1'** | **見送り** | 既存コードは毎 tick WINDOW を現行 credit で送り backpressure は効く。回復の遅さは credit の実測調整が要り、ゲスト観測 (F3 と絡む) で PM/テスターの領分。贋 NIC は決定的で再現しない。候補は F4 再測後に PM 判断 | — |

**F4 で追加した公平化修正の注意** (レビュー観点): `link_tx_round` は NIC が受けなかった (rc<0)
周回では `link_tx_turn` を進めない (送れた / 空のときだけ進める)。N0 §2a「位置は tick を
またいで保つ」に沿う。修正前は入り parity が偶然通常寄りで `r2_R9` が通っていただけ。

**PM がゲスト再測で見るカウンタ** (`kernel.map` シンボル): `link_l0_ok`/`link_l0_fail` (L0、`net_l0_test.py`)、
`link_rt_ok` (最終は L3 区間の値)、`link_retransmits` (無ドロップで 0 を期待、F4)、`link_l1_recv`/`link_l2_read` (F3)、
`link_resyncs`/`link_rt_fail`/`link_tombstones`/`link_l2_overflow` (F3 の切り分け: resync 起因か overflow 起因か)。

**着地時に注意する共有ファイル**: `net/link.c` (F2/F4 + 公平化、`link_l0_ok`/`link_l0_fail` 追加)、
`net/link.h` (extern 2 本)、`kapi/kapi_db.c` (F1)、`tools/net_l0_test.py` (`link_l0_ok` を読む)、
`tools/net_l1_test.py`/`net_l2_test.py` (N3)、`tools/tests/net_link_host.c` + `test_net_link.py` は
変更なし側と衝突しやすい (N2 の 2 ケース追加)、`tools/tests/kapi_db_v50_host.c` + `test_kapi_db_v50.py` (v50_selftest 追加)、
`tools/tests/n1_tdd.md` §4/§6。`sdk/kapi.json` は**触っていない** (v51 のまま、slot 追加なし)。

### N1-fix レビュー (Fable、2026-09-14) — Approve
判定 **Approve** (blocker 0、non-blocker 5)。契約 N0 §1a/§1b/§2a (RTO は NIC 受理 tick から、交互は tick をまたいで保持、カウンタは自己試験の区間値) を満たし、飢餓・デッドロック・再送漏れ無し。派生の公平化 (`link_tx_round` の turn 保持) も NE2000 の TX watchdog で busy が永続しないため飢餓なしと確認。

**non-blocker (次の機会に。今すぐ直さない)**:
1. **F1 の導出が次の slot 追記で崩れる** (`kapi/kapi_db.c:1152`): `COUNT != HOST_CLOSE + 1` は host_close が末尾を固定するので、v52 で 1 本足すと再び bit0 が立つ (make check の `v50_selftest` で先に見える)。→ `HOST_OPEN != DB_ERROR_CODE + 1` / `HOST_CLOSE - HOST_OPEN != 4` / `COUNT <= HOST_CLOSE` (末尾追記だけ許す下限) に。**次に KAPI slot を足す票 (今は N2/N3 に無い) と同時に直す**。
2. F4 のガードが贋 NIC (1 tick 1 フレーム) では固定されない (`!due` を外しても 31/31 PASS)。→ ホスト試験に NIC TX の栓 (`nic_tx_block`) を足して未送信中の無再送を直接踏むケース。
3. `n2_no_drop_roundtrip` の `ticks(10)` 固定 (`net_link_host.c:1393`) は実 Agent の応答が 0.5s を超えると偽 FAIL。→ `rel_pending==0` を有界待ちしてから assert。
4. `net/link.h:169` のコメント: `link_rt_ok` は自己試験中は区間値、それ以外は累積 — 文言を直す。
5. **既存 (退行ではない)** NO_SLOT 経路 (`net/link.c:387`) が `rel_tries` を増やさず RELEASE だけ落ち続けても再同期に至らない。ゲストで `link_no_slots` を監視。

2〜4 は N2 の着手時に「試験の頑健化」としてまとめる。1 は次の KAPI 追記票に付ける。

### N1-fix 配備後の受入 (2026-09-14、kernel-lgy98-link + ext2 段 B、kselftest 87/0)
- **F1 解決**: kselftest **87/0** (配備後・再起動後とも)。
- **F2 解決**: `check-net-l0` OK (`rt_ok=10`、L0 専用カウンタ)。
- **F4 解決**: `link_retransmits=0` (無ドロップ)。`resyncs=0`/`rt_fail=0`。
- **L3 OK**: GET /pattern/65536 完走・404・TIME・実 HTTP 559B。**host_test 26/26**。
- **ext2 (段 B)**: `tar c` 実用速度 (§S6P)。

### F6 [blocker、N1-fix2] 64KB を超えるストリームが 66114 B で早期 EOF
`check-net-l1` (BULK 102400) と `check-net-l2` (STREAM 131072) が**どちらも 66114 B / 131 frame で停止**。診断:
- Agent は RESPONSE の length を正しく宣言 (`host_agent.py:597` `<HI` で 102400 / 131072、`_answer` の `total`)。
- OS32 側は `link_resyncs=0` / `link_tombstones=0` / `link_l2_overflow=0` / `l1_ooo=0` / `l2_gaps=0` / `rx_dropped=0` / `retransmits=0`。**壊れずに止まる**。
- `link_l1_done=1` / `link_l2_eof=1` = **EOF を受けている**。`link_host_read_stage` は `read_bytes >= length` で 0 (完了) を返すので、**e->length が 66114 に化けている** か、**EOF が完了を短絡している**。
- **66114 は要求した total に依存しない固定値** (102400 でも 131072 でも同じ) → 比例しない = どこかの固定境界 (≈ 64KB = 65536)。**L3 の 65536 ちょうどは通る**ので閾値は 65536 の直後。
仮説 (コーダーが確定): (a) Agent の配送が WINDOW 停滞で ~64KB 送って EOF を早出しし、OS32 が EOF で `length = recv_bytes` に詰め直して完了扱い; (b) OS32 の WINDOW credit / recv_bytes / ack が 64KB 付近で頭打ち (`link_credit_pages`/`link_stream_free`、Agent の deliver の `sent`/`acked`/`credit`/`stall`)。**実害**: N3 の `wget` で 64KB 超のファイルが途中で切れる。小さい GET / PRINT / CLIP には影響しない。
受入: 贋 NIC で >64KB のストリームを流すホスト TDD (OS32 側で再現するか) と、実 Agent の `BULK 200000` (Agent 側なら再現)。両方で完走を assert。`link_l2_eof` を完了判定に使わない契約 (N0 §2c) を破っていないか確認。

#### F6 コーダー診断 (2026-09-14、N1-fix2) — 原因はコードではなく emulated NIC 経路
ホスト TDD (`tools/tests/net_link_host.c`) に >64KB を流す 4 ケースを追加し、**両仮説を否定**した。
記録は `tools/tests/n1_tdd.md` §7。要旨:

- **仮説 (a)/(b) とも不成立 (GREEN)**: 実 Agent + 贋 NIC で `GET /pattern/200000` (host_read 経路)、
  **ゲストと同じ関数** `link_l2_stream(200000,512,100)`・`link_l1_bulk(400,512)=204800B`、
  さらに Agent もタイマも介さない台本の順序どおり >64KB (length=70000) が **全部完走** (35/35 PASS)。
- **length は化けない**: RESPONSE `<HI` (u32) を `rd32(pl+2)` で読み、`link_host_status` が `ln==200000`
  を返すことを assert 済み。**EOF は完了を短絡しない**: `LINK_OP_EOF` は `link_l2_eof/l1_done` を
  立てるだけで `e->length` に触れず、完了は `read_bytes>=length` のみ (契約 N0 §2c を保持)。
- **u16/64KB 境界は無い**: 転送量に効く量は全て `u32` (`length`/`recv_bytes`/`read_bytes`/
  `link_l1_bytes`/`link_l2_read`/`stream_count/head`/`ack_seq`)。u16 は epoch/length/sess・credit・
  plen (≤1400) だけ。**credit も常に小さい** (`stream_free/256 ≤ 32` が上限 → credit ≤ 20 ページ →
  Agent inflight ≤ 3 フレーム)。ゲストと同じ credit regime を贋 NIC でも通す (L1 windows=134)。
- **原因の所在**: link.c / host_agent.py は同一関数・同一 Agent・同一 credit で >64KB を通すので、
  差分は **transport のみ** = NP21/W (ai-debug) の LGY-98 エミュレーション。`net_socket.c` の
  `SOCK_RXMAX 65536` は 1 フレーム上限 (無関係)、emulated NE2000 リングは 48KB (`lgy98dev.h`
  `NE2000_MEM_SIZE=0xC000`)。全カウンタ 0 + EOF 受信 + `recv=131` は「132+ の DATA が **OS32 の
  dispatch へ届かない** (gap にならないのは EOF が別 op だから)」と整合。credit ≤3 inflight では
  OS32 の 8 スロット rxq も 48KB リングも溢れないため、ドライバ取りこぼしではなく
  **エミュレータ配送 (reader スレッド ↔ DMA 同期 / ai-debug fork の 64KB 付近挙動)** を疑う。
- **コード修正なし** (契約を守り、再現しない現象に投機修正を当てない)。追加は回帰テスト 4 本のみ。
- **PM/テスターへ**: `kernel-lgy98-link` + エミュレータ (テスターの領分) で確認 — Agent の `--pcap` で
  132+ の DATA がワイヤに出ているか、np2netmon の RX drop、66114 到達後に時間を置いて
  `link_l1_recv`/`ack_seq`/`nic.st.rx_dropped`/`resync` が動くか。詳細は n1_tdd.md §7-6。

### F6 の PM 追加観測 (2026-09-14) — Agent の pcap で ack を確認
N1-fix2 のコーダーは「原因は emulated NIC の取りこぼし (132 番以降が届かない)」と推定したが、**PM が同じ boot の Agent 受信 pcap (`n1fix_link.pcap`) を解析すると否定される**: OS32 が送った WINDOW/ACK の最大 ack は **rid 11 (L1 BULK 200) = 200、rid 12 (L2 STREAM 256) = 256、rid 13 (L3 65536 = 128) = 128**。ack = OS32 が順序どおり受けた最終 seq なので、**OS32 は全フレームを受信・ACK している** (emulator は落としていない)。にもかかわらず `link_l1_recv=131` / `link_l1_bytes=66114` / `link_l2_read=66114` で自己試験は 66114 B で完了扱い。
→ **emulator drop ではなく、ゲストでのみ ack (受信) と recv/read カウンタ・消費が乖離する**。ホスト TDD (決定的に step) では再現せず (>64KB の 4 ケース 35/35 PASS)、コード経路は正しい。疑いは **`link_tick` (100Hz タイマ) と自己試験の `LINK_IDLE` (sti;hlt) ループの再入 / 競合** がゲストの実時間でのみ出るタイミング依存。深掘りにはゲスト計装 (MCP / トレース) が要る。**実害の範囲**: 自己試験 L1/L2 と N3 の 64KB 超 wget。GET ≤64KB / PRINT / CLIP / TIME は影響なし。**N1 のコア機能は受入可**。F6 は N3 の wget 受入で実サービス経路として再確認する (別途、深掘りはユーザー判断)。
