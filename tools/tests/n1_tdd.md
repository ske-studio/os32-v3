# N1 — Host Services 基盤 (ワイヤ v2 / `net/link.c` 非ブロッキング / KAPI v51 / Agent v2) の TDD 記録

票: [`docs/archive/network/TASK_N1.md`](../../docs/archive/network/TASK_N1.md)、
契約の正典は [`docs/archive/network/TASK_N0.md`](../../docs/archive/network/TASK_N0.md) 第 5 版
(§1a ABI / §1b ワイヤ v2 / §2 駆動・排他・状態機械 / §3 TDD の全ケース)。

試験は 2 本。**どちらも実物のソースを動かす**:

| 実行 | 何を動かすか |
|---|---|
| `python3 -B tools/tests/test_host_agent.py` (`make check-host-agent`) | 実 `tools/host_agent.py` の `HostAgent`。贋 OS32 が **フレームを直接組む** (Agent の直列化を借りない) |
| `python3 -B tools/tests/test_net_link.py --target` (`make check-net-link-host`) | 実 `net/link.c` + 実 `kapi/kapi_host.c` を `#include`。NIC / cli-sti / 100Hz タイマ / ディスパッチャだけ贋物。対向は **実 Agent** (`host_agent.py` を UNIX ソケットで子プロセス起動) か台本 |

エミュレータ・ネットワーク・`make` には触っていない (Agent は `--offline` で起動する)。

---

## 1. ケース名 ↔ TASK_N0 の指摘番号

### 1a. Agent 側 (`tools/tests/test_host_agent.py`、25 ケース)

| ケース名 | N0 §3 の由来 | 見るもの |
|---|---|---|
| `r2_R1_hello_and_roundtrip` | 往復 2 R1 | 非ゼロの `agent` 世代での 3 way HELLO → REQUEST → ACK → 業務 RESPONSE → WINDOW → DATA → 本文一致 |
| `r2_R2_request_ack_dup_and_wdata_loss` | 往復 2 R2 | REQUEST ACK の遅延重複 + WDATA 喪失の同時注入。累積 ACK が欠落で止まり、埋まると進む |
| `r2_R3_status_repeats_response_zero_len` | 往復 2 R3 | 転送 ACK 後の RESPONSE 消失 → STATUS で再提示 (**0 長の成功応答も**) |
| `r2_R4_release_drops_the_right_rid` | 往復 2 R4 | A / B の close 順。RELEASE が B だけを捨て、A の結果は残る。bit0 の ACK |
| `r2_R5_longlived_a_survives_many_short_b` | 往復 2 R5 | 長寿命 A + 短命 B × 21。A は ACTIVE のまま、8 件境界を越えた墓標は落ち、遅延 REQUEST は (3) で TOMBSTONE |
| `r2_R7_stale_hello_and_stale_response` | 往復 2 R7 | 別 sess / 別 epoch のフレームを捨てる |
| `r3_B1_release_ack_vs_request_ack` | 往復 3 B1 | RELEASE の ACK は flags bit0、転送 ACK は flags 0 |
| `r3_B2_release_before_and_after_request` | 往復 3 B2 | RELEASE 後の遅延 REQUEST = TOMBSTONE / RELEASE が REQUEST に先着 (規則 (4)) |
| `r3_B3_same_clock_restart_is_a_new_session` | 往復 3 B3 | 同じ nonce・同じ epoch で再起動しても **別 sess** (Agent 採番) |
| `r3_B4_delayed_old_syn_cannot_break_session` | 往復 3 B4 | 遅延した旧 SYN は候補を作るだけ。nonce 不一致の CONFIRM は無視 |
| `r3_B5_epoch_wrap_uses_a_new_session` | 往復 3 B5 | epoch 65535 の次が新セッションで成立 (大小比較で拒まない) |
| `r3_B6_epoch_bump_frees_active_slots` | 往復 3 B6 | 同 sess の epoch 更新で ACTIVE 2 件が消え、墓標は残り、新要求が NO_SLOT にならない |
| `r3_B6_hole_becomes_tombstone_on_epoch_bump` | 往復 3 B6 + 往復 4 R1 | epoch 切替で HOLE → RELEASED (埋まらない穴を残さない) |
| `r3_B7_business_status_not_confused_with_control` | 往復 3 B7 | 業務 503 / 本文付き 410 が flags 0、制御 NO_SLOT が flags bit0 |
| `r3_no_slot_keeps_a_hole` | 往復 3 B2 + 往復 4 R1 (規則 (1)) | 枠無しの REQUEST は NO_SLOT + **HOLE** で残り、枠が空けば (2') で受理 |
| `r4_R1_hole_accepts_the_retransmitted_request` | 往復 4 R1 | A の REQUEST 欠落 → B 受理 → A 再送が **新規**として受理 (穴を墓標にしない) |
| `r4_R2_restart_does_not_fall_back_to_old_session` | 往復 4 R2 | `req_sess` / `req_epoch` の写し。旧セッションの CONFIRM を無視し、新 sess で成立 |
| `r4_R3_sess_exhaustion_stops_the_agent` | 往復 4 R3 | 65535 で新セッションに応答せず停止。旧セッション偽装も通らない。生きている旧セッションは動く |
| `r4_sess_not_reused_across_restart` | 往復 4 R3 | `sess.txt` の永続採番 (Agent 再起動をまたいで再使用しない) |
| `r4_hello_stage_losses` | 往復 4 その他 | SYN / SYN-ACK / CONFIRM / ESTABLISHED の各消失。重複 CONFIRM に冪等な ESTABLISHED |
| `paylen_mismatch_is_dropped` | 往復 2 その他 | op ごとの payload 長検査 (5 種 + 宣言長がフレームに収まらない) |
| `hello_paylen_per_stage` | §1b HELLO 表 | SYN 0 / SYN-ACK 6 / CONFIRM 0 / ESTABLISHED 2 |
| `wdata_dedup_by_sess_rid_seq` | 往復 2 R2 / §2d | WDATA の重複排除は (sess, rid, seq) |
| `window_gates_delivery` | §1b WINDOW | WINDOW を受けた rid だけ流れ、credit を超えない |
| `time_format_is_19_bytes` | HOST_SERVICES_PLAN §2 | TIME の標準形 `YYYY-MM-DD HH:MM:SS` |

### 1b. OS32 側 (`tools/tests/net_link_host.c`、31 ケース。末尾 2 件は N1-fix N2、§6 参照)

| ケース名 | N0 §3 の由来 | 見るもの |
|---|---|---|
| `r2_R1_agent_request_response_body` | 往復 2 R1 | **実 Agent** と HELLO → `host_open` → `host_status` → `host_read` で 2048B 一致 |
| `r2_R8_selftest_is_driven_only_by_the_timer` | 往復 2 R8 | KAPI を何度呼んでも tick が増えない = KAPI は `link_tick` を呼ばない。進むのはタイマだけ |
| `r2_R3_status_probe_repeats_lost_response` | 往復 2 R3 | 転送 ACK 後に RESPONSE 消失 → `T_probe` 後に STATUS → 0 長の再提示を拾う |
| `r2_R4_release_carries_the_right_rid` | 往復 2 R4 | B → A の順で close して、両方の rid の RELEASE が出る |
| `r2_R9_alternating_tx_one_frame_per_tick` | 往復 2 R9 | NIC が 1 tick 1 フレームでも制御と通常が交互に出て飢えない。持ち越し (`link_tx_deferred`) を数える |
| `r2_R10_reflect_mode_does_not_run_link_tick` | 往復 2 R10 | `link_init` されていない (反射モード) と `link_tick` が RX を消費しない |
| `r3_B1_release_resends_until_bit0_ack` | 往復 3 B1 | RELEASE 消失 + 旧 REQUEST の ACK (flags 0) では止まらず、bit0 の ACK で止まる |
| `r3_B6_stale_close_sends_no_release` | 往復 3 B6 反例 2 | 再同期 → STALE → close で RELEASE を送らない。二重 close は INVAL |
| `r3_B7_business_status_vs_control` | 往復 3 B7 | 制御 NO_SLOT を業務結果にしない。業務 503 / 本文付き 410 は届く |
| `r3_tombstone_makes_the_handle_stale` | 往復 3 B7 / §2c | 制御 TOMBSTONE → STALE (status / read は STALE、close は通る) |
| `r3_no_slot_resends_release_then_request` | 往復 4 non-blocker (NO_SLOT 後の再送) | NO_SLOT で **RELEASE を先に**、次に REQUEST を再送 |
| `r3_B8_dispatcher_kill_and_wrapper_inval` | 往復 3 B8 | 先頭帯外 → ディスパッチャが kill (ラッパーへ入らない) / 先頭帯内 + 長さ超過 → ラッパーが INVAL / **出力ポインタは全部検証してから書く** |
| `r4_R4_resync_during_user_copy_keeps_the_return_value` | 往復 4 R4 | 成功確定点は最初の cli 区間。写しの途中で再同期しても戻り値と中身が一致し、次が STALE |
| `r4_hello_stage_losses` | 往復 4 その他 | SYN 再送で nonce が進む / CONFIRM 再送 / 古い SYN-ACK を採用しない |
| `r4_cpl0_open_close` | 往復 4 その他 | CPL=0 経路 (ディスパッチャを通らない直呼び) の open / close / 二重 close |
| `r6_agent_silence_resyncs_and_stales_handles` | 往復 2 R6 | ACK 済み 2 本で Agent が黙る → STATUS × k → 再同期 → 両方 STALE |
| `wire_paylen_mismatch_is_dropped` | 往復 2 その他 | op ごとの payload 長検査 (OS32 側) |
| `wire_stale_session_frames_are_dropped` | 往復 2 R7 / §1b | 別 sess / 別 epoch の RESPONSE を採用しない |
| `irq_if0_defers_the_timer_then_runs_it` | §2a | IF=0 中のタイマ延期と、IF 復元直後の tick |
| `irq_injection_during_tx_build` | §3 「割込み挿入」 | TX 構築の途中に贋 IRQ5 が DATA を積んでも落とさない |
| `read_last_byte_then_zero` | §1a `host_read` | 最後のバイトを写した呼び出しは正の長さ、**その次**が 0 |
| `ring_owner_is_exclusive_until_close` | §2b | リングの所有は最初に読んだ側が close まで持つ |
| `write_contract_declared_length` | §1a `host_write` | 宣言長なし / 残り超過 / 転送 ACK 前 / 未 ACK の WDATA がある間 |
| `decl_len_parsing` | HOST_SERVICES_PLAN §2 | `ECHO` / `CLIP PUT` / `PRINT DATA` / `PUT` の宣言長、GET には付かない |
| `owner_exit_reclaims_handles` | §1a owner 回収 | `host_owner_exit(4)` が owner 4 のハンドルだけ回収し RELEASE を出す |
| `handles_are_owner_private` | §1a | 他人のハンドルは status / close ともに INVAL |
| `open_error_codes` | §1a `host_open` | 未確立 STALE / 0 長・1400B 超 INVAL / 3 本目 FULL / NIC 無し NOSYS / RELEASE 未 ACK の枠は AGAIN |
| `last_data_and_eof_loss` | §3 その他 | 最終 DATA 消失。**EOF だけでは完了にしない** (完了は `recv_bytes == length`) |
| `data_gap_is_dropped_and_reacked` | §1b DATA | 先行 DATA を捨てて累積 ACK を止め、埋まると進む (Go-Back-N) |
| `n2_no_drop_roundtrip_has_zero_retransmits` | N1-fix N2 (a) / F4 | 無ドロップ往復で `link_retransmits == 0` (未送信フレームに RTO を課さない) |
| `n2_rt_ok_resets_between_selftest_sections` | N1-fix N2 (b) / F2 | `link_selftest` → `link_l1_bulk` で `link_rt_ok` が区間ごとに reset、`link_l0_ok` は保たれる |

ソース本文で見張る 2 件 (`tools/tests/test_net_link.py` の冒頭):

- `check_reclaim_has_host_owner_exit()` — `exec_reclaim_owned` の `host_owner_exit(id)` が
  `launch_owner_exit` と `con_sink_owner_exit` の**間**にある。
- `check_timer_calls_link_tick()` — `timer_handler` が `ne2k_timer_tick()` の**直後**に
  `link_tick()` を呼び、`kapi/kapi_host.c` が `link_tick` を呼ばず、`net/link.c` の中にも
  呼び出しが無く (定義 1 か所だけ)、`drivers/lgy98.c` が反射モードで `link_init` を止めている。

---

## 2. RED → GREEN

### 2a. 段 1 (Agent v2)

最初の実行 (実装直後、ケース 25 本):

```
SUMMARY 21/25 PASS
  FAIL r3_B3_same_clock_restart_is_a_new_session: 再起動が同じ sess を貰った (1)
  FAIL r4_R2_restart_does_not_fall_back_to_old_session
  FAIL r4_R3_sess_exhaustion_stops_the_agent: 別 MAC の旧セッション偽装に答えた
  FAIL window_gates_delivery: credit 12 ページで 2 本流れた
```

- **B3 は実装の欠陥**。SYN の再送で `sess` を無駄に消費しないよう「同じ nonce・同じ
  `req_sess` / `req_epoch` の SYN は候補を使い回す」と書いていたが、これだと
  **同じ RTC 秒・同じ初期 tick で再起動した OS32 が旧 sess を貰う** (= B3 の反例そのもの)。
  N0 §1b は「`seq` = OS32 の nonce (**HELLO ごとに +1**)」なので、SYN の再送は必ず
  別の nonce になる。候補の使い回しをやめ、**SYN のたびに候補を作り直す**ことにした
  (`tools/host_agent.py` の `_on_syn`)。採番するのは新セッションの SYN だけで、
  セッション内の再同期 (`req_sess` == 現行 sess) は同じ sess のまま = 再送で sess を減らさない。
- **R2 / R3 / window は試験側の思い違い**。R2 は「再起動側が新 sess で成立する」ことを
  見るべきなのに `a.sess != old` を主張していた (Agent は CONFIRM が来るまで切り替えない
  ので正しくは動いていた)。R3 の偽装は同じ MAC を使っていたので現行セッションの
  正当なフレームだった。window は credit 12 ページ ÷ 6 ページ/フレーム = 2 本が正解。
  3 件とも試験を直した。

GREEN: `SUMMARY 25/25 PASS`。

### 2b. 段 2・3・4 (`net/link.c` / KAPI v51 / ハーネス)

最初の実行 (ケース 29 本):

```
SUMMARY 27/29 PASS
  FAIL r3_no_slot_resends_release_then_request: NO_SLOT の後に RELEASE を再送しない
  FAIL r4_hello_stage_losses: SYN 再送で nonce が進んでいない / CONFIRM が出ない
```

どちらも **`net/link.c` の欠陥**で、試験は正しかった:

1. **NO_SLOT の扱い** (`link_on_response`)。「そのハンドルの `rel_pending` を立て直す」と
   書いていたが、未達の RELEASE は **NO_SLOT を受けた rid のハンドルのものとは限らない**
   (閉じた**別の**ハンドルの専用スロットに残っている)。保留中の RELEASE を全部立て直し、
   `link_tx_turn = 0` で次の 1 本を制御 (= RELEASE) にしてから REQUEST を再送するよう直した。
2. **handshake のタイマ** (`link_timers`)。`if (link_hs_due == 0) link_hs_due = 1;` を
   毎 tick 実行していたため、(a) CONFIRM 送出直後の tick が **CONFIRM ではなく SYN** を
   立て直し、(b) RTO を待たずに再送していた。`if (!link_hs_due && since(link_hs_tick) >= RTO)`
   に直し、**印が立っている間 (= まだ NIC が受けていない) は期限を数えない**
   (RTO は NIC 受理 tick から、往復 2 の R9) ようにした。

GREEN: `SUMMARY 29/29 PASS` + `TARGET i386-elf GNU89 -Werror COMPILE PASS`。

---

## 3. 決めたこと (N0 に無い細部。PM 向け)

1. **HELLO の payload 長**: N0 §1b の op 表は HELLO を「0 / 2B」と書いているが、同じ §1b の
   3 way の表は SYN-ACK に 6B (`agent` + `req_sess` + `req_epoch`) を要求している。
   後者 (往復 4 の反映) を採り、**SYN 0 / SYN-ACK 6 / CONFIRM 0 / ESTABLISHED 2** とした
   (ESTABLISHED の 2B は `agent`。OS32 が世代を確定できるようにする)。
   両端の検査表: `net/link.c` の `link_paylen_ok`、`tools/host_agent.py` の `HELLO_PAYLEN`。
2. **制御 RESPONSE の符号**: `PROCESSING = 1` / `TOMBSTONE = 2` / `NO_SLOT = 3`
   (`net/link.h` の `LINK_CTL_*`、`host_agent.py` の `CTL_*`)。N0 は名前だけを決めていた。
   業務側の HTTP ステータスとは flags bit0 で分かれるので値の衝突は起きない。
3. **宣言長の読み方**: 要求行の動詞で決める — `ECHO <len>` は 2 番目の語、
   `CLIP PUT <len>` / `PRINT DATA <id> <len>` / `PUT <path> <len>` は**末尾の語**。
   数字以外・`LINK_DECL_MAX` (64KB) 超は「宣言長なし」= 0 として扱う (`link_decl_len`)。
4. **`link_tick` は `ne2k_poll()` を呼ばない**。直前に走る `ne2k_timer_tick()` が
   リング回収をするので、リンク層は `ne2k_recv` でホストキューを drain するだけ
   (1 周回 `LINK_RX_BUDGET` = 16 フレーム)。
5. **反射モードの止め方**: `drivers/lgy98.c` が `LGY98_FLAG_REFLECT` のとき `link_init` を
   呼ばない → `link_ready == 0` → `link_tick` が即 return。`timer_handler` の並びは
   1 本のままで済む。
6. **`host_open` の TX 満杯**: ハンドルごとに REQUEST の枠が 1 つあるので、
   FREE のハンドルが取れた時点で枠は必ず空いている = `AGAIN` は
   「専用スロットの RELEASE が未 ACK」のときだけ返る。
7. **rid / epoch の枯渇**: `link_next_rid` が `0xFFFFFFFF` に達した `host_open` は
   再同期 (新セッション) を起こして `STALE` を返す。`epoch` が 65535 のときの再同期は
   `sess = 0` の SYN (新セッション)。
8. **Agent の HOLE 生成に上限**: `high_water` が跳ねたとき作る HOLE は最大 64 件
   (`HOLE_FILL_MAX`)。壊れたフレームが巨大な rid を名乗っても台帳が膨らまない。
   OS32 のハンドルは 2 本なので正常系では 1〜2 件しか出ない。
9. **`sess.txt` の書き方**: `<state-dir>/sess.txt` に 10 進 1 行。`.tmp` へ書いて
   `fsync` → `os.replace` (途中で落ちても番号が戻らない)。`--state-dir` を渡さない
   (既定) ときはメモリ内だけ = 試験用。
10. **L1 / L2 の自己試験サービス**: v1 の `BULK` / `STREAM` を Agent v2 にも残した
    (下記 §4)。v1 では RESPONSE を返さずいきなり DATA を流していたが、v2 では
    他のサービスと同じく **業務 RESPONSE (200, 総バイト数) → WINDOW → DATA** になる。
11. **ホスト試験の型**: ホストに 32bit libc が無いので、`tools/tests/net_link_host.c` は
    64bit + 贋 `types.h` (`u32 = unsigned int`) で組む。ポインタを `u32` に落とす検証は
    `kapi_db_v50_host.c` と同じ「登録した実ポインタ帯の下位 32bit 一致」で代用する。

---

## 4. 既存のゲスト観測試験への影響 (`tools/net_l0_test.py`〜`l3`、`check-net-m2`)

コーダーは**これらを実行していない** (ゲスト観測なので PM / テスターの受入)。
読むシンボルが v2 でも意味を持つよう保った / 変えた点:

| シンボル | v2 での意味 |
|---|---|
| `link_hello_ok` | **セッション確立 (0/1)**。v1 は「HELLO を 1 通受けた」だったが、v2 は 3 way が ESTABLISHED まで通ったとき 1 |
| `link_peer_mac` / `link_epoch` | 同じ (peer の MAC / 現在の再同期世代) |
| `link_rt_ok` | **現在の自己試験区間の成功往復数**。各自己試験 (L0〜L3) の入口で `link_counters_reset()` が 0 に打ち直すので、最終読み出しでは最後に走った L3 の値になる (N1-fix F2 で修正。v1 の「累積した往復数」とは違う) |
| `link_rt_fail` | 現在の区間の失敗往復数 (区間ごとに reset) |
| **新規** `link_l0_ok` / `link_l0_fail` | **L0 selftest 専用のスナップショット** (N1-fix F2)。`link_selftest` が区間末に `link_rt_ok` / `link_rt_fail` を写す。`net_l0_test.py` は `link_rt_ok` ではなく**これ**を最終読み出しで見る (L1〜L3 が `link_rt_ok` を打ち直すため) |
| `link_retransmits` | REQUEST / WDATA / RELEASE / CONFIRM / SYN の再送回数 (v1 は REQUEST と HELLO だけ) |
| `link_rx_frames` / `link_rx_dropped` | 同じ (受けた数 / 検査に落ちた数)。v2 は payload 長検査と sess/epoch 照合で落ちる分が増える |
| `link_l1_recv` / `_bytes` / `_ooo` / `_windows` / `_max_credit` / `_min_credit` / `_meas_pages` | 同じ意味。`_recv` / `_bytes` は**リング所有ハンドルの DATA** を数える |
| `link_l1_done` / `link_l2_eof` | EOF を受けた (0/1)。**完了判定には使わない** (完了は `recv_bytes == length`) |
| `link_l2_bytes` / `_read` / `_gaps` / `_bad` / `_overflow` | 同じ |
| `link_l3_*` (8 本) | 同じ |
| **新規** `link_sess` / `link_agent_gen` / `link_resyncs` / `link_tombstones` / `link_no_slots` / `link_processing` / `link_tx_deferred` | v2 の観測点 (`net/link.h` 末尾) |

消えたシンボル: `link_tx_seq` / `link_rx_ack` / `link_last_resp*` (static だったので
`kernel.map` には出ない)。公開関数では `link_poll` / `link_hello` / `link_request` /
`link_stream_read` / `link_service_get` が**無くなった** (非同期 API に置き換え)。
`net/l*_test.py` はどれもカウンタしか読まないので参照は無い
(`grep -rn 'link_' tools/net_l*_test.py` で確認済み)。

`check-net-m2` の反射試験は、`drivers/lgy98.c` で `LGY98_FLAG_REFLECT` のとき
`link_init` を呼ばなくしたので **v1 と同じ動作** (RX の消費者は反射だけ)。

---

## 5. 実行結果 (コーダーの手元、2026-09-14)

```
$ python3 -B tools/tests/test_host_agent.py
  ok   r2_R1_hello_and_roundtrip
  ... (25 本)
SUMMARY 25/25 PASS

$ python3 -B tools/tests/test_net_link.py --target
ORDER SOURCE: host_owner_exit between launch/con_sink PASS
DRIVE SOURCE: link_tick only from timer_handler PASS
HOST GNU89 -Werror compile PASS (real net/link.c + kapi/kapi_host.c)
TARGET i386-elf GNU89 -Werror COMPILE PASS
  ok   r2_R1_agent_request_response_body
  ... (29 本)
SUMMARY 29/29 PASS
EXIT net_link_host=0

$ python3 tools/check_kapi_version.py
KAPI バージョン一致: v51 (4 箇所)
KAPI_SPEC.md の関数表: kapi.json と一致
```

個別のカーネル側コンパイル (`i386-elf-gcc -Wall -Wextra -Werror`) も通した:
`net/link.c` / `kapi/kapi_host.c` / `exec/exec.c` / `kernel/isr_handlers.c` /
`drivers/lgy98.c` / `kapi/kapi_generated.c`、および
`userland/tests/host_test.c` (ユーザランドのフラグ)。

**やっていないこと** ([V4]): `make all` / `make check` / `make clean` / 配備 /
エミュレータでの実行 / `check-net-l0`〜`l3` / `check-net-m2` / `host_test` の実機実行。
どれも票で PM の担当。

---

## 6. N1-fix (実装レビュー + ゲスト受入の修正、2026-09-14、基点 `d584419`)

TASK_N1 §4 の blocker F1 / F2 / F4 / N2 と non-blocker N3 / F5 を実装。RED → GREEN は
すべてコーダーの手元で確認 (`make` / エミュレータ / 配備は未実行 = PM の担当)。

### 6-1. F1 (kselftest 86/1 — v51 で v50 検査が崩れる)

`kapi/kapi_db.c` の `db_v50_selftest()` (0) が slot 件数を数値直書きしていた:
`KAPI_SLOT_COUNT != 208` と `KAPI_SLOT_DB_ERROR_CODE != KAPI_SLOT_COUNT - 1`。
v51 で `KAPI_SLOT_COUNT` = 213・`DB_ERROR_CODE` = 207・末尾は `HOST_CLOSE` に変わり bit0 が立つ。
→ ヘッダ定数から導く形に ([C4]): `KAPI_SLOT_COUNT != KAPI_SLOT_HOST_CLOSE + 1` と
`KAPI_SLOT_DB_ERROR_CODE != 207` (db 帯の末尾)。コメントも db 帯 201..207 + host 帯 208..212 に更新。

- **RED**: `python3 -B tools/tests/test_kapi_db_v50.py v50_selftest` → `FAIL v50_selftest: db_v50_selftest() == 0`
- **GREEN**: 同上 → `PASS v50_selftest` (23/23)

### 6-2. F2 (`check-net-l0` rt_ok=16 — L0〜L3 で累積)

`link_rt_ok` は業務 RESPONSE ごとに増え、L0〜L3 の全部を通じて累積していた
(`net_l0_test.py` は最終読み出しで `== 10` を期待するので恒常 FAIL)。
→ `link_counters_reset()` に `link_rt_ok = link_rt_fail = 0` を足し、`link_selftest` (L0) の
入口でも呼ぶ。L0 の結果は区間末に専用 `link_l0_ok` / `link_l0_fail` へスナップショット。
`net_l0_test.py` は `link_rt_ok` ではなく `link_l0_ok` / `link_l0_fail` を読む。
`n1_tdd.md §4` の「v1 の往復数と同義」も訂正済み。

### 6-3. F4 (`link_retransmits=32` — 未送信フレームに RTO)

`net/link.c` の RTO 判定 3 か所 (RELEASE / REQUEST / WDATA) にハンドシェイクと同じ
`!e->rel_due` / `!e->req_due` / `!e->w_due` ガードを追加し、`host_open` / `host_write` /
`link_free_handle` の `last_tx_tick` / `rel_tick = tick_count - LINK_RTO_TICKS` の前倒しを外した
(初回送信は `due=1` が担保、RTO は NIC 受理 tick から)。

- 併せて `link_tx_round` の公平化バグを修正 (F4 が露呈): NIC busy で送れなかった周回でも
  `link_tx_turn` を進めていたため、制御 (WINDOW) が毎 tick 先着して通常 (WDATA) が飢える
  (N0 §2a「位置は tick をまたいで保つ」に反する)。送れた / 空のときだけ進めるよう直した。
  修正前は `link_tx_turn` の入り parity が偶然通常寄りで `r2_R9` が通っていた。

### 6-4. N2 (ホスト TDD にカウンタ契約を追加)

- `net_link_host.c` に 2 ケース追加 (`test_net_link.py`、計 31)。
  - `n2_no_drop_roundtrip_has_zero_retransmits` — 実 Agent と無ドロップ往復で `link_retransmits == 0`。
    **RED** (F4 revert): `FAIL: 無ドロップ往復で再送が計上された` / **GREEN**: `ok`。
  - `n2_rt_ok_resets_between_selftest_sections` — `link_selftest(3)` → `link_l1_bulk(4,512)` で
    `link_rt_ok` が区間ごとに打ち直され、`link_l0_ok` が保たれる。
    **RED** (F2 revert): `FAIL: L1 区間で link_rt_ok が打ち直されず累積している` / **GREEN**: `ok`。
- `kapi_db_v50_host.c` に `v50_selftest` ケース追加 (`test_kapi_db_v50.py`、計 23、`make check` の
  `check-db-v50-host` 経由)。`db_v50_selftest() == 0` を踏む (F1 の回帰)。ホストは 64bit 幅なので
  (2) の `0xFFFFFF00` overflow が起きない → `host_cpl3 = 1` (帯 [BAND_LO,BAND_HI)) で帯外判定にする。

### 6-5. non-blocker

- **N3** (完了判定と EOF の齟齬): `net_l1_test.py` / `net_l2_test.py` の `EOF received == 1` を
  合否から外し情報行に格下げ (契約は `recv == COUNT` / `read == TOTAL` = `recv_bytes == length`)。
- **F5** (WINDOW 2 通/tick): **欠陥として不成立をコードで確認**。`want_window` は
  `link_timers` (1 tick 1 回) だけが立て、`link_tx_control` の WINDOW 送出で 0 に落ちる。
  同一 tick 内に再セットする経路は無い (次 tick の `link_timers` まで 0 のまま) → rid ごと 1 tick 1 本。
  F4 の公平化修正で 1 tick 1 フレームはさらに厳密化。PM の pcap 再確認 (隣接 tick のバッチ配送) と整合。
- **N1'** (DATA overflow の無 ACK 破棄): **見送り**。既存コードは streaming 中 `link_timers` が
  毎 tick WINDOW を現行 credit で送っており backpressure は効いている。回復の遅さ (ack_seq 停滞) の
  改善は credit の実測調整が要り、ゲスト観測 (PM/テスター) の領分で F3 の再測と絡む。
  ホスト TDD の贋 NIC は決定的で overflow 回復のタイミングを再現しないため、手元で合否を取れない。
  候補 (overflow 時に want_ack/WINDOW を即再送、または credit をより保守的に) は F4 再測後に PM が判断。

### 6-6. F3 (PM がゲストで再測)

F4 修正で改善見込み。コーダーはホスト試験で「未送信フレームに RTO を課さない」ことを
`n2_no_drop_roundtrip_has_zero_retransmits` で示した (贋 NIC の TX 受理 tick を明示し、
受理前は再送しない)。実機の L1/L2 再測と `link_resyncs` / `link_rt_fail` / `link_tombstones` /
`link_l2_overflow` の読み出しは PM。

### 6-7. 手元の実行結果

```
$ python3 -B tools/tests/test_net_link.py --target
... TARGET i386-elf GNU89 -Werror compile PASS
SUMMARY 31/31 PASS      (F4 + N2 の 2 ケース追加)
$ python3 -B tools/tests/test_host_agent.py
SUMMARY 25/25 PASS      (変更なし)
$ python3 -B tools/tests/test_kapi_db_v50.py
SUMMARY 23/23 PASS      (F1 + N2c の v50_selftest 追加)
```

## 7. F6 [N1-fix2] — 64KB を超えるストリームが 66114 B で止まる

### 7-1. 症状 (PM がゲストで確定、TASK_N1 §4 F6)

`check-net-l1` (`link_l1_bulk(200,512)` = 102400 B) と `check-net-l2`
(`link_l2_stream(131072,512,100)`) が **どちらも 66114 B / 131 frame で停止**。要求 total に
依存しない固定値・両 self-test の tick 予算が違う (1500 / 3000) のに同値 →
レート/タイムアウトではなく 65536 直後の固定境界。`GET /pattern/65536` (ちょうど 64KB) は完走。
ゲスト診断カウンタは全て 0 (resync/overflow/gaps/ooo/rt_fail/rx_dropped/retransmits)、
`link_l1_done=1`/`link_l2_eof=1`。

### 7-2. RED を狙ったホスト TDD (実 Agent + 台本、4 ケース)

契約どおり >64KB を流し、OS32 が宣言 length ぶんを完走するかを見る。

- `n1fix2_stream_over_64k_completes` — 実 Agent `GET /pattern/200000` を host_read 経路で全消費
  (N3 wget と同じ L3 ストリーム)。
- `n1fix2_l2_stream_over_64k_completes` — 実 Agent、**ゲストと同じ関数** `link_l2_stream(200000,512,100)`
  (seq100 で 1 回欠落 → Go-Back-N 回復)。
- `n1fix2_l1_bulk_over_64k_completes` — 実 Agent、`link_l1_bulk(400,512)` = 204800 B
  (ゲスト `check-net-l1` の 2 倍)。
- `n1fix2_scripted_inorder_over_64k` — **Agent もタイマも介さない台本**。length=70000 を宣言し、
  512B DATA を seq 1..137 で順序どおり注ぎ、都度 host_read で消費。OS32 の受理 / length 保持 /
  完了判定 (`read_bytes==length`) に 65536 の境界が無いことを決定的に確認。

**結果: 4 ケースとも GREEN。F6 はホスト TDD で再現しない。**
```
n1fix2_stream_over_64k_completes        ok  (200000/200000)
n1fix2_l2_stream_over_64k_completes     ok  (read=200000, bad=0, gaps=2 → 回復)
n1fix2_l1_bulk_over_64k_completes       ok  (recv=400/400, 204800B)
n1fix2_scripted_inorder_over_64k        ok  (got=70000, length 保持, EOF 非短絡)
```

### 7-3. 切り分け — Agent 側でも OS32 側 (コード) でもない

- **length は化けない**: RESPONSE の `struct.pack("<HI", status, total)` (u32) を OS32 は
  `rd32(pl+2)` で読む。`n1fix2_stream` は `link_host_status` が `ln==200000` を返すことを assert。
- **EOF は完了を短絡しない**: 現行 `link_dispatch` の `LINK_OP_EOF` は `link_l2_eof/l1_done` を
  立てるだけで `e->length` に触れない。完了は `link_host_read_stage` の `read_bytes>=length`
  だけ (既存 `last_data_and_eof_loss` が「EOF だけでは完了にしない」を保証)。
- **u16/符号の 64KB 境界は無い**: 転送量に効く量 (`e->length`/`recv_bytes`/`read_bytes`、
  `link_l1_bytes`/`link_l2_read/bytes`、`link_stream_count/head`、`e->ack_seq/wseq`) は全て `u32`。
  u16 は header の epoch/length/sess・credit・plen (≤1400) だけで、いずれも転送で 65535 を超えない。
- **credit は常に小さく Agent は溢れさせない**: `link_credit_pages` は
  `min(ring, qfree*6, stream_free/256) - 12`。`stream_free/256 ≤ 8192/256 = 32` が上限を握り、
  credit ≤ 20 ページ → Agent の inflight ≤ 3 フレーム。ゲストと同じ regime を贋 NIC でも通し
  (L1 は windows=134 で完走)、burst による emulated ring 溢れは起きえない。

### 7-4. 原因の所在 (コードではなく emulated NIC 経路)

link.c と host_agent.py は >64KB を正しく転送する (上記 4 ケース)。ゲスト self-test は同じ関数・
同じ Agent・同じ credit regime。差分は **transport だけ** = NP21/W (ai-debug fork) の LGY-98
エミュレーション。確認した点:

- 台本経路: `net_socket.c` の `SOCK_RXMAX 65536` は **1 フレームの length 上限** (512B フレームには
  無関係)。累積 64KB の上限は socket 層に無い。
- emulated NE2000 リング (`lgy98dev.h`): `NE2000_PMEM_SIZE=32KB` / `NE2000_MEM_SIZE=48KB` (0xC000)。
  64KB ではない。ソケット reader スレッド (`reader_thread` → `np2net_deliver` → `recieve_packet`
  → `fd_read`) が非同期でリングへ書く。

`link_l1_recv=131`/`link_l2_eof=1`/全カウンタ 0 は「OS32 が 131 DATA を順序どおり受けた後は EOF
しか届かず (132+ が OS32 の dispatch へ届かない)、gap にならない (EOF は別 op)」と一致する。
= 132 番以降の DATA が **emulated NIC 内で OS32 に見えず消える**。credit ≤3 inflight では OS32 の
8 スロット rxq も 48KB リングも溢れないため、ドライバ層の取りこぼしではなく **エミュレータの
配送 (reader スレッド ↔ ゲスト DMA の同期、または ai-debug fork 固有の 64KB 付近の挙動)** を疑う。

### 7-5. 手元の実行結果 (GREEN)

```
$ python3 -B tools/tests/test_net_link.py --target
... TARGET i386-elf GNU89 -Werror compile PASS
SUMMARY 35/35 PASS      (F6 の >64KB 4 ケース追加、L3/L0/既存に回帰なし)
$ python3 -B tools/tests/test_host_agent.py
SUMMARY 25/25 PASS      (Agent 無変更)
```

### 7-6. PM / テスターへの引き継ぎ (ゲストで確認する点)

コードは >64KB を通すので、次はゲストで transport を観測する:
- Agent 側で配送数を数える (`--pcap` または log): `sent` が `nframes` に達するか、
  132+ の DATA が **ワイヤに出ているか**。出ていれば emulated NIC 側の取りこぼし。
- `/api/net` の np2netmon RX drop カウンタ (`NP2NETMON_RX`)。
- ゲストの `link_l1_recv`/`ack_seq`/`nic.st.rx_dropped`/`nic.st.resync` を 66114 到達後も時間を置いて
  複数回読み、止まっているか (Agent が resend していないか、emulated NIC が落とし続けているか)。
- 64KB 直後で切れるなら ai-debug fork の socket/NIC 経路 (reader スレッドのバッファリング) を疑う。
  これは `kernel-lgy98-link` ビルド + エミュレータ操作 (テスターの領分) が要る。
