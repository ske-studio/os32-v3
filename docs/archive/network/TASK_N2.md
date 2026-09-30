# TASK_N2 — Host Agent の PRINT / CLIP サービス (ホスト側 Python)

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

実装 e40a0a3、Fable 実装レビュー Approve (差分 blocker 0)。test 66/66、N1 TDD 35/35 回帰なし。ゲスト実サービス検証は N3 で**。正典: [HOST_SERVICES_PLAN.md](../../tasks/network/HOST_SERVICES_PLAN.md) §2 (サービス表) / §4 (印刷) / §6 (運用) / §9 (決裁)、ワイヤは [TASK_N0.md](TASK_N0.md) 第 5 版 §1b (v2、宣言長 + WDATA)。**OS32 側 (KAPI・カーネル) は変えない** — N1 の `host_open`/`host_write`/`host_read`/`host_status`/`host_close` (v51) と宣言長 WDATA でそのまま話す。利用する OS32 コマンド (`lpr`/`hclip`) は N3。

## 0. 範囲
`tools/host_agent.py` に**要求サービスを 6 本足す**だけ (ワイヤ・状態機械・rid 台帳・HELLO は N1 のまま不変)。CLIP は含める、**PUT は v1.4 へ先送り** (§9-5)。印刷は **to-file 既定、pywin32 は任意依存** (§9-1)。置き場は WSL2 のみ (§9-2、実機 Windows は N5)。

| 要求 | 引数 | 本文 | 応答本文 | 実装 |
|---|---|---|---|---|
| `PRINT OPEN <name> <kind>` | kind=`text` (v1) / `raw` → 501 | 無 | `job <id>` | ジョブ表に 1 本足しスプール **`<spool>/<epoch>-<job>.txt`** (再起動をまたいで一意、B4) を作る。id は `state_dir/job.txt` に永続する単調カウンタ。未知 kind → 400。`kind = parts[-1]` / `name = " ".join(parts[2:-1])` (name の空白を許す、nb7)。`<name>` はパスに使わない (ログ用) |
| `PRINT DATA <id> <len>` | **1 ≤ len ≤ 65536** | WDATA (宣言長) | 200 / 400 / 409 | `len` を `^[0-9]+$` で読み範囲外 (0 / 非数 / 超過) → **要求行の時点で 400** (WDATA 無し、B2)。宣言長ぶんを `_finish_body` で受け、ジョブが open ならスプールに追記 → 200。未知 / close 済み id → 409。**完了後・応答後も WDATA seq は必ず累積 ACK する** (本文は捨てる、B1) |
| `PRINT CLOSE <id>` | — | 無 | `pages <n>` / 500 (本文 `error <msg>`) | スプールを確定し**出力**: 既定 `--to-file` (`--print-dir` があれば一意名で移す、既定はスプールに残す)、`--printer` かつ win32print があれば既定プリンタへ。**ページ数 = `\f` の数 + 1、`\f` が無ければ `ceil(行数 / --lines-per-page(既定 60))`、空ジョブは 0**。失敗 → **500 + 本文 `error <msg>`** (B3)、job.state=error。未知 id → 409、`done` への再 CLOSE → 冪等 200 `pages n` |
| `PRINT STATUS <id>` | — | 無 | `queued` / `done` / `error <msg>` | 状態 (単一スレッドなので `printing` は観測されない)。open→`queued`、close 成功→`done`、失敗→`error <msg>` (200 の本文)。未知 id → 409 |
| `CLIP GET` | — | 無 | UTF-8 テキスト / 503 | クリップボードを読む (下の `--clip` バックエンド)。改行は CRLF→LF、64KB 超は切って 200。**バックエンドが無ければ 503** (空クリップの 200 + 空と区別、B5) |
| `CLIP PUT <len>` | **1 ≤ len ≤ 4096** | WDATA (宣言長) | 200 / 400 / 503 | 範囲外 → 要求行で 400 (B2)。テキストをクリップボードへ (LF→CRLF)。**バックエンドが無ければ 503、黙って捨てない** (B5)。完了後の WDATA も ACK (B1) |

`GET`/`TIME`/`PING`/`ECHO` は N1 のまま。要求名は大文字・空白区切り・本文が要る要求は末尾に宣言長 (N0 §1b の WDATA)。

## 1. Agent の実装 (`tools/host_agent.py`)
- **ジョブ表** `self.jobs = {id: {"kind","state","spool","pages","error"}}`、`self.next_job` は job.txt 永続。`state` = `open`/`done`/`error` (単一スレッド同期なので `closing`/`printing` は観測されない)。OPEN で作り、DATA で追記、CLOSE で確定。
- **要求行での 409 / 冪等** (往復 2 nb1/nb2/nb7): `PRINT DATA` / `PRINT CLOSE` / `PRINT STATUS` は**要求行 (本文を待つ前) に id を検査** — 未知 id → 409。`done`/`error` のジョブへの DATA → 409。**同一ジョブに本文未完了 (`resp is None` = RESPONSE 前) の DATA rid がある間の 2 本目の DATA / CLOSE → 409** (往復 3 B-2: 判定は「ACTIVE」ではなく「`resp is None` かつ同一 job」に限定する — 完了済み・未 RELEASE の DATA rid で CLOSE を 409 にしない。DATA ent に `job` を持たせ自 rid は除外)。中断経路 (RESPONSE 前に close) では RELEASE 着まで CLOSE が 409 になりうる (N3 は再試行で通る)。**`done` のジョブへの再 CLOSE は冪等に 200 `pages <n>`** (STALE 後の N3 の再試行を楽にする。未知 id の 409 と区別)。
- **要求の振り分け**: `_service_now` に OPEN/CLOSE/STATUS/CLIP GET を足す (小さい応答、即答)。`PRINT DATA` と `CLIP PUT` は**宣言長 + WDATA** なので `_service_now` では応答せず、既存の本文収集 (`_finish_body`) に載せて、受け切ってから追記 / クリップボード書き込み → 200 (N1 の ECHO と同じ経路)。要求行のパースは既存 `parts` を使う。
- **スプール**: `<spool-dir>/<unixtime>-<job>.txt` に追記 (一意、B4)。**追記失敗 (満杯・権限) → 500 + 本文 `error <msg>`、state=error** (nb5)。CLOSE で出力。`--to-file` (既定) はスプールを残す (`--print-dir` があれば `shutil.move` で一意名で移す — 別 FS の `EXDEV` に耐える、nb3)、`--printer` で win32print。改ページ `\f`。`--spool-dir` 既定は `<state-dir>/spool`、`--state-dir` 無し (試験) は `tempfile.mkdtemp` (nb6)。`error` ジョブへの再 CLOSE は**出力を再試行** (一時失敗の救済)、成功で `done`/`pages n`、失敗で再び 500 (nb4)。
- **クリップボード**: バックエンドは上の `--clip {auto,win32,wsl,file:<path>,none}` で選ぶ (`--clip-file` は廃止、`file:<path>` に統合)。改行は GET で CRLF→LF、PUT で (まず CRLF→LF に正規化してから) LF→CRLF。64KB 超の GET は **UTF-8 境界で**切って 200 (CLAUDE.md §4-27)。
- **境界**: 宣言長を取る全 verb (ECHO / PRINT DATA / CLIP PUT。PUT も) に一律で範囲検査 — PRINT DATA / ECHO は 1〜65536、CLIP PUT は 1〜4096。**0 / 非数 (`+5` `-5` `5_0` を含む — `int()` でなく `^[0-9]+$` で読む) / 上限超は要求行の時点で 400** (WDATA を待たない、B2。`ECHO 0` の永久 PROCESSING もこれで消える)。1 ジョブに上限は設けず 1 要求 ≤ 64KB を繰り返す。`_finish_body` は verb で分岐 (ECHO / PRINT DATA / CLIP PUT / それ以外 = 本文付き未知 verb は 501)。
- **引数**: `--spool-dir` (既定 `<state>/spool`)、`--print-dir` (to-file の出力先、既定 = spool のまま。移す名前は `<epoch>-<job>.txt` で一意)、`--printer` (フラグ、win32print で既定プリンタへ)、`--clip {auto,win32,wsl,file:<path>,none}` (既定 `auto` = win32 が import できれば win32、駄目なら WSL2 の `clip.exe`/`powershell` が居れば wsl、どちらも無ければ none=503)。既存の `--root`/`--file-root` は流用。`HostAgent.__init__` は `spool_dir`/`print_dir`/`clip`/`printer` を受ける (試験は CLI を通さない)。
- **CLIP バックエンド** (B5、往復 2 新 2 / 新 3): `win32` = `win32clipboard`。`wsl` = GET は `powershell.exe -NoProfile -Command "[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes([string](Get-Clipboard -Raw)))"` (空 / 非テキストは `[string]` cast で `""` → 200 + 0 長、503 にしない、nb1) の出力を base64 デコード (符号化 = PowerShell の OEM CP932 問題と末尾 CRLF の両方を回避)、PUT は `clip.exe` に **UTF-16LE + BOM** (`"\ufeff".encode("utf-16-le")` 前置、または `text.encode("utf-16")`) を stdin。`file:<path>` = 読み書き (試験・pywin32 無し運用)。`none` = 503。**subprocess は非同期** (往復 3 B-1): Agent は単一スレッドなので同期 `run` は他ハンドルの RTO 予算 (未 ACK の REQUEST/WDATA があると `LINK_RTO_TICKS 20 × LINK_TRIES 5 ≈ 1.2 秒`) を越えて再同期・スプール二重追記を起こす。→ `wsl` の powershell/clip.exe (と将来の `GET http` の urlopen) は **`Popen` で起動して `_serve` は `resp=None` のまま返す** (STATUS には既存の PROCESSING)。常駐ループを `select([sock], timeout≈0.05)` にして子の完了 / 4 秒期限を巡回で見る → 完了で `_answer`、期限超過は kill + 503、実行中に同 rid の RELEASE / epoch 切替が来たら結果を捨てる。例外種は `(OSError, TimeoutExpired, CalledProcessError, UnicodeDecodeError, ValueError, binascii.Error)` → 503 + 本文 (base64 復号の `binascii.Error` を含む、nb2)。試験は「子を進める」フックを 1 本足す。`win32print` 経路も同じ非同期に載せる。
- **B1 の ACK 規則 (往復 2 新 1)**: **ACTIVE な rid への WDATA は `decl` / `resp` に依らず ACK する** — `seq == last_seq + 1` なら `last_seq` を進めて累積 ACK、重複 (`seq ≤ last_seq`) も ACK。本文を `got` に足すのは **`resp is None and decl > 0`** のときだけ (要求行で 400 した rid = `decl == 0` でも WDATA は ACK し、本文は捨てる)。`_on_wdata` の早期 return は `decl == 0` を条件にしない (これが往復 1 の B1 が塞ぐはずの故障。例: `CLIP PUT 4097` → 400 → WDATA seq1 が届く → `ack=1` を返す、書かない)。これで「宣言 < 実」= 超過分は捨てて ACK・応答は 1 回、「宣言 > 実 (RELEASE で打ち切り)」= スプールは伸びず応答無し、が定まる。
- **B3 の配送規則**: `_answer` は `total > 0` なら status に依らず `deliver` を作る (200 限定をやめる)。409 / 500 / 503 に本文を付けても OS32 が WINDOW→DATA で読み切れる。既存 `GET /status/410` の本文もこれで読める (N1 の潜在バグ)。
- **B6 の既定**: `_finish_body` の既定 (未知 verb の本文) は **501** (200 をやめる)。先送りの `PUT /file/` は `_serve` で宣言長を受理しつつ即 501 (何も書かない)。

## 2. TDD (`tools/tests/test_host_agent.py` を拡張、Python 単体)
既存の贋 OS32 (フレームを直接組む) で:
- PRINT OPEN → `job 1`、DATA (宣言長 = WDATA 長) を 2 回追記 → スプールに連結、CLOSE → `pages n` + スプール内容一致。
- **宣言長と WDATA 長の不一致** (宣言 > 実 WDATA、宣言 < 実) の扱い (N1 の WDATA 契約に従う = Agent は宣言長ぶん受けてから応答)。
- 未知 id / CLOSE 済み id への DATA = 409、STATUS の状態 (queued/done/error)。
- CLIP PUT (宣言長 + WDATA) → `file:<path>` に書かれる、CLIP GET → `file:<path>` の内容を返す (往復)。4KB 超の CLIP PUT = 400。
- `--to-file` の CLOSE がスプールを残す / `--print-dir` へ移す。pywin32 が無い環境で全部通る (win32 系は import 失敗を許容)。
- **B1**: 完了後 / 400 後の WDATA seq を再送 → ACK(`ack=N`) が返り、スプールが二重に伸びず RESPONSE も再送されない (冪等)。最終 WDATA 再送でスプール不変。
- **B2**: `PRINT DATA 1 0` / `CLIP PUT 0` / `PRINT DATA 1 65537` / `CLIP PUT 4097` / `PRINT DATA 1 +5` → 業務 400 が WDATA 無しで即返る。
- **B3**: 409 / 500 / 503 に本文を付けた応答を FakeOS32 が WINDOW→DATA/EOF で読み切る。既存 `GET /status/410` の `gone body` を実際に読む。
- **B4**: state_dir を共有した Agent を 2 回起動し各 1 ジョブ印刷 → 出力ファイルが 2 つ両方残る (名前衝突なし)。
- **B5**: `--clip none` で GET/PUT が 503。`--clip file:<p>` で往復一致。`--clip wsl` は `clip.exe`/`powershell` を subprocess スタブに差し替えて 1 本。
- **B6**: `PUT /file/x 3` + WDATA 3B → 501、ファイルは作られない。
- **並行 / 冪等**: 2 ジョブ同時 open で交互 DATA が正しいスプールへ。重複 REQUEST / CLOSE の RESPONSE 消失 → STATUS 再提示で再印刷しない。
- **新 1**: `CLIP PUT 4097` → 400 → WDATA seq1 → `ack=1` が返り RESPONSE 再送なし・書かれない。`ECHO 0` → 400。
- **新 2/新 3**: `--clip wsl` を subprocess スタブに差し替え、GET は base64 経路 (日本語 1 本のバイト列)、PUT は UTF-16LE+BOM の stdin を検証。スタブが `TimeoutExpired` → 503 + 本文、Agent は次の要求に答える。
- **nb1/nb2/nb7**: 同一ジョブに DATA (未完了) と CLOSE を同時 → CLOSE が 409。`done` への再 CLOSE → 200 `pages n` (冪等)。未知 id の DATA/CLOSE/STATUS → 要求行で 409。
- **B4**: state_dir 共有で Agent 2 回起動 → 各 1 ジョブ、2 台目が `job 2`、出力ファイル 2 つ両方一意名で残る。
- **切り詰め / 改行**: CLIP GET の CRLF→LF・64KB を UTF-8 境界で切る、CLIP PUT の CRLF 正規化。`PRINT OPEN x raw` → 501、未知 kind → 400、空ジョブ CLOSE の `pages`。
- **B-1 (非同期)**: 子プロセスが未完了の間に別 rid の REQUEST/WDATA を送って ACK と応答が即返る、同 rid の STATUS が PROCESSING、子の完了で RESPONSE が 1 回、完了前の RELEASE で結果を捨てる、期限超過で 503 + 本文。
- **B-2**: DATA 完了 (200 応答済み) だが RELEASE 未着 → 同ジョブの CLOSE が 200 `pages n`。DATA 本文未完了 (WDATA 途中) → CLOSE が 409、RELEASE 後の再 CLOSE が 200。
- **nb1/nb2**: CLIP GET の stdout 空 → 200 + 0 長 (503 でない)、非 base64 → 503。`error` ジョブへの再 CLOSE (nb4)、追記失敗 → 500 (nb5)。
- 既存 25 ケース (HELLO / 台帳 / 墓標 / 枯渇) に回帰なし。`HostAgent.__init__` の試験用引数 (`spool_dir`/`print_dir`/`clip`/`printer`)、**`clip` の既定は `none`** (試験・CI が実クリップボードに触れない。`auto` は CLI 既定だけ)。

## 3. 受入 (PM)
`python3 -B tools/tests/test_host_agent.py` 全通過。ゲスト受入は N3 (`lpr`/`hclip` 実装後) で実サービスとして: 端末から `lpr /etc/profile` がホストの spool/print-dir に落ちる、`hclip` の往復。N2 単体はホスト Python 試験まで。

## 4. レビューで見てほしい点 (設計)
1. ジョブ表と `_finish_body` の分岐が N1 の WDATA 契約 (宣言長ぶん受けて 200) を壊さないか。
2. pywin32 を任意依存にし `--clip-file` / `--to-file` で pywin32 無しでも完結する設計が妥当か。
3. 409 / 400 / 5xx の返し方が業務結果 (RESPONSE flags 0) で、N1 の制御結果 (flags bit0) と混ざらないか。
4. スプールの並行 (複数ジョブ同時 open、同一ジョブへの並行 DATA の順序、CLOSE と後着 DATA の競合) と、再起動をまたぐ成果物名の一意性 (B4)。
5. B1 の「完了後も ACK」がカーネルの再送を止めるのに十分か (カーネル `link_on_response` が保留 WDATA を無効化しない N1 潜在バグは別記 — B1 の Agent 側修正で実害は消えるが、将来の N-fix 候補として §5 に残す)。
6. CLIP の WSL2 バックエンド (`clip.exe` / `powershell`) の文字コード (UTF-16LE) と、それでも実クリップボードは N2 の受入外 (試験は file バックエンド) でよいか。

## 5. N1 の潜在バグ (N2 では触らない、将来の N-fix 候補)
往復 2 のレビューが既存 `tools/host_agent.py` / `net/link.c` に見つけたもの。N2 の範囲外だが記録する:
- **(a、優先度高) `GET /file/` のパストラバーサル**: Agent の `_service_get` は `os.path.join(root, path.lstrip("/"))` で `..` を通し、`--file-root` 未指定だと**ホストの任意ファイルが読める** (HOST_SERVICES_PLAN §6 の「許可リスト外は読まない」に反する)。N2 で `/file/` を触らないが、**別の小 N-fix で `--file-root` 必須化 + `..` 正規化拒否**をする (優先度高)。本票の実装コーダーは触らない。
- **(b) `link_on_response` が保留 WDATA を無効化しない**: 最終 WDATA の ACK が落ちると RESPONSE 受信後もカーネルが WDATA を RTO 再送し続ける。B1 で Agent が完了後も累積 ACK を返すことで実害 (再同期) は消えるが、根治は「RESPONSE 着後は同 rid の WDATA 再送を止める」カーネル側の 1 行。N3 着手時にホスト TDD で踏んで判断。
- **(c) `GET http(s)://` の `urlopen(timeout=10)`** が他ハンドル在庫時の RTO 予算 (~1.2s) を超え再同期を起こす。N3 の wget 実装時に **B-1 と同じ非同期機構に載せる** (timeout だけでは不足)。

## 6. 実装記録 (2026-09-14)
- 着地: `host_agent.py` + `test_host_agent.py` の 2 本のみ (OS32 側不変)。`python3 -B tools/tests/test_host_agent.py` **57/57 PASS** (N2 新規 32 + 既存 25 回帰なし)。
- 非同期化 (B-1): 常駐ループを `select(..., 0.05)` + 毎周 `tick`、子は `spawn`→`_RealProc` (Popen 薄包み)、`_start_async` で `pending[rid]` に積み `_serve` は resp=None (STATUS は PROCESSING)、`tick` が完了で `_answer` / 期限超過で kill+503 / RELEASE・epoch 切替で破棄。例外種は `SUBPROC_ERRORS`。試験は `spawn` を `FakeSpawn` に差し替え `FakeProc.step()` で子を進める。
- **PM 判断が要った点 (受入)**: `win32print` / `win32clipboard` 経路は**同期のまま** (pywin32 は任意依存・WSL2 では import 不可・既定は to-file で RTO 懸念なし・`--printer` は既定オフ)。非同期機構は subprocess 前提で、win32print (Python API 直呼び) を載せるには別途スレッド化が要る。→ **WSL2 の CLIP (clip.exe/powershell) という B-1 の主対象は非同期化済み**なので受入可。**win32print の非同期化は N5 (実機 Windows ネイティブ) で判断** (そこで pywin32 が実在し `--printer` が実経路になる)。§1 の「win32print も同じ非同期に」はこの範囲で読み替える。
- 未確認 (机上): 実 Windows の clip.exe/powershell (WSL 上は subprocess スタブで検証)。実クリップボードは N3/N5 の受入で。

## 7. 実装レビュー (Fable、2026-09-14) → N2-fix
判定 Request changes、blocker 1 件 (実測)。既存 57/57 + N1 ホスト TDD 35/35 は回帰なし。
- **blocker B7 (実測)**: `_RealProc` が `stdout=PIPE` で `poll()` が非 None になるまで読まないため、**約 49KB 超のクリップボード** (base64 後 >64KB = Linux パイプ容量) で子がブロック → 4 秒で kill → 503。設計は「49〜64KB は 200、64KB 超は切って 200」。→ `stdout=tempfile.TemporaryFile()` にして `output()` で `seek(0);read()` (パイプ容量非依存)。受入: **実 `_RealProc`** (FakeProc でなく) で ≥100KB を吐く子が期限内に完了、Agent 経由で 60000B→(200,60000)、100KB→(200,≤65536,UTF-8 境界)。FakeProc はパイプ背圧を模さないのでこの 1 本は実子プロセスで。
- **nb (直す)**: (a) `_finish_clip_get` が rc を見ず、powershell が rc≠0 + stdout 空だと 200+0 長に化ける (B5「黙って捨てない」に反する) → `poll()!=0 → 503`。(b) `--clip` の未知値が検証されず wsl 扱い → argparse 後に `{auto,win32,wsl,none}` か `file:` 接頭辞かを検査して exit。(c) `_ensure_jobs`/`alloc()` の `os.makedirs`/`write_int_atomic` が try 外で、spool/state が書けないと PRINT OPEN で Agent が落ちる → OSError を 500+`error` に。(d) `now=time.time` → `time.monotonic` (NTP ジャンプ耐性)。
- **nb (試験を足す)**: epoch/sess 切替での pending 破棄、実 `_RealProc` を使う大容量 clip 1 本、powershell 引数本文 (base64 ラッパ) の照合。
- **nb (残す・N5 申し送り)**: kill 後の未 wait (次 Popen まで 1 個ゾンビ、有界)。`_print_win32` は RAW datatype で UTF-8 を書く → 日本語が出ない (TEXT+CP932 か GDI 描画が要る)。`pywintypes.error` が except に掛からず落ちる。いずれも `--printer` 実経路 = 実機 Windows なので **N5** で直す (§6)。

## 8. N2-fix 実装記録 (2026-09-14)
- blocker B7: `_RealProc` を `stdout=tempfile.TemporaryFile()` + `output()` で `seek(0);read()` (パイプ容量非依存)。nb: `_finish_clip_get` は rc≠0 → CalledProcessError → 503、`--clip` は `valid_clip_arg` で argparse 後に検証、`_ensure_jobs`/`alloc` の OSError → 500+error、期限時計 `time.monotonic`。試験: 実 `_RealProc` の大容量 (≥100KB / 60000B / UTF-8 境界)、epoch/sess 切替の pending 破棄、`--clip bogus`、powershell 引数照合。
- `python3 -B tools/tests/test_host_agent.py` **66/66 PASS**、N1 ホスト TDD `test_net_link.py` 35/35 回帰なし。
- PM 補足: コーダーが API timeout で中断し `n2fix_sess_switch_discards_pending` が未完 (clip=none で spawn せず IndexError) だったので、PM が 1 行 (`clip="wsl"`) を補って 66/66 に。エージェント落ちの後始末。
- N5 申し送り (`--printer` 実経路): `_print_win32` の RAW datatype → 日本語不可、`pywintypes.error` の except 漏れ、kill 後の未 wait。§7 のとおり実機 Windows で。

## 9. 受入 (2026-09-14) と残 non-blocker
Fable 実装レビュー (差分 `69e2906..e40a0a3`) **Approve**、blocker 0。B7 は変異試験 (PIPE に戻すとハング) で本物の背圧を踏むことを確認。**N2 受入完了** (ホスト側 Python、ゲスト実サービスは N3)。
残 non-blocker (N3 の /file/ N-fix と同じ Agent 触りでまとめる): (1) kill/rc≠0 経路で `_outfile` を明示 close (fd 衛生)、(2) `--clip file:` の空パス拒否 (`valid_clip_arg`)、(3) `RealB64Spawn.wait_all()` に timeout (B7 再発時にスイートがハングしないよう)、(4) `n2fix_b7_real_large_clip_get` の「子未完了」assert の時間依存を外す、(5) `n2fix_c_open_oserror_500` を root 実行時 skip、(6) rc≠0 の 503 本文に powershell stderr を載せる (診断性)。
