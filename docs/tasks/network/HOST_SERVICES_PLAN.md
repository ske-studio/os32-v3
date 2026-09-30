# Host Services 詳細計画 — ネットワーク・印刷・時刻・クリップボードをホスト (Windows) に丸投げする

> 発行: PM (2026-09-13) / 状態: **受入完了 (2026-09-15)**

N1〜N4 すべて受入完了 (2026-09-15)。LGY-98 は既定ビルド。CUI (wget/lpr/hclip/hdate) と GUI (ファイラ印刷・端末コピペ) の両方で実証済み。残: N5 (実機)。
上位: [LINK_PLAN.md](LINK_PLAN.md) (リンク層 L0〜L3 の正典、機構はエミュレータ合格済み)、[PLAN.md](PLAN.md) (LGY-98 ドライバ、M1〜M4 合格、M5 = 実カード)。
本書は LINK_PLAN §5-1 の「残りは KAPI 公開だけ」を、**外部プログラムから使える形**まで分解し、印刷などネットワーク以外のサービスを同じ仕組みに載せる計画。契約の正典は LINK_PLAN のまま (フレーム形式・フロー制御は変えない)。

## 0. 方針 (LINK_PLAN §1 の再確認)

OS32 に TCP/IP・DNS・HTTP・TLS・プリンタドライバを載せない。**ホスト = I/O コプロセッサ**。OS32 は「要求」を出し「結果」を受け取るだけ。
ホスト側の実体は **Host Agent** (`tools/host_agent.py`、Python)。エミュレータでは NP21/W の LAN ソケットバックエンド経由、実機では LGY-98 の raw Ethernet で同じ Agent と話す。

```
OS32 アプリ ── KAPI host_* ── net/link.c (L0〜L3) ── LGY-98 ── [NP21/W socket | 実 LAN] ── host_agent.py ── Windows (HTTP / プリンタ / 時計 / クリップボード / ファイル)
```

## 1. 現状 (2026-09-13、コードで確認)

| 層 | 状態 |
|---|---|
| ドライバ `drivers/lgy98.c` `ne2000*.c` | M1〜M4 エミュレータ合格 (検出、送受信、IRQ5、リング溢れの安全網、100Hz 受信ウォッチドッグ)。**既定は無効** (`make kernel LGY98=1` / `kernel-lgy98`、スタンプ `build/out/lgy98.flags`)。M5 (実カード: 受信 count の FCS、16KB RAM、8bit 転送) 未 |
| リンク層 `net/link.{c,h}` | L0 HELLO/REQUEST/RESPONSE/ACK、L1 絶対値 WINDOW (credit = min(ring 空き, queue 空き×6, ストリーム空き) − 12 ページ)、L2 8KB リングの順次消費 + Go-Back-N、L3 `link_service_get()` (同期版)。EtherType 0x88B5、ヘッダ 16B |
| Host Agent `tools/host_agent.py` | HELLO 応答、`GET /pattern/N`、`GET http(s)://…` (urllib)、`GET /file/<path>` (ホストのファイル読み)、`TIME`、L1/L2 の bulk / stream 配送 |
| KAPI | **未公開**。v43 を予約していたが版は v50 まで進んだので **v43 の番号は使えない** (単調増加)。次の空きは **v51** → KAPI_SPEC §3-2 の予約表を「v43 据え置き → v51 に移す」と改訂する |
| 試験 | `make check-net-l0/l1/l2/l3`、`net_m2/m4_test.py` (NP21/W の inject/capture)。ini の 5 キー (`USELGY98` 等) は設定済み ([D2]、変更しない) |

## 2. サービス一覧 (Host Agent が受ける要求)

ワイヤは **TASK_N0 §1b (v2)** が正典。要求は REQUEST フレームの payload (ASCII 行、最大 1400B)、
応答は RESPONSE (`status u16` + `length u32`、6B 固定 — TIME / PING も同じ。業務結果は flags 0 で HTTP ステータスをそのまま載せ、リンクの制御結果は flags bit0 で分ける) + 本文の DATA
ストリーム (OS32 が WINDOW を送った `rid` だけ流れる)。**OS32 → ホスト方向の本文** (印刷ジョブ、
CLIP PUT、PUT) は**要求行に宣言長を書き**、WDATA (`rid`、`seq` = 1〜、1 本ずつ ACK 待ち) で送る。
Agent は宣言長ぶん受け切ったら RESPONSE を返す。要求ごとに `rid` が付くので、要求行に
`seq` は書かない (v1 の `PRINT DATA <id> <seq>` 案は廃止)。

| 要求 | 引数 | 応答 | 用途 / 備考 |
|---|---|---|---|
| `GET <url> [--as text\|mgx[:W,C]]` | `http://` `https://` | status = HTTP ステータス、本文 | 既存。リダイレクトはホストが追う。ヘッダは返さない (v1)。**HTML の既定表現は `text`** (ホストで `w3m -dump` 相当に落とし、リンクは番号付き一覧、図の位置に印) — 5〜20KB で端末 / `edit` にそのまま出せる。`mgx:640,16` を指定すると headless ブラウザで描画して MGX (`tools/img2mgx.py` と同じ形式、`mgxview` で表示) を返す — 1 画面 3〜10 万 B、忠実な見た目が要るときだけ (PM 所見 2026-09-13、決裁 §9-6) |
| `GET /file/<path>` | ホスト側パス | 200 / 404、本文 | 既存。**許可リスト** (§6) の下だけ |
| `TIME` | — | 200、本文 = `YYYY-MM-DD HH:MM:SS` (19B) | 既存 (v2 では標準ヘッダ + 本文ストリーム)。時刻同期 (`date -sync` を足す) |
| `PING` / `ECHO <len>` | — / 宣言長 | 200 / 200 + 本文 = 受けた WDATA | N1 の受入用 (ECHO は WDATA → DATA の折り返し) |
| `PRINT OPEN <name> <kind>` | kind = `text` / `raw` | status、本文 = `job <id>` | **新規**。ジョブを開く。`text` は UTF-8 テキスト (ホストが CP932 / フォント処理を担う)、`raw` はプリンタにそのまま (ESC/P 等、v2) |
| `PRINT DATA <id> <len>` + WDATA | len ≤ 宣言長 (1 要求 ≤ 64KB) | 200 / 409 (ジョブ無し・閉じ済み) | ジョブ本文。ホストはスプールに追記。大きい本文は要求を繰り返す |
| `PRINT CLOSE <id>` | — | 200 + `pages N` / 5xx | ホストが Windows の既定プリンタへ送る (§4)。`--to-file` 運転ならファイルに落とすだけ |
| `PRINT STATUS <id>` | — | `queued` / `printing` / `done` / `error <msg>` | 非同期の完了確認 |
| `CLIP GET` | — | 本文 = UTF-8 テキスト | **新規**。Windows クリップボードのテキストを読む (`edit` / 端末への貼り付け) |
| `CLIP PUT <len>` + WDATA | ≤ 4KB (v1) | 200 | OS32 側のテキストをホストのクリップボードへ |
| `PUT /file/<path> <len>` + WDATA | 許可リスト内 | **501 (v1)** / v1.4 で 200 / 403 | **先送り**。v1 は 501。ホストへのファイル書き出し (ログ、スクリーンショット) は v1.4 |

`GET` と `TIME` は既存のまま。要求名は大文字、引数は空白区切り、本文が要る要求は末尾の引数に
宣言長を書く — 新しい要求を足すときは Host Agent の `_service_*` を 1 本増やすだけで、OS32 側
(KAPI) は変わらない。分担: 基盤 (GET / TIME / PING / ECHO、Agent v2) = N1、PRINT / CLIP = N2。

## 3. KAPI (v51、末尾追記、[ABI1〜3])

**ABI の正典は TASK_N0 §1a** (ここは要約)。LINK_PLAN §5-1 の 4 本を、GUI の協調 (`sys_halt` /
`OP_WAIT` / `sys_yield` と共存する**非ブロッキング**) で確定し、第 5 の `host_write` を追加する
(印刷 / CLIP PUT / PUT の本文送信用)。

| slot | name | args | ret | 規則 (要約) |
|---|---|---|---|---|
| 208 | `host_open` | `const char *req, u32 len` | h (0 / 1) / 負 | 要求行 (1〜1400B、CPL=3 ポインタは v50 と同じ範囲検証) をカーネル領域へ写し REQUEST をキューへ。**送るだけで戻る**。同時ハンドルは **2** (ストリームは 1 本)。HELLO 未確立 / 再同期中 `STALE`、空き無し `FULL`、TX 満杯 `AGAIN`、ドライバ無効 `NOSYS` |
| 209 | `host_status` | `i32 h, u32 *status, u32 *length` | 0 / `AGAIN` / 負 | RESPONSE 未着なら **AGAIN** (呼び手は `sys_yield` / タイマで再試行)。着いていれば status と本文長 |
| 210 | `host_read` | `i32 h, void *buf, u32 cap` | 読んだ長さ / 0 = 完了 / `AGAIN` / 負 | 1 回 ≤ min(cap, リング可用, 1400)。最初に読んだ側がリングを close まで所有 (他方は AGAIN)。消費した分だけ credit が回復する |
| 211 | `host_write` | `i32 h, const void *buf, u32 len` | 受け付けた長さ / `AGAIN` / 負 | 宣言長のある要求だけ。REQUEST の転送 ACK 後、WDATA 1 本ずつ (≤ 1400B) 送り ACK を待つ間は AGAIN。宣言長超過 `INVAL` |
| 212 | `host_close` | `i32 h` | 0 / 負 | 任意の状態から解放し RELEASE を送る (転送 ACK まで再送)。`exec_reclaim_owned` で **owner 回収** (`host_owner_exit`) |

- エラー写像: 送信失敗 `OS32_ERR_IO` (-1)、引数不正 `INVAL` (-9)、未対応 `NOSYS` (-10)、待ち `AGAIN` (-14、K7 で取得済み)、リンク断 (HELLO 再同期中) / 再同期で失効したハンドル `STALE` (-11)、空き無し `FULL`。固有番号は取らない。
- **カーネル内の駆動**: プロトコルを進めるのは 100Hz の `link_tick` だけ (KAPI は状態を読み書きするだけで待たず、`link_tick` を呼ばない)。反射モード (`LGY98_FLAG_REFLECT`) では `link_tick` を起動しない。GUI 配下のアプリは T8 D8 のポーリング型 (WAIT_POLL) と同じ作法で待つ (`sys_yield` → 再試行)。CUI のコマンドは `sys_halt` ループ。
- 同期版 `link_service_get()` / L1 / L2 の自己試験は非同期 API の上に書き直す (KAPI にはしない、状態機械を進めるのは `link_tick` だけ)。
- データフィールド (`sbrk_heap_limit` / `shm_base`) は 0x35C / 0x360 へ移る。全体 clean rebuild (F0 の方針)。

## 4. 印刷の設計 (Host Agent 側)

- スプール: `<spool_dir>/<epoch>-<job_id>.txt` に `PRINT DATA` を追記 (再起動をまたいで一意)。`CLOSE` で確定。
- `text`: UTF-8 → ホスト側で描画。**v1 は Windows の既定プリンタへ `win32print` (pywin32) でテキスト印刷**、無ければ `--to-file` (`.txt` / `.pdf` へ) — 決裁 §9-1。日本語は Windows のフォントで出るので OS32 側にプリンタフォントは要らない。改ページは `\f`。
- `raw`: v2。ESC/P (PC-PR201) のバイト列をそのまま `RAW` で送る。OS32 側に ESC/P 生成が要るので後回し。
- 失敗 (プリンタ無し、スプール書けず) は `PRINT STATUS` の `error <msg>` と `CLOSE` の 5xx で返す。OS32 側は文言を表示するだけ。
- 単位: 1 ジョブ = 1 文書。ジョブ id はホストが振り `state_dir/job.txt` に永続 (成果物名は `<unixtime>-<job>.txt` で一意、TASK_N2 B4)。

## 5. OS32 側の利用者

| 利用者 | 内容 | レーン |
|---|---|---|
| `libos32host` (C、静的) | `host_get(url, sink)`, `host_print_text(name, text)`, `host_clip_get/put`, `host_time` — KAPI の AGAIN ループを隠す薄い層 | C |
| `wget <url> [file]` | GET → ファイルへ (進捗はバイト数)。`/bin` (cmds glob) | C |
| `lpr <file>` / `lpr -` (stdin) | テキスト印刷 | C |
| `hclip get` / `hclip put <file>` | クリップボード | C |
| `hdate` | `TIME` でホスト時刻を表示 (`date -sync` は内部コマンド影 + `rtc_write` 不在で不可、TASK_N3 B3。RTC 設定は v52 以降) | C |
| GUI: ファイラの「印刷」、端末の「コピー / 貼り付け」(CLIP)、edit の印刷 | libos32gui の末尾追記で `host_*` を公開 | W / apps |

## 6. セキュリティ・運用

- Host Agent は **許可リストの外のファイルを読み書きしない** (`--root <dir>`、既定は `agent_dir/share/`)。`PUT` は `--allow-put` で明示的に。
- `GET http(s)://` はホストのプロキシ設定に従う。TLS 検証はホスト任せ (OS32 は結果だけ)。
- 実機運用では同じ LAN の**任意のホスト**が EtherType 0x88B5 に応答できるので、v1 は「信頼できる LAN」前提。HELLO に共有トークンを載せる案は v2 (LINK_PLAN §6 の Host Agent の項に追記)。
- NP21/W の ini (`USELGY98` 等 5 キー) は設定済み。変更は [D2]。

## 7. 票 (実装単位、順序)

| 票 | 内容 | 依存 | 受入 |
|---|---|---|---|
| **N0** | KAPI_SPEC §3-2 の予約を v43 → **v51** に改訂 (v43 は欠番のまま「使わない」と明記)、本書 §3 の ABI 表を §1a 形式で確定、Codex 設計レビュー | — | 表の照合 |
| **N1 (K)** ✅ | v51 の 5 本、`net/link.c` の非ブロッキング化 (`link_request` の分割送信、状態機械)、`host_owner_exit`、ホスト TDD (`tools/tests/net_link_host.c` を L0〜L3 の試験から起こす)、`userland/tests/host_test.c` | N0、`kernel-lgy98-link` ビルド | `make check-net-l3` 相当を KAPI 経由で: GET /pattern/65536 の内容一致、404、TIME、AGAIN ループで WM が止まらない (GUI 配下で `gui_busy` と同時) |
| **N2 (ホスト)** ✅ | `host_agent.py` に `PRINT OPEN/DATA/CLOSE/STATUS`、`CLIP GET/PUT` (WSL2 は clip.exe/powershell、無ければ 503)、`--spool-dir`/`--print-dir`/`--printer`/`--clip`、win32print/win32clipboard は任意依存。**`PUT /file/` は 501 で先送り (v1.4)** | — | Python 単体試験 (スプール、宣言長と WDATA の照合、閉じたジョブへの DATA 409、許可リスト外 403) |
| **N3 (C)** ✅ | `libos32host` + `wget` / `lpr` / `hclip` / `date -sync` | N1、N2 | 端末 (GUI) と CUI の両方で `wget http://example.com/ /tmp/x` が 559B、`lpr /etc/profile` がホストの `spool/` に落ちる (to-file)、`hclip` の往復 |
| **N4 (W/apps)** ✅ | libos32gui 末尾追記、ファイラ「印刷」、端末のコピー / 貼り付け | N3 | GUI 受入 |
| **N5** | 実カード (M5) — FCS の有無、16KB RAM、8bit 転送、IRQ。実機の Windows 側は **Npcap + scapy** (raw Ethernet) で `host_agent.py` を動かす | 実機 | 実 LAN で N3 の受入 |

各票は T9 / S0 と同じ流儀 (設計 → Codex 網羅レビュー → worktree 実装 + ホスト TDD → Codex 実装レビュー → 配備 → 受入)。**LGY-98 有効カーネルは既定ビルドではない**ので、N1 以降の配備は `kernel-lgy98-link` を配備し、受入後に既定 (`LGY98=1` を常時) にするか決裁 §9-3。

## 8. メモリ・性能の見積り

- カーネル: `net/link.o` 10KB + ドライバ ≈ 20KB (既にビルド可)。KAPI 5 本 + 状態 ≈ 2KB。ストリーム 8KB は既存。
- アプリ側: `host_read` の buf だけ。`wget` は 4KB。
- 速度: L2 の実測は NP21/W で 128KB を欠落回復込みで完走。実機 LGY-98 (10Mbps、PIO) では 100〜300KB/s 程度の見込み (M5 で実測)。印刷ジョブは数十 KB なので十分。

## 9. ユーザー判断が要る点

1. **印刷 v1 の出口**: Windows 既定プリンタへ直接 (pywin32 依存) か、まず `--to-file` (txt/pdf) だけか。推奨: **to-file を既定、プリンタは任意 (pywin32 があれば)**。
2. **Host Agent の置き場**: エミュレータ運用は WSL2 (既存 `np2net_helper.py` と同居)、実機は Windows ネイティブ (Npcap)。両対応にするか、実機は後回しか。推奨: v1 は WSL2 のみ、N5 で Windows ネイティブ。
3. **LGY-98 を既定ビルドに入れるか** → **決定: 入れる (ユーザー決裁 2026-09-14)**。`build/config.mk` で既定 (stamp 無し) = LAN 有効・FLAGS 0。既定カーネルを NHD に配備しゲストで検証済み: kselftest 87/0、`hdate` / `wget /pattern/100000` (100000 B) / `wget http://example.com/` (559 B) が起動時自己試験なしで動作。カード未装着は無効化。無効ビルドは `make kernel-nolgy98`。
4. **KAPI 版**: v51 (S0 の v50 の次)。異論が無ければ N0 で予約表を改訂。
5. **範囲**: CLIP / PUT を v1 に含めるか (小さいが票が増える)。推奨: CLIP は含める (端末のコピー / 貼り付けに直結)、PUT は後回し。
6. **HTML の既定表現**: `text` (w3m -dump 相当) を既定、`mgx` は明示指定のときだけ (§2)。画像は帯域 (1 画面 0.5〜1 秒) と検索・選択不可の欠点があるため。headless ブラウザ依存は N2 で任意にする。
