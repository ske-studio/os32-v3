# TASK_N3 — libos32host と wget / lpr / hclip / date -sync (OS32 側 C)

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

ゲストで wget/lpr/hclip/hdate 実動、F6 解決 (実サービスの wget は >64KB 完走)。Fable 実装レビュー Approve。N3-fix 着地済み (test 硬化 + /file/ NUL 修正、host_lib 82/82 host_agent 81/81)**。正典: [HOST_SERVICES_PLAN.md](../../tasks/network/HOST_SERVICES_PLAN.md) §2 (サービス) / §5 (利用者)、ワイヤ [TASK_N0.md](TASK_N0.md) 第 5 版 §1a (host_open/status/read/write/close の ABI)。依存: N1 (KAPI v51、受入済み)、N2 (Agent の PRINT/CLIP、受入済み)。**KAPI は変えない** (v51 のまま)。同梱: F6 の wget 再確認 (§6)、Agent の `/file/` トラバーサル N-fix + N2 残 non-blocker (§7)。

## 0. 範囲
- **`libos32host`** (`userland/lib/host/`、C 静的、`build/libs.mk` の `DEFINE_LIB`): host_* KAPI の AGAIN ループ (`sys_yield`) と多段のサービス手順を隠す薄い層。GUI 配下 (park) でも CUI (sys_halt 相当) でも `sys_yield` で待つ (host_test.c と同じ作法、K7/T8 で確立)。
- **コマンド** (`userland/cmds/`、`/bin` = deploy.yaml の cmds glob、api 51): `wget`、`lpr`、`hclip`、`hdate`。いずれも libos32host の薄い CLI。
- **ホスト TDD** (`tools/tests/`): libos32host を贋 KAPI で (`cfg_host.c` の作法)。
- **ゲスト受入**: §6 (PM/テスター、kernel-lgy98-link + host_agent v2)。

## 1. libos32host の API (`userland/lib/host/libos32host.h`)
`kapi` は crt0/リンク時に渡る (既存 lib と同じ)。全関数は**リンクが立っていること**を内部で待つ (STALE が続けば `HOST_ELINK`)。戻り値は 0 / 負の `HOST_E*` (下記)。業務ステータス (HTTP の 404 等) は out 引数で返す (エラーにしない)。

| 関数 | 動作 |
|---|---|
| `int host_time(char out[20])` | `host_open("TIME")` → status ループ → read 19B → close。`out` は NUL 終端。0 / 負 |
| `int host_get(const char *url, host_sink_fn sink, void *ud, int *http_status, u32 *nbytes)` | `host_open("GET <url>")` → status ループ → **`*http_status` を read ループの前に書く** (wget が非 200 のとき sink 前に判るように) → read ループ (AGAIN→yield、0=完了、>0 は `sink(ud, buf, n)`) → close。`sink` が非 0 → `HOST_EABORT`。本文をバッファに溜めない (>64KB でもメモリ一定、F6 の実サービス経路) |
| `int host_print_text(const char *name, const char *buf, u32 len, u32 *pages)` | `PRINT OPEN <name> text` → read `job <id>` → `PRINT DATA <id> <n>` を 1400B 単位で write 分割 (各 status 200 を確認) → `PRINT CLOSE <id>` → read `pages <n>`。id は内部で持つ。業務 500/409 は `HOST_ESERVICE` + `*pages` 据置き |
| `int host_print_stream(const char *name, host_source_fn src, void *ud, u32 *pages)` | 同上だが本文を `src(ud, buf, cap)` から引く (lpr - / 大きいファイル用、メモリ一定) |
| `int host_clip_get(host_sink_fn sink, void *ud, u32 *nbytes)` | `CLIP GET` → read ループ → sink。503 = `HOST_ESERVICE` |
| `int host_clip_put(const char *buf, u32 len)` | `CLIP PUT <len>` (len ≤ 4096、超過は `HOST_EINVAL`) → write → status 200 |

- **宣言長規則 (最終レビュー blocker、必須)**: (i) 1 要求の宣言長は **1〜65536** (Agent の `DECL_MAX_BYTES`)。`host_print_text` は 65536 を超える本文をこの単位に分割する (70KB なら DATA 2 本 = 65536 + 残り)。(ii) `host_print_stream` は内部バッファ (16KB) を **src が 0 (EOF) を返すか満杯になるまで詰めてから、その実長**で `PRINT DATA <id> <実長>` を open し、**宣言長ぶんを必ず write する** (宣言してから src を読まない — `lpr -` の stdin が行単位の短い read を返しても宣言 > 実にならない)。(iii) 詰めた長さ 0 (即 EOF) なら DATA を出さず CLOSE へ。(iv) src が負を返したら `HOST_EABORT` で後始末 (CLOSE 送らず)。
- **多段の後始末 (N-3)**: OPEN→DATA×n→CLOSE の途中で STALE / 500 / 409 が起きたら **CLOSE を送らず job を放置** (部分文書を印刷しない。Agent は job 数無制限・`_job_pending_data` の ACTIVE は epoch 切替で消えるので放置は安全、スプールがホストに残るだけ)。開いた host ハンドルは**全経路で close** (STALE の close も 0 を返す)。プロセス死は `exec_reclaim_owned`→`host_owner_exit` が回収。
- **AGAIN ループ (B1 込み)**: **`host_open` も `AGAIN` / `FULL` の間は `sys_yield` で待つ** (`link_host_close` の RELEASE が未 ACK の間 `rel_pending` で open が AGAIN を返すため — 多段 PRINT の close 直後 open がほぼ確実に踏む。カーネルの `link_wait_open` / host_test.c の `wait_up` と同形)。status / read / write も同じく `sys_yield`。`link_tick` が進める。
- **STALE / 生存性**: **最初の open だけ上限付き (既定 3 秒 = **`get_tick` の 100Hz `tick_count` で計る** — `sys_time` は RTC のシリアル読みで重い) で STALE を待ち** (リンク確立待ち)、それ以降の STALE は即 `HOST_ELINK`。PROCESSING を返し続ける Agent への出口として**無進捗期限 (既定 30 秒、`get_tick` で計る)** — `host_status` が 0 を返す / `host_read > 0` / `host_write > 0` / 各段の open 成功のたびに基準を取り直し、**AGAIN / FULL / PROCESSING しか返らない状態が 30 秒続いたときだけ** `HOST_ETIMEOUT` (絶対期限にすると大きい転送 = §6 の 64KB 超・10MB wget を殺すため。Agent 死は probe→STALE→ELINK が別に拾う)。`NOSYS` (NIC 無し) は `HOST_ENODEV` (ELINK と別文言)。
- **エラー**: `HOST_ELINK` (STALE/未確立)、`HOST_ENODEV` (NIC 無し = NOSYS)、`HOST_ETIMEOUT` (全体期限)、`HOST_ESERVICE` (print/clip の 409/500/503 — `*svc_status` に業務値、診断本文の先頭を小 sink で受けてよい)、`HOST_EINVAL`、`HOST_EIO` (予期しない負値)、`HOST_EABORT` (sink/src 中断)。print/clip 系の関数は `u32 *svc_status` (NULL 可) を揃える。定数は `libos32host.h` (3 層)。
- **要求行の組み立て**: `host_open` は 1〜1400B。`GET <url>` の url が長いと 1400B 超 → `HOST_EINVAL`。宣言長は 10 進で付ける (`PRINT DATA 7 4096`)。

## 2. コマンド (`userland/cmds/`)

**終了コード表 (4 コマンド共通、N-3)**: 0 = 成功、1 = 業務失敗 (非 200 の http_status、print/clip の ESERVICE)、2 = リンク (`HOST_ELINK` / `HOST_ENODEV` / `HOST_ETIMEOUT`)、3 = usage / 引数 (`HOST_EINVAL`: url 1400B 超・clip 4096 超・hclip put 空)、4 = ローカル I/O (sink の `sys_write` 失敗・src の `sys_read` 失敗 = `HOST_EABORT`/`HOST_EIO`)。文言は `<cmd>: <理由>`。

- **`wget <url> [file]`**: `host_get`。**http_status を確定してからファイルを作る** (404 で空ファイルを残さない、nb4)。file 指定なら `sys_open(O_CREAT|O_TRUNC)` して sink が write (短書き込みはループ)、無ければ stdout。完了で `<n> bytes`。http_status 200 以外は `wget: <status>` + 終了 1。`HOST_ELINK` → `wget: host link down`、`HOST_ETIMEOUT` → `wget: host timeout` (FULL でハンドル占有の誤診を避ける)、いずれも終了 2。終了コード: 0 成功 / 1 業務 (非 200) / 2 リンク / 3 usage。
- **`lpr <file>` / `lpr -`**: file を `host_print_stream` (src = ファイル / stdin を読む)。完了で `printed, <pages> pages`。名前は basename の空白・制御文字を `_` に置換 (Agent は `line.split()` で kind がずれるため)、`-` は `stdin`。
- **`hclip get` / `hclip put <file>`**: get = `host_clip_get` (sink = stdout)。put = ファイル (1〜4096、空は `HOST_EINVAL`) を `host_clip_put`。
- **`hdate`** (新規、`hclip` と同流儀): `host_time` でホスト時刻を 1 行表示。**`date -sync` にしない** (B3: `date` は常駐シェル / sh.bin の内部コマンドで外部 `date.bin` を影にする、かつ KAPI に `rtc_write` が無く RTC は書けない)。**RTC 設定はしない (表示のみ)** — `rtc_write` は v52 以降、内部 `date` への `-sync` 統合は将来のシェル票。票から `date` 拡張と「deploy.yaml の date は既存」を撤回。

## 3. 登録
`build/libs.mk` (`DEFINE_LIB libos32host`)、`build/programs.mk` (4 コマンドが libos32host.a をリンク — cmds の明示規則、tar.elf と同じ形)、`build/app.conf` (wget/lpr/hclip/hdate api 51)、`userland/deploy.yaml` (cmds glob `/bin` で自動)、`docs/07_shell.md` にコマンド追記、`CLAUDE.md` の「21 commands」→ **25**、`docs/INDEX.md` 等の数値。

## 4. ホスト TDD
`tools/tests/host_lib_host.c` + `test_host_lib.py`: libos32host を `#include` し、host_* を**贋 KAPI** (状態機械: open→AGAIN×k→status→read チャンク→close をスクリプトで与える) に差し替え。踏む: get の AGAIN ループと sink 呼び出し・>64KB のストリーミング (メモリ一定)・http_status の受け渡し・sink 中断、print_text/stream の多段 (job 採番→DATA 分割→CLOSE→pages)・500/409、clip get/put・503・4096 超、STALE で `HOST_ELINK`、url 1400B 超で `HOST_EINVAL`。コマンドは `-DHOST_TEST` で引数処理・終了コード表を (stat/tar と同じ作法で) 単体試験。**追加で踏む** (贋 `sys_time` を進める仕掛けで): open STALE×k→3 秒内 UP で成功 / 3 秒超で ELINK / 2 段目以降の STALE 即 ELINK / AGAIN のみ→ETIMEOUT (無進捗) / NOSYS→ENODEV、write INVAL の 3 分岐 (status 409/AGAIN/STALE)、両スロット rel_pending→open AGAIN→yield、print_stream の短い src read と最終チャンク・print_text の 64KB 超分割、hdate。Agent の GET が 4 秒超でも 200・404 fixture・rc≠0→502 (N-2、Python 試験)。**無進捗期限の肯定側**: 進捗が 20 秒間隔で来る 60 秒転送が成功する (read>0 / write>0 / status 0 / open 成功の 4 経路で基準取り直し、絶対期限に戻す変異で落ちる)。多段の後始末: 409 の後に CLOSE が出ない + close 回数 == open 回数。宣言長: src が 1 バイトずつ + 途中 EOF で宣言長 == write 合計、70KB print_text で DATA 2 本。

## 5. レビューで見てほしい点 (設計)
1. libos32host の AGAIN/yield ループが GUI (park) と CUI の両方で正しく待ち、STALE で無限ループしないか。
2. `host_get` / `host_print_stream` が本文をバッファに溜めず sink/src でストリーミングし、>64KB (F6) と大きいファイルでメモリ一定か。
3. print の多段 (OPEN→DATA×n→CLOSE) の途中失敗 (STALE / 500 / 409) の後始末 (開いた job をどうするか、ハンドルの close)。
4. 業務ステータス (get の 404、print/clip の 4xx/5xx) の呼び手への伝え方 (get は out、他は `HOST_ESERVICE`) が一貫か。
5. §7 の N-fix (Agent `/file/` トラバーサル) の直し方。

## 6. F6 の実サービス再確認 (受入で)
N1 の F6 (自己試験 L1/L2 が 64KB 超で 66114 B 停止) は「ゲスト固有のタイミング、コードは >64KB を扱える (ホスト TDD 35/35)」で保留した。N3 の受入で **実サービスの wget で 64KB 超を落として確認**: ゲストで `wget http://<64KB 超のファイル> /tmp/big` または Agent の `GET /pattern/200000` (host_get 経由) を流し、(a) 全量が落ちるか、(b) L1/L2 と同じ ~66114 B で切れるか。切れれば F6 は実害 → `link_tick` と consume の競合を計装して深掘り (別 N-fix)。切れなければ F6 は自己試験固有 (link_l1_bulk/link_l2_stream の LINK_IDLE ループ) の artifact として片付け、票に記録。

## 7. 同梱 N-fix (Agent、ホスト Python)
N3 の実装で Agent (`tools/host_agent.py`) を触るので、以下をまとめて直す (TASK_N2 §5(a)/(c) / §9):
- **`GET http(s)://` の非同期化 (B4、優先度高)**: 現状 `urlopen(timeout=10)` が主ループを塞ぎ、~6 秒超で `link_resync` → wget が「host link down」になり F6 の再確認も偽陰性になる。→ N2 の B-1 と同じ子プロセス/非同期経路に載せるが、**期限は要求種別ごと** (N-2: `_start_async(..., timeout=...)`、CLIP は 4 秒のまま、**GET は 25 秒** = ライブラリの無進捗期限 30 秒より短くして利用者が ETIMEOUT でなく業務 **503** (Agent の期限超過は `error timeout`) を見る)。子の形: `sys.executable -c` で urllib を回し、**`r.read()` で読み切ってから** status 行 + 本文を `_RealProc` の TemporaryFile へ (B7 と同じ。途中断を「200 + 短い本文」にしない — 子が rc≠0 なら Agent は status 行があっても 502)。**`urllib.error.HTTPError` は `e.code` + `e.read()` を status 行 + 本文にする** (404 を 502 にしない)、`allow_net`/`--offline` の 502 は維持。Python 試験: **5 秒遅延**の HTTP fixture で STATUS→PROCESSING を挟んで 200 (4 秒で切れないこと)、`subproc_timeout` を 1 秒に下げても GET は切れず CLIP は切れる。
- **`GET /file/` のパストラバーサル (優先度高)**: `--file-root` 未指定なら `/file/` は 403、指定時も `os.path.realpath(join)` が `os.path.commonpath([realpath(root), realpath(join)])` == `realpath(root)` に収まることを確認 (`..`・symlink 脱出を落とす、前方一致 `/root2` を通さない)。ホスト Python 試験。
- N2 の残 non-blocker 6 件 (TASK_N2 §9): `_outfile` の明示 close、`--clip file:` 空パス拒否、`RealB64Spawn.wait_all()` timeout、B7 試験の「子未完了」assert 時間依存、root 実行時の OSError 試験 skip、rc≠0 の 503 本文に stderr。

## 8. 実装レビュー (Fable、2026-09-14) — Approve
判定 **Approve**、blocker 0。レビュアーは GET_CHILD を実サーバで実行 (200 で 200000B 読み切り / 404 本文 rc0 / 接続不能 rc1)、lib 変異 3 本で試験の抜け道を確認。ビルド all/external/check 緑、lib 70/70・コマンド 40・test_host_agent 79/79。
**N3-fix (受入と並行、次の Agent 触りでまとめる)**:
- (1、強く推奨) lib ホスト TDD の抜け道: 贋 `host_write` に「最初の k 回 AGAIN」欄・`open_full` ケース・write 経路の無進捗変異を足す (実カーネルは REQUEST ACK まで必ず write AGAIN なので、この欠陥は本番で全滅するのに 70/70 が通る)。
- (2、強く推奨) `GET_CHILD` が試験で一度も実行されない (全 GET が FakeProc)。`http.server` fixture で実子を 1 本。
- (3、guest 到達の Agent クラッシュ) `_service_get_file` の `realpath` が try 外で、`GET /file/a\0b` (NUL) の `ValueError` 未捕捉 → 主ループは ConnectionError しか受けず Agent が落ちる。→ `realpath` を try に入れ `(ValueError, OSError)` → 403。
- (4〜7、記録) `g_link_up` プロセス大域は W レーンで再検討 / wget の stdout モードは file 指定時のみ進捗 / 転送途中 ELINK で部分ファイル / hdate の ESERVICE 文言 / tick poll 例外の close / hclip get 試験の stdout 漏れ。

## 9. ゲスト受入 (2026-09-14) — 合格
kernel-lgy98-link + host_agent v2 (--file-root / --clip file: / net 可) で:
- `hdate` → ホスト時刻 1 行。
- **`wget /pattern/200000 /tmp/big` → 200000 B 完走**、先頭 `00 01 02 03 …` = 正しいパターン。**F6 解決**: 実サービスの host_read 経路は >64KB を完走する。F6 は L1/L2 自己試験 (`link_l1_bulk`/`link_l2_stream` の LINK_IDLE ループ) 固有の artifact で、製品経路の欠陥ではなかった。
- `wget /pattern/65536 /tmp/mid` → 65536 B。`wget http://example.com/ /tmp/ex` → 559 B、実 Example Domain の HTML (Agent の非同期子経由)。
- `lpr /etc/settings.tsv` → `printed, 1 pages`、ホスト spool に 1406 B・内容一致。
- `hclip put /tmp/cb.txt` → 18 B、ホスト clip file に "clipboard-test-98"、`hclip get` で往復一致。`hclip put /etc/system.cfg` → 15 B。
カーネルは N1-fix のまま (再配備不要)、新コマンドは HostDrv → `hsync` で /bin に配布。**N3 受入完了**。
