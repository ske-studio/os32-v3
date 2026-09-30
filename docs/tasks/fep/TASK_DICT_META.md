# TASK_DICT_META — FEP 辞書のメタ情報と、学習データの別ファイル化

> 発行: PM (Claude Code `claude-fable-5-1`、2026-09-30) / 状態: **計画 (2026-09-30)** — ユーザー決定 (2026-09-30、[`../v3/TASK_MEMMAP_V3.md`](../v3/TASK_MEMMAP_V3.md) D27・D28) を記録した段。**着手は v3 の後の方** ([`../v3/V3_PLAN_DRAFT.md`](../v3/V3_PLAN_DRAFT.md) §3 P10「v3 後半」)。設計票の往復・実装は未着手。
> 出所: ユーザー「辞書のメタ情報と学習データを辞書と別ファイルに分ける — 採用。ただしすぐ取り掛からず v3 の後の方でよい」(2026-09-30、TASK_MEMMAP_V3 §11-3 の「ユーザー決定 (池の運用規則まわり)」7)。
> 正典の関係: SQLite の予算と FEP の取り分 (`mem_reserve_kb` の使い道) は TASK_MEMMAP_V3 §4-6 (D27)。FEP のアーキテクチャの快照は [`FEP_STATUS.md`](FEP_STATUS.md)、フェーズ別の設計は [`00_INDEX.md`](00_INDEX.md)。辞書の品質 (コスト式・頻度) は [`04_DICT_QUALITY.md`](04_DICT_QUALITY.md) の領分で、この票は**入れ物 (メタ情報と学習の置き場)** だけを扱う。
> 本文の `file:line` は `feat/gui` 5d79e5dd の行。**数字は 2026-09-30 に手元の `assets/fep.db` を Python の `sqlite3` で照合した実測**。

---

## 0. 決定 (ユーザー 2026-09-30)

| # | 決定 | 出所 |
|---|---|---|
| M1 | 辞書 (SQLite DB) に**メタ情報**を持たせる。項目は §2 の表 | TASK_MEMMAP_V3 D28 |
| M2 | **学習データを辞書と別ファイルに分ける** (例 `/etc/fep_user.db`)。理由は §3 | 同 D28 |
| M3 | 辞書のメタ情報 `mem_reserve_kb` が **FEP の SQLite 取り分** (起動時に優先確保) の出典。settings で上書き可。事前確保量を超える辞書への入れ替えは**再起動で再確保** | 同 D27、§4-6 |
| M4 | **着手は v3 の後の方** — TASK_MEMMAP_V3 の T4 (SQLite モジュール化) と T5a (FEP モジュール化) の後。それまで FEP は今の `fep.db` (メタ無し、学習表同居) のまま動き、`mem_reserve_kb` は既定値 + settings だけ | ユーザー (同日)、V3_PLAN_DRAFT §3 P10 |
| M5 | `license` / `attribution` は**必須項目** — 辞書はパブリックの `os32-v3` で配るので、元データ (IPADIC) の配布条件をファイル自身が運ぶ | PM (M1 の項目の中で必須に格上げ。理由は §2) |
| M6 | **学習データの移行は二段** — fork 直後は既存データを保持した媒体で v3 のシステム一式だけ更新し (`fep.db` の `dict_user` を配備で消さない)、この票で旧 `dict_user` を `fep_user.db` へ移す (件数・内容の照合、再実行の二重加算防止、途中失敗の復旧、S/M/L の `dict_id` 対応)。`fep_user.db` は `hsync` の保護名に。v2.x の戻り先は旧データの写しも保持 (§3 末尾) | ユーザー (2026-09-30、Codex X8 の推奨を承認。TASK_MEMMAP_V3 §8-4) |

---

## 1. 現状の事実 (出典付き)

| 事実 | 値 | 出典 |
|---|---|---|
| システム辞書のファイル | `assets/fep.db` 5,815,296B (M 版)。生成物で git には無い (`.gitignore:33-36` が `fep.db` / `fep_s.db` / `fep_l.db` / `fep.dic`)。`build/assets.mk:32-38` が `tools/fep_to_sqlite.py` で IPADIC の CSV から作る | `ls -l`、`.gitignore`、`build/assets.mk` |
| 表 | **`dict` 表 1 つ** `(yomi TEXT, kanji TEXT, pos_id, cost INT)` + 索引 `idx_dict_yomi(yomi)`。**97,514 語**。メタ情報の表は無い | `sqlite_master`、`SELECT count(*)` (2026-09-30 実測) |
| ページ / 版 | `page_size` **1024**、`PRAGMA user_version` **0** (未使用) | 同上。生成側は `PAGE_SIZE = 1024` (`tools/fep_to_sqlite.py:25,192`)、カーネル側は `SQLITE_DEFAULT_PAGE_SIZE 1024` (`lib/sqlite3/os32_sqlite_config.h:67`) |
| 学習表 | **同じ `fep.db` の中**に、辞書を開くたび `CREATE TABLE IF NOT EXISTS dict_user (yomi, kanji, freq, last_ts, PRIMARY KEY (yomi, kanji))` + `idx_user_yomi` を作る。学習は UPSERT (`SQL_LEARN`)、列挙・削除・書き出し・全消去は `ime_user_list / delete / export / clear` | `kernel/ime_dict.c:86-95` (作成)、`:51` (`SQL_LEARN`)、`:463-599` (`ime_user_*`) |
| 辞書を開く形 | `sqlite3_open_v2(path, SQLITE_OPEN_READWRITE)` — 学習表を書くので**読み書き**で開く。FD は `vfs_fd_set_protect` で exec の一括クローズから守る | `kernel/ime_dict.c:76,25-30,133` |
| S / M / L | `tools/fep_to_sqlite.py` の `SIZE_DEFS` (`:33-38`): S = cost ≤ 800 (`fep_s.db`)、M = cost ≤ 1500 (`fep.db`、既定)、L = 無制限 (`fep_l.db`)。`--all` で 3 つ、`--size` で 1 つ。dict_raw → yomi+kanji で重複排除 (同点は pos_id 最小、再現性のため) → `dict` → `VACUUM` | `tools/fep_to_sqlite.py:33-38,192-236,244-270` |
| 切り替え | `ime_switch_dict(variant)` が `/db/fep_s.db` / `/db/fep.db` / `/db/fep_l.db` を開き直す (`ime_dict_reopen`)。**開いている辞書の置き換えは VFS が BUSY で断る**ので、`hsync` は差し替えられない | `kernel/ime.c:882-895`、`kernel/ime_dict.c:150-157` |
| 配備 | `assets/fep.db` → ゲスト `/db/fep.db` | `userland/deploy.yaml:311-312` |
| SQLite の取り分 (今) | FEP 辞書が常駐した状態で 38,784B (`cfg_bench` の実測)。MEMSYS5 は全接続共有の 1 ヒープ (今 384KB → v3 で 512KB) | [`../settings/DESIGN.md`](../settings/DESIGN.md) §6、`lib/sqlite3/os32_sqlite_vfs.c:34-39`、TASK_MEMMAP_V3 §4-6 |
| `PRAGMA user_version` はカーネルの SQLite で**省かれている** | `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` (`lib/sqlite3/os32_sqlite_config.h:56`) は `schema_version` だけでなく **`user_version` の PRAGMA も落とす** (`lib/sqlite3/sqlite3.c:144652-144658` の `#if !defined(SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS)` の内側) | `sqlite3.c` の PRAGMA 表 |
| ライセンス文 | `assets/ipadic/` は CSV 13 本だけで、**IPADIC の配布条件の原文は本リポジトリに無い** | `ls assets/ipadic` (2026-09-30) |

---

## 2. 辞書のメタ情報 — 項目・使い道・置き場

**置き場の分担**: **`PRAGMA user_version` (ヘッダの 4 バイト整数) には形式の版だけ**を置き、**残りは `meta` 表 (`key TEXT PRIMARY KEY, value TEXT`)** に置く。理由: 形式の版は「この FEP が開けるか」を**SQL を流す前**に決める値なので、スキーマに依らず読めるヘッダ値がよい (開けない版の辞書に `SELECT` を流さない)。それ以外は数も型もこれから増えるので表がよい。ただし §1 のとおりカーネルの SQLite は `user_version` の PRAGMA を省いているので、**実装時に `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` を外す**か、**ファイルヘッダのオフセット 60〜63 (ビッグエンディアン) を VFS 経由で直接読む**かを T4 (モジュール化でビルド設定を作り直す段) で決める。`page_size` はヘッダのオフセット 16〜17 にあり `PRAGMA page_size` でも読めるので、`meta` には**持たず照合だけ**する。

| 項目 | 型 | 使い道 | 置き場 |
|---|---|---|---|
| `format_version` | 整数 | この FEP が開ける形式か (スキーマ・列・索引の約束の版)。**小さければ開かない**、大きければ「知らない版」として開かない。`user_version` に置き、`meta` にも同じ値を写す (表だけ読んだ側の照合用) | `PRAGMA user_version` + `meta` |
| `page_size` | 整数 | カーネルの `SQLITE_DEFAULT_PAGE_SIZE` と一致するかの照合 (違うと読める場合もあるがキャッシュの見積もりが狂う) | ヘッダ (`PRAGMA page_size`)。`meta` には持たない |
| `min_fep_version` | 整数 (KAPI 版か FEP 自身の版) | 辞書が要求する FEP の最低版 (新しい列・コスト式を使う辞書を古い FEP に読ませない) | `meta` |
| `dict_id` | 文字列 (例 `ipadic-2.7.0-M`) | **学習データとの対応付け** (§3)。同じ `dict_id` の辞書と学習だけを組にする | `meta` |
| `kind` | `system` / `user` / `supplement` | システム辞書 (読み取り専用で配る) / 学習データ (書く) / 追加辞書 (人名・地名など、将来) の区別。FEP は `kind` を見て開き方 (RO / RW) を決める | `meta` |
| `language` / `charset` | 文字列 (`ja` / `UTF-8`) | 将来の多言語・別の符号化の辞書を今の FEP が誤って開かないため | `meta` |
| `variant` | `S` / `M` / `L` | `ime_switch_dict` の表示と、`mem_reserve_kb` の既定の見積もりの根拠 | `meta` |
| `source` / `source_version` | 文字列 (`IPADIC` / `2.7.0`) | 出典。`04_DICT_QUALITY` の頻度データを重ねたときはここに列挙 | `meta` |
| **`license` / `attribution`** | 文字列 (条項の要旨と著作権表示の全文、または同梱ファイルへの参照) | **必須**。辞書はパブリックの `os32-v3` で配るので、元データの配布条件 (IPADIC は著作権表示と条項の同梱を求める) をファイル自身が運ぶ。`ime` コマンドで表示できる形にする。**配布条件の原文の取り寄せと同梱 (`assets/ipadic/` に置き、`fep_to_sqlite.py` が `meta` に写す) はこの票の実装項目** | `meta` |
| `build_tool` / `build_date` | 文字列 | 生成した道具と日付 (`fep_to_sqlite.py` の版、ISO 8601)。再現性の確認 | `meta` |
| `entry_count` | 整数 | `SELECT count(*)` を起動時に流さずに語数を表示・検査する (97,514 のような数を `ime` コマンドで出す) | `meta` |
| `pos_table_version` | 整数 | `pos_id` の意味 (品詞表) の版。**学習データ側にも記録して不整合を検出** (§3)。品詞表を変えた辞書に古い学習を混ぜない | `meta` |
| `cost_scale` | 整数または文字列 | `cost` の尺度 (IPADIC の生コストか正規化後か、上限値)。候補の並べ替えで辞書と学習の `freq` を合成するときの基準 | `meta` |
| **`mem_reserve_kb`** | 整数 (KB) | **FEP の SQLite 取り分** (TASK_MEMMAP_V3 D27・§4-6)。起動時にこの量を MEMSYS5 から優先確保し、アプリの `db_open` は「プール − この分」で上限。settings (`fep.mem_reserve_kb`) で上書き。切り替え先の値が起動時の確保量を超えるときは再起動で再確保 | `meta` |
| `cache_pages` | 整数 (ページ) | 辞書の接続に設定する `PRAGMA cache_size` の推奨値 (`mem_reserve_kb` の内訳。page_size × cache_pages ≤ mem_reserve_kb × 1024 をビルド時に検査) | `meta` |
| `checksum` | 文字列 (`dict` 表の内容のハッシュ、または全ファイル) | 配備の照合 (`hsync --verify` の代わりに辞書だけ検査)、破損の検出。全ファイルのハッシュはファイル自身に書けないので**表の内容のハッシュ**にする | `meta` |

未指定の項目の既定値 (メタ情報の無い今の `fep.db` を v3 の途中で読むための救済) は**実装の段で決める**: `format_version` 0 = 「メタ無しの旧形式」として `dict` 表だけ読み、`mem_reserve_kb` は既定値 (TASK_MEMMAP_V3 §4-6)。

---

## 3. 学習データを辞書と別ファイルに分ける (M2)

**今**: 学習表 `dict_user` は辞書と同じ `fep.db` に作られる (`ime_dict.c:86-95`)。辞書を差し替えると学習も消え、辞書を書き込み可能で開かなければならず、`hsync` は使用中の辞書を差し替えられない (`ime_dict.c:150-157`)。

**決定**: 学習データは別ファイル (例 **`/etc/fep_user.db`**。パスは settings で上書き可) に置く。

| 理由 | 効果 |
|---|---|
| **辞書の入れ替え・`hsync` で学習が消えない** | S/M/L の切り替え (`ime_switch_dict`) や辞書の更新配備を、学習を失わずに行える。学習は `/etc` に残る |
| **システム辞書を読み取り専用で配れる** | `kind = system` の辞書は `SQLITE_OPEN_READONLY` で開く。書き込みが無いので ext2 の ROFS (`OS32_ERR_ROFS`) でも変換は動き、ジャーナルも作らない。`hsync` は辞書を「使用中の RW ファイル」として扱わなくてよくなる (差し替え規則は実装の段で確認) |
| **不整合を検出できる** | 学習データ側の `meta` に **`dict_id` と `pos_table_version`** (と `format_version`) を記録し、開くときに辞書側と照合する。不一致なら学習の**使用を止めて警告** (消さない — `ime` コマンドで移行か破棄を選ばせる)。辞書に混ざっていた今は、この検出そのものができない |

**学習 DB のスキーマ (案)**: `dict_user` は今の列のまま (`yomi, kanji, freq, last_ts`、PRIMARY KEY `(yomi, kanji)`) + `meta` 表 (`kind = user`、`dict_id`、`pos_table_version`、`format_version`、`created`)。無ければ FEP が作る (今の `CREATE TABLE IF NOT EXISTS` の経路を学習 DB 側へ移す)。

**接続の数**: 辞書 1 本 (RO) + 学習 1 本 (RW) の **2 本**になる。SQLite の取り分 (`mem_reserve_kb`) は 2 本分で見積もる (TASK_MEMMAP_V3 §4-6 の既定値も、この票の着手時に 2 本分へ更新する)。

**移行 (M6 — Codex X8、2026-09-30、ユーザー承認)**: v3 への移行でメモリの再配置がディスク上のデータ形式を変えることは無いが、**配備の上書きと、この票の辞書変更には明示の保存・移行が要る**。二段に分ける:

1. **fork 直後 (この票の前)**: 既存データ (文書・`settings.db`・`fep.db` の `dict_user`) を**保持した媒体**で v3 のシステム一式だけを更新する。分離前の `/db/fep.db` を生成済み辞書で上書きすると同居する学習が消える — 使用中の BUSY 拒否 (`ime_dict.c:150-157`) は**停止中のホスト配備を守らない**。**S/M/L の実使用ファイル (`fep.db` / `fep_s.db` / `fep_l.db`) は `dict_user` を保持する形で配備する** (配備前に抽出して書き戻すか、配備対象から外す — 手順は fork の票で決める。V3_PLAN_DRAFT §5 C7)。新しい `settings.db` のマスタで上書きすることを「移行」と呼ばない (再配置だけならスキーマ移行は不要)。
2. **この票**: 旧 `dict_user` (3 ファイルに散っている) から `fep_user.db` へ **`yomi / kanji / freq / last_ts` を保持して移す**。定義するもの: (a) **S/M/L 間の重複規則** — 同じ `(yomi, kanji)` が複数の辞書ファイルの学習にあるとき `freq` を足すか大きい方を取るか、`last_ts` は新しい方; (b) **`dict_id` の対応範囲** — `dict_id` に S/M/L の変種を含めて学習 DB に完全一致を要求すると、§5 の「S → L に切り替えても学習が残って使える」と衝突する。変種を除いた基底 ID (`ipadic-2.7.0`) で対応付けて `variant` を別に持つか、対応表を持つかを設計票で決める; (c) **再実行時の二重加算防止** (学習 DB の `meta` に移行済みの印と出所ファイルの `checksum`); (d) **途中失敗からの復旧** (旧表は移行の完了を確認するまで消さない、途中で落ちたら再実行で完了する)。
3. **`fep_user.db` を `hsync` の保護名に**: 今の保護名の一覧は `settings.db*` だけ (`userland/system/hsync_protect.inc:34`)。配備マニフェストから外すだけでは、古い HostDrv 側のファイルからの同期を防げない。
4. **v2.x の戻り先**は旧バイナリに加えて**旧データ (fork 時点の NHD / `fep.db` / `settings.db`) の写し**も保持する (V3_PLAN_DRAFT §5 C7)。

---

## 4. 影響範囲 (着手時に確定する)

| 場所 | 変更 |
|---|---|
| `tools/fep_to_sqlite.py` | `meta` 表の書き込み (§2 の全項目)、`user_version` の設定、配布条件の原文の同梱 (`license` / `attribution`)、`checksum` の計算、`cache_pages` × page_size ≤ `mem_reserve_kb` の検査 |
| `assets/ipadic/` | 配布条件の原文を取り寄せて置く (今は無い、§1) |
| `kernel/ime_dict.c` | 開くとき: 形式の版の検査 (ヘッダ直読みか PRAGMA、§2)、`meta` の読み取り、辞書を RO で開く、学習 DB を別に開く (`dict_user` の作成をそちらへ)、`dict_id` / `pos_table_version` の照合。`SQL_LEARN` と `ime_user_*` の宛先を学習 DB へ |
| `kernel/ime.c` | `ime_switch_dict`: 切り替え先の `mem_reserve_kb` が起動時の確保量を超えるときは再起動を促す (D27) |
| `lib/sqlite3/os32_sqlite_config.h` | `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` を外すか、ヘッダ直読みで済ませるか (T4 で決める) |
| KAPI (`ime_user_*`、`ime_switch_dict`) | シグネチャは変えない ([ABI2])。学習の宛先が変わるだけ。メタ情報を返す口 (`ime_dict_info` 等) を足すなら [ABI2] 追記 |
| `userland/deploy.yaml`、`hsync` | 学習 DB は配備対象にしない (`/etc` に残す)。システム辞書の RO 差し替えの規則 |
| `userland/system/hsync_protect.inc` | **`fep_user.db*` (本体・journal・回復用) を保護名に足す** (M6。今は `settings.db*` だけ) |
| 移行の手段 (`ime` コマンドの下位コマンドか、学習 DB が無いときの初回起動の自動移行 — 設計票で決める) | 旧 `dict_user` (S/M/L の 3 ファイル) → `fep_user.db`。件数・内容の照合、重複規則、`dict_id` の対応、移行済みの印、途中失敗の復旧 (M6、§3) |
| settings (`settings.db`) | `fep.mem_reserve_kb`、`fep.user_dict_path` (上書き用) |
| 文書 | [`FEP_STATUS.md`](FEP_STATUS.md) の辞書スキーマの節を更新、[`00_INDEX.md`](00_INDEX.md) の表に状態を反映 |

---

## 5. 受入 (案 — 設計票の段で確定)

- メタ情報の無い今の `fep.db` を旧形式として開けること (`format_version` 0 の救済)。`format_version` が FEP より大きい辞書は開かず、理由を表示。
- 学習 → 辞書を S → L に切り替え → 学習が残ること。辞書を `hsync` で差し替えても学習が残ること。
- `dict_id` の違う学習 DB を置いたとき、学習の使用が止まり警告が出て、辞書の変換は動くこと。
- システム辞書を RO で開いた状態で ROFS (書き込み全拒否) を起こしても 1 語変換が通ること。
- `ime` コマンドが `license` / `attribution` と `entry_count` を表示すること。
- `mem_reserve_kb` を settings で上書きしたとき、起動時の確保量がその値になること。切り替え先の値が確保量を超えるとき、その場で確保せず再起動を促すこと (TASK_MEMMAP_V3 §4-6)。
- **移行 (M6)**: 旧 `fep.db` / `fep_s.db` / `fep_l.db` の `dict_user` から移した学習の件数と内容 (`yomi / kanji / freq / last_ts`、重複規則どおりの合成) が一致すること。移行を 2 回実行しても `freq` が二重加算されないこと。途中で失敗させても旧表が残り、再実行で完了すること。S/M/L のどれで学習した語も `dict_id` の対応規則どおりに使えること。`hsync` が `fep_user.db` を上書きしないこと。

---

## 6. 時期と依存

- **v3 の後の方** (ユーザー 2026-09-30)。前提: TASK_MEMMAP_V3 の **T4** (SQLite のモジュール化 — `mem_reserve_kb` の既定値と `db_open` の上限はそこで入る) と **T5a** (FEP のモジュール化)。この票はその上に「辞書からの読み取り」と「学習の別ファイル化」を載せる。
- 並べる先: [`../v3/V3_PLAN_DRAFT.md`](../v3/V3_PLAN_DRAFT.md) §3 **P10 データ・設定層** (v3 後半)。
- 関係する票: [`../settings/FEP_BOUNDARY.md`](../settings/FEP_BOUNDARY.md) (FEP の KAPI 境界)、[`../settings/F2_OWNERSHIP.md`](../settings/F2_OWNERSHIP.md) (接続単位の FD 所有 — 学習 DB の接続も同じ規則)、[`04_DICT_QUALITY.md`](04_DICT_QUALITY.md) (コスト式。`cost_scale` / `pos_table_version` の値を決めるのはそちら)。
- **移行の前段 (Codex X8、2026-09-30)**: fork 直後の v3 一式への更新は**既存データを保持した媒体**で行い、`fep.db` の `dict_user` を配備で消さない (§3 の 1)。v2.x の戻り先は旧データの写しも保持。**排他 open (D29) の導入まで**の同一 DB の重複接続 (parked アプリ同士を含む) の扱いは TASK_MEMMAP_V3 §4-6 (T4 で明記)。
- **後続 (U6 決定、ユーザー 2026-09-30、TASK_MEMMAP_V3 D29)**: F3b「同一 DB の排他 open」(S0_FOUNDATION の lock 表は作らず、親子 (exec の入れ子) が同じ DB を同時に開く経路を open で塞ぐ最小代案) は**この票の後**に P10 で行う — 辞書 RO 1 本 + 学習 RW 1 本になれば「辞書は RO 共有、学習は RW 単独」で排他 open と相性がよい。出典 [`../v3/U6_PENDING_REVIEW.md`](../v3/U6_PENDING_REVIEW.md) §1-1。

---

## 7. 経緯

| 日付 | 出来事 |
|---|---|
| 2026-09-30 | v3 のメモリマップの討論 (TASK_MEMMAP_V3 §11-3) の中で、FEP の SQLite 取り分の出典として「辞書のメタ情報 `mem_reserve_kb`」が出た → ユーザーが**メタ情報の項目と学習データの別ファイル化を採用、着手は v3 の後の方**と決定 (D27・D28)。この票を起こした (計画) |
| 2026-09-30 | V3_PLAN_DRAFT §7-2 **X8** (データの互換) への Codex の回答をユーザーが承認 → **M6** (二段移行、`fep_user.db` の保護名、v2.x の戻り先は旧データの写しも) を §0・§3・§4・§5・§6 に追記。決定 M1〜M5 は変えない |
