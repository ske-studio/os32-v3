# U6_PENDING_REVIEW — 保留 5 件 (F3a〜c / F2c / FEP_BOUNDARY / MEMORY_RAM_INTEGRATION / DEVICE_RESERVATION) の仕分け表

> 状態: **完了記録 (2026-09-30)** — **決裁済み**。[V3_PLAN_DRAFT.md](V3_PLAN_DRAFT.md) §7-1 **U6**「保留 5 件を v3 で拾うか」の仕分け表。§2 の判断 6 点は **2026-09-30 にユーザーがすべて推奨 (★) どおりに決定**: (1) F3b は (b) 同一 DB の排他 open を TASK_DICT_META の後、(2) F2 の残りは (a) T4 + T5a に畳む、(3) FEP_BOUNDARY は (a) T2 / T4 / T5a の要件 + 旧 `db_exec` / `db_prepare` の 1024B 超は失敗に、(4) MEMORY_RAM_INTEGRATION は撤回して archive へ、(5) DEVICE_RESERVATION は (c) 識別 + 予約は起動時・probe + enable は GUI 境界、範囲は検証済みの実測 BAR へ (X4 は Codex へ)、(6) U6 の答えは「一部」。決定の正典は [TASK_MEMMAP_V3](../../tasks/v3/TASK_MEMMAP_V3.md) **D29〜D34**、§2 末尾の「決裁後に PM が行うこと」は同日に反映済み (各票の状態行、T1 / T2 / T4 / T5a の受入、V3_PLAN_DRAFT P4 / P10 / U6、ROADMAP §1、INDEX)。以下の本文は草案時のまま。
> それまでの状態: 草案 (2026-09-30) — ユーザーが内容を再確認するための仕分け表。決定ではない
>
> 発行: コーダー `claude-fable-5-1` (2026-09-30)、PM の指示 (ユーザー指示「U6 の仕分け表を作る」) による。基点 `feat/gui` 2a6cc836 (KernelAPI v68)。
> 読んだもの: 5 件の票 ([F2_OWNERSHIP](../../tasks/settings/F2_OWNERSHIP.md) / [FEP_BOUNDARY](../../tasks/settings/FEP_BOUNDARY.md) / [MEMORY_RAM_INTEGRATION](../settings/MEMORY_RAM_INTEGRATION.md) / [DEVICE_RESERVATION](../../tasks/settings/DEVICE_RESERVATION.md)、F3a〜c は [S0_FOUNDATION §3](../settings/S0_FOUNDATION.md) の表)、保留の出典 ([HANDOVER_v14 §3](../agents/HANDOVER_v14.md)、[S0_PLAN_2026-09-13 §2 後回し欄](../settings/S0_PLAN_2026-09-13.md))、[V3_PLAN_DRAFT](V3_PLAN_DRAFT.md) §1-3・§3・§4、**[TASK_MEMMAP_V3](../../tasks/v3/TASK_MEMMAP_V3.md) の決定 D1〜D28・§2-2 lease・§3-5 池の運用規則 R1〜R7・§4-6 SQLite/FEP の取り分・§6 票 T0〜T7**、[TASK_DICT_META](../../tasks/fep/TASK_DICT_META.md)、[TASK_TRIDENT_DRIVER §4-2](../../tasks/realhw/TASK_TRIDENT_DRIVER.md)。
> **「今の状態」の列は 2026-09-30 に HEAD 2a6cc836 のソースと `git log` で確かめたもの** (根拠は §3)。票の本文の行番号は 09-13 の作業ツリーのものなので、ここでは関数名で書く。

---

## 0. 結論の一覧 (票ごとに 1 行)

「柱」は V3_PLAN_DRAFT §3 の P0〜P10、「票」は TASK_MEMMAP_V3 §6 の T0〜T7。

| 保留 | 提案 | 一言 |
|---|---|---|
| **F3a** (SQLite VFS の I/O 失敗を正直に) | **v3 P10 — 拾う** (小さい、P1 に依存しない。T4 の**前**か**中**で `os32_sqlite_vfs.c` を触るときに) | TASK_MEMMAP_V3 は何も置き換えていない。truncate / sync / delete / access / seek の rc は今も捨てている |
| **F3b** (DB の同一性と lock 表) | **判断が要る** (§2 の 1) — 案 (b) 「同一 DB の排他 open」を TASK_DICT_META (辞書 RO + 学習 RW) の後に、を推奨 | P6 の決定 (同時実行しない) で lock 表の価値は下がったが、親子 (exec の入れ子) が同じ DB を開く経路は残る |
| **F3c** (open フラグ / RO の正直さ) | **v3 P10 — ほぼ済み**。公開側 (KAPI v50 `db_open_existing`) は着地、VFS 側 (`pOutFlags`、RO 接続の write 拒否) の小さな残りだけ F3a と一緒に | S0-K (08b48790) で公開側は解決 |
| **F2c** (FEP の RESIDENT group 移行) — 実態は **F2 の残り全部** (F2b の呼び出し側接続・F2c・F2d の隔離経路・R0/R1) | **v3 P10 — 拾う。T4 (SQLite モジュール化) と T5a (FEP モジュール化) の受入に足す**のを推奨 (同じファイルを 2 度触らない) | D21 (常時読み込み)・§4-6 (FEP の接続は起動時に開く)・TASK_DICT_META (接続 2 本) が前提を変えた: RESIDENT の容量は 1 → 2 |
| **FEP_BOUNDARY** (FEP KAPI の境界で未検証ポインタを SQLite に渡さない + エンジン中断の fail-stop) | **v3 P10 — 拾うが、独立票ではなく T2 / T4 / T5a の要件として** (§1-3 の表) | T2 がアプリ帯を 0x80000000 へ動かすので `ring3_ptr_ok` の帯判定は作り直しになる (B1 は T2 の中で)。T4 のエクスポート表が「全 outer 呼び出しの台帳」(§6.3) を置き換える |
| **MEMORY_RAM_INTEGRATION** (RAM 統合 Phase 2) | **撤回 (archive へ) を提案** — A/B の大半は K6 で着地済み、残り (A3 の exec 永久 claim・C・D) は **D3 で「撤去」と決まった機構の完成形**なので v3 では作らない。残る 2 点だけ T1 の受入へ (§1-4) | **見つけた事実: 8MB 機は今も legacy 経路 (`pgalloc_init`) に落ちる** — T1 の「legacy 撤去」の受入に 8MB を明示すること |
| **DEVICE_RESERVATION** (任意デバイス窓の予約 broker) | **v3 P4 — 拾う。ただし改訂して**: transaction 核と owner 台帳は **T1 の台帳に吸収**、順序契約 (識別 → 予約 → 写像 → probe → 公開) と Trident の実測 BAR の口は **P4 の票**へ。§5 (後発 GUI の PEGC BB) は D19・§2-2 で不要 | 判断が要る (§2 の 5): probe と予約の時期 = 起動時 (TASK_MEMMAP_V3 §4-5) か GUI 境界 (DEVICE_RESERVATION §5、TRIDENT T8 (i)) か |

U6 の選択肢「拾う (P4・P10) / 一部 / 拾わない」に当てると **「一部」**: 4 件は拾う (形を変えて)、MEMORY_RAM_INTEGRATION は撤回、F3b は判断待ち。

---

## 1. 票ごとの仕分け

各表の行: (1) 何を決め・作ろうとしていた票か / (2) 今の状態と実装済みの部分 / (3) TASK_MEMMAP_V3 の決定で **置き換わった・要らなくなった部分** と **まだ残る部分** / (4) 残る部分をどこで拾うか / (5) ユーザーの判断が要る点。

### 1-1. F3a〜c — SQLite VFS の失敗と排他を正直にする

出典: [S0_FOUNDATION §3 F3](../settings/S0_FOUNDATION.md) (3 行の表だけ。独立の票は無い)。保留の理由 (S0_PLAN §2): v1.3 で settings.db を使うのに要らない。

| | 内容 |
|---|---|
| (1) 何を | **F3a I/O**: `os32Read/Write/FileSize/Truncate/Sync/Delete/Access` で、負 offset・int 超過・加算 overflow を I/O 前に拒否、seek の rc を検査、truncate 未対応は `SQLITE_IOERR_TRUNCATE` (no-op 成功にしない)、sync 失敗は `SQLITE_IOERR_FSYNC`、delete は NOTFOUND だけ無害、access は不存在と I/O 失敗を分ける。**F3b identity/lock**: fullpath の正規化・切捨て拒否、mount/inode の同一性による lock 表、SHARED/RESERVED/PENDING/EXCLUSIVE。**F3c open/RO**: `pOutFlags` を実際のモードに合わせる、RO 接続は main と journal への write/create/delete/truncate を拒否、RW 要求の silent RO fallback を公開側で検出、READWRITE (no CREATE) で missing を作らない |
| (2) 今 | `lib/sqlite3/os32_sqlite_vfs.c` (HEAD): **F3a 未着手** — `os32Read/Write` は `vfs_seek` の rc を見ず `(int)iOfst` を無検査、`os32Truncate` は **no-op で OK**、`os32Sync` は `vfs_sync()` の int を捨てて OK、`os32VfsDelete` は `vfs_rm` の rc を捨てて OK、`os32VfsAccess` は stat の失敗を「不存在」と同じに扱う、`os32VfsFullPathname` は `kstrncpy` で切捨て。**F3b 未着手** — `os32Lock/Unlock/CheckReservedLock` は no-op (先頭コメント「シングルタスクのため」)。**F3c は公開側が着地**: KAPI v50 `db_open_existing(path, writable)` (RO / RW、CREATE 無し、open 前に本体と `-journal` を stat して NOTFOUND だけ不存在・他は IOERR で SQLite を呼ばない — 08b48790、77d61b30)。VFS 側は `pOutFlags = flags` の写しのまま。周辺で着地したもの: FD の失効 (STALE) と `file_live()` 検査、開いている DB の rename は BUSY (b99f30ab、1cd5dc5f) — F3b の「同一性」の一部に当たるが lock 表ではない |
| (3) 置き換わった / 残る | **置き換わった部分は無い**。TASK_MEMMAP_V3 は SQLite の**置き場** (T4: モジュール、MEMSYS5 512KB、外部データ禁止) を決めただけで、VFS の I/O・lock・RO の契約には触れていない。影響: T4 で `os32_sqlite_vfs.c` はモジュール側に入り、`vfs_*` はインポートスタブ経由になる (§4-2 の例 `vfs_validate_sqlite`) — F3 の変更は**そのまま一緒に移る** (衝突しない)。**F3b の前提は変わった**: P6 (同時実行しない、協調) と S2 決裁 (cfg の接続は構造的に同時 1 本) で「複数 writer」の経路は減ったが、**親 (常駐シェル / gshell) が開いた接続を子が使う・子が同じ DB を別接続で開く** (exec の入れ子、F2 の T01/T02 の状況) は残る。TASK_DICT_META (辞書 RO 1 本 + 学習 RW 1 本) は F3b の最小代案「同一 DB の全接続を排他 open」と相性がよい (辞書は RO 共有、学習は RW 単独) |
| (4) どこで | **F3a + F3c の VFS 側 → P10** (独立、小さい。**T4 の前に済ませる**か **T4 の受入に足す** — `os32_sqlite_vfs.c` を触るのは同じ機会)。**F3b → 判断** (下)。ext2 の truncate は今も未実装 (`fs/vfs.h` の ops に `truncate` はあるが VFS 注記は「未実装」) — F3a では **`SQLITE_IOERR_TRUNCATE` を返す**だけにし、実体は別の小票 (S0_FOUNDATION の指示どおり) |
| (5) 判断 | **F3b の形**: (a) S0_FOUNDATION どおりの lock 表 (SHARED/RESERVED/… + mount/inode 同一性) を P10 で作る / **(b) 最小代案「同一 DB の全接続を排他 open」** — TASK_DICT_META の後 (辞書が RO になれば FEP との互換の問題が消える) に v3 後半で / (c) v4 へ。**推奨 (b)**。理由: P6 で同時実行しないと決めたので lock 表が守るのは「入れ子の親子が同じ DB を同時に書く」1 経路だけで、排他 open で塞げる。(a) を選ぶなら F3b は P10 の独立票 |

### 1-2. F2c — FEP の RESIDENT group 移行 (実態は F2 の残り全部)

出典: [F2_OWNERSHIP](../../tasks/settings/F2_OWNERSHIP.md) (設計中、F2b の内部基盤だけ着地)。保留欄には「F2c」とあるが、F2c だけを切り出して拾う形にはならない (F2b の呼び出し側接続と F2d の隔離経路が前提)。

| | 内容 |
|---|---|
| (1) 何を | SQLite の接続を開いた exec owner と、その接続が**後から開く全 FD** (遅延 journal・temp) を結び付け、正常時は SQLite に閉じさせ、close 失敗時は接続と残存 FD を再利用不能に**隔離**してから子を終了する。方式: 接続専用 VFS instance (group、`kind = EXEC / RESIDENT`) + FD 表の lease (`lifetime = SQLITE`、cookie、世代)。段: F2a FD lease → F2b 接続 VFS + F1 接続 → **F2c FEP** (常駐 group を open 前に取得、`dict_fd_protect` の撤去、close 失敗を隠さない reopen 拒否) → F2d 終了統合 (`exec_cleanup_owned_resources`: DB → redirect → generic FD → pipe) |
| (2) 今 | **着地**: F1 (`db_cleanup_owned`)、F2a (`fs/vfs_fd.c` の `vfs_open_sqlite / close / validate / count / quarantine`)、F2b の内部基盤 (`os32_sqlite_group_acquire / open / opened / begin_close / finish / quarantine`、DB_MAX_CONNECTIONS 8 + RESIDENT 1、be445f4e、host 32/32)、**F2d の順序修正** (`exec_reclaim_owned` は `db_cleanup_owned(id)` → `vfs_close_owned(id)` → `shm_free_owned(id)`、S0-K 08b48790)。**未着地**: F2b の呼び出し側 — `kapi/kapi_db.c` は今も `sqlite3_open(path)` / `sqlite3_open_v2(…, NULL)` (default VFS)、`kernel/ime_dict.c` も `sqlite3_open_v2(…, NULL)` で `os32_sqlite_group_open` を呼ぶ接続は **1 本も無い**; F2c; F2d の隔離経路 (`exec_cleanup_owned_resources` helper、quarantine の到達); default VFS の未所属 open の fail-closed 化; R0 / R1 |
| (3) 置き換わった / 残る | **置き換わった部分は無い** (F2 の契約は FD の所有で、TASK_MEMMAP_V3 の R7 は**物理ページ**の owner 回収 — 別の層)。**前提が変わった部分**: (i) D21 で SQLite と FEP は常に読み込む → RESIDENT group は常に存在、MINIMAL では SQLite ごと無い (stub); (ii) §4-6 で **FEP は起動時 (最初のアプリより前) に辞書の接続を開く** → 常駐 group の open は live AS = 0 の起動列で行う (F2c の「open 前に RESIDENT を取得」がそのまま当てはまる。遅延 journal が子の実行中に開く問題 (T12) は残る); (iii) **TASK_DICT_META で FEP の接続は 2 本** (辞書 RO + 学習 RW) → F2b の `RESIDENT = 1` は **2 に**; (iv) T4 で `os32_sqlite_vfs.c` はモジュールへ → group 表はモジュールの data に置かれる。R7「v3 ではモジュールを取り外さない」なので「kernel 再初期化まで隔離を保持」の契約はそのまま成り立つ; (v) D7 で ABI 互換は考えない — F2 は元から公開 KAPI を変えないので影響なし。**残る部分 = 未着地の全部** (F2b 呼び出し側、F2c、F2d 隔離、fail-closed、R0/R1) |
| (4) どこで | **P10。実施は T4 (kapi_db.c / ime_dict.c をエクスポート表経由に書き換える) と T5a (FEP モジュール) の中**を推奨 — 同じ 2 ファイルの接続部を 2 度書き換えない。T4 の受入「`db_*` 全 KAPI、FEP 変換」に F2 の T01 / T02 / T07 / T12 (親の接続の生存、後発 journal の所属、close 失敗の隔離、子の実行中の FEP 学習) を足す |
| (5) 判断 | **(a) T4 + T5a に畳む** (推奨。T4 が大きくなる) / (b) T4 の前に P10 の独立票 (F2b 呼び出し側 + F2c + F2d) / **(c) 捨てる** — S0_PLAN §3 の 3 で cfg については「子の異常終了で残るのは hot journal 1 つ、次の RO open が検出して通知する (自動回復しない)」契約を既に受けている。捨てる場合に残る穴: 子の実行中に FEP が初めて journal を開くと `exec_exit` の一括回収が FEP の FD を閉じる (今は `vfs_fd_set_protect` で main だけ守っている)、game の複数 DB の close 失敗。**(a) を推奨** |

### 1-3. FEP_BOUNDARY — FEP user 境界と SQLite 中断禁止

出典: [FEP_BOUNDARY](../../tasks/settings/FEP_BOUNDARY.md) (設計中、R0 待ち)。

| | 内容 |
|---|---|
| (1) 何を | **SQLite / VFS が caller の未検証ポインタを一度も読まない・書かない境界**。(a) 上限表 §3 (prefix / yomi / kanji / path は 256B、list は 1〜64 件、staging 64 × 68B、SQL は 1024B で**切捨てて実行しない**); (b) checked copy の helper §4 (caller PD の PDE/PTE を walk、`[start, start+len)` の overflow、IF の保存/復元); (c) list は kernel staging → **finalize → copyout** の順 §5; (d) DB の path / SQL も同じ checked copy、**旧 active_stmt の finalize より前に SQL を全コピー** §6.1; (e) **エンジン in-flight の fail-stop** §6.2 — SQLite 進入中 (`enter` / `leave`) の #PF / 一般例外は app-kill にせず kernel 異常として停止、EIP が `.sqlite_text` にあるかだけで判定しない; (f) 全 `sqlite3_*` 呼び出しの台帳 §6.3; 段 B1 uaccess → B2 FEP → B3 DB → B4a/b guard |
| (2) 今 | **FEP の facade は未着手**: `ime_user_list_facade / delete / export` (`kernel/ime.c`) は caller のポインタをそのまま `ime_user_*` に渡す。**その後に着地して一部を覆うもの**: (i) [TASK_KAPI_OUTPUT_GUARD](../kernel_v21/TASK_KAPI_OUTPUT_GUARD.md) (09-23) — `wrap_ime_user_list` の先頭で `ring3_user_ranges_writable(out, max × sizeof(IME_UserEntry))` を検査 (書けない出力範囲は kill)。ただし SQLite の step 中に**直接 user メモリへ書く**のは変わらず、§5 の「finalize の後に copyout」ではない; (ii) 入力の文字列 (prefix / yomi / kanji / path) は dispatcher の `ring3_ptr_ok` (先頭番地の帯判定) だけで、長さ・NUL・ページ跨ぎは見ない; (iii) DB 側: KAPI v50 の `db_open_existing` / `db_prepare_only` は **`db_user_str_copy`** (1 バイトずつ `ring3_user_range_ok` で検証、cap 内に NUL が無ければ**失敗、切捨てない**) — §6.1 の新規 3 本分は着地。旧 `db_exec` / `db_prepare` は今も `kstrncpy` の切捨てで、**旧 stmt の finalize が SQL のコピーより先** (§6.1 の指摘のまま); (iv) §6.2 の fail-stop は**無い** — `kernel/isr_handlers.c` は EIP が `__sqlite_start..__sqlite_end` にあれば「[.sqlite_text]」と表示するだけ (票が禁じた判定法そのもの) で、`ring3_in_syscall` の app-kill はそのまま走る; (v) §6.3 の台帳は無い |
| (3) 置き換わった / 残る | **置き換わった**: (i) §4 の「caller PD の帯」— **T2 でアプリ帯が 0x80000000〜 の私有写像に**なり `ring3_ptr_ok` / `RING3_*` は定数分離の対象 (TASK_MEMMAP_V3 §2-3 ⑦ の表に `ring3_ptr_ok` 明記)。今の帯で B1 を作ると T2 で作り直しになる → **B1 は T2 の中で**; (ii) §6.3「全 outer 呼び出しの台帳」— **T4 でカーネル → SQLite の呼び出しはエクスポート表 1 か所を通る** ので、`enter / leave` は表のラッパに置けば台帳は表そのもの。FEP (T5a) の呼び出しも同じ表; (iii) §6.2「CTRL+STOP は syscall 境界まで保留」— **§3-5 R1 で決定済み** (IRQ は要求と制御移譲だけ、回収は通常文脈。T2 で `ring3_abort_kill` を 2 段に)。P6 の番犬 (前景が固まったら取り戻す) も同じ契約の上; (iv) MINIMAL (D21・D26) では SQLite が無いので `db_*` / `ime_user_*` は stub — 境界の検査は「モジュール有り」の経路だけ。**残る**: (a) 入力文字列の bounded copy (FEP の 4 引数 + 旧 `db_exec` / `db_prepare`); (b) list の staging と finalize 後の copyout; (c) delete の「不正 kanji で全候補削除に拡大しない」、export の「不正 path で truncate しない」(§3); (d) エンジン in-flight の fail-stop 自体 (判定法を表のラッパに変えるだけで、要件は残る); (e) 32B 結果幅 vs 入力上限 256B の区別 |
| (4) どこで | **P10 だが独立票にしない**: **B1 (uaccess の helper) → T2 の受入** (新しい帯で `ring3_ptr_ok` を書き直す差分に「NUL まで 1 バイトずつ検証するコピー」を含める。`db_user_str_copy` を `exec/` へ移して共通化); **B4 (enter / leave と fail-stop) → T4** (エクスポート表のラッパ + `isr_handlers.c` の判定順); **B2 (FEP facade) → T5a**、**B3 (旧 `db_exec` / `db_prepare` の checked copy) → T4**。TASK_DICT_META (v3 後半) は `ime_user_*` の宛先を学習 DB へ変えるので、B2 はその前に済ませる |
| (5) 判断 | (a) 上の「T2 / T4 / T5a の要件として拾う」 (推奨。独立票が要らず、レビューも T 票の Codex 往復で受ける) / (b) T5a の後に P10 の独立票 (票の §7 の段のまま) / (c) 拾わない (今の帯判定 + 出力ガードで足りるとする — ただし §2 の穴 (未終端の user 文字列で SQLite 進入中に #PF → app-kill が cleanup に再入) は残る)。**追加の判断**: 旧 `db_exec` / `db_prepare` の SQL が 1024B を超えたとき、今の**切捨てて実行**を**失敗**に変えてよいか (D7「後方互換は考えない」の範囲。推奨: 変える) |

### 1-4. MEMORY_RAM_INTEGRATION — RAM 統合 Phase 2

出典: [MEMORY_RAM_INTEGRATION](../settings/MEMORY_RAM_INTEGRATION.md) (計画)。V3_PLAN_DRAFT §1-3 は「再棚卸しが先」(§4 D10・D11)。

| | 内容 |
|---|---|
| (1) 何を | physmem (適格性モデル) → 起動時配置 (metadata / PT workspace / BB) → checked PFN allocator (動的 bitmap、4GiB を u32 バイトにしない) → 高位 RAM の恒等写像 → sys / exec (**A/B を起動時に永久 claim**、`sys_reserve_top` の freeze、shlib 帯の起動予約) → ドライバ activation の予約判定 (§7) を**一緒に接続する**。§8: PC-98 の安全な >16MiB 検出源は当時未発見 (BDA 0594h は手掛かり) |
| (2) 今 | **A・B の大半は K6 で着地** (2026-09-10〜12): 102b4796 (適格性モデル + オンデマンド PT で RAM 上限を外す)、f6ec5209 (K6-RAM: **BIOS 0594h で 16MB 超を検出**、bitmap と恒等 PT は検出量に応じて確保)、f3abdb34 (`memory_boot_ram_kb`)、1a559a07 (PEGC probe は 15〜16MB に RAM が無いかで判定)、7faf61f5 (モデル経路の `sys_reserve_top`)、01b5751d (**hotdeploy 窓の撤去** — 票の §2・§4・§6 の前提が消えた)。`kernel/physmem.c` は `build/kernel.mk` でリンク済み (票 §2 の「まだ C_KERNEL にない」は古い)。`kernel/memory_boot.c`: `memory_boot_detect` = 0594h の申告値を 1MB ごとの書き込み・読み戻し + 24bit ラップ + 2 巡目の別名検査で**確認した連続分だけ**登録 (§8 の「snapshot と decode」に当たるが、資料の decode ではなく実測の検証)、`memory_boot_init` = legacy 15MB clamp → metadata を低位 RAM の末尾に → `sys_memory_bootstrap_model` → `sys_memory_stage_online`。`pgalloc_metadata_bytes` (2 bitmap の動的サイズ)、`pgalloc_reserve_pfn`。ホスト試験: `test_physmem / test_pgalloc_range / test_pgalloc_model / test_highram_stage / test_paging_bounds`。実機 Ra266 64MB で動作 (RELEASE_v2.1)。**未着地**: A3 / C (exec の A/B 永久 claim — `exec_child_claim` は今も子ごとに mark / free、`EXEC_DYN_RESERVE` あり、`sys_reserve_top` は PEGC の BB が起動時に使う)、D (activation の予約判定 — transaction 核 `sys_device_reserve_core` だけ存在、呼び手なし → DEVICE_RESERVATION 側)。**見つけた事実**: `memory_boot_init` は **PT workspace を `MEM_APP_BAND_MAX_TOP` より上に取れない構成 (= 8MB 機) では legacy の `pgalloc_init(mem_kb)` に落ちる** — 8MB 機は今もモデル経路を通っていない |
| (3) 置き換わった / 残る | **ほぼ全部が置き換わった**: D2 (物理地図と所有権台帳を先に、T1) は physmem モデルの後継; **D3 (`exec_child_claim` / `sys_reserve_top` / `EXEC_DYN_RESERVE` は撤去)** で A3・C の目標「A/B を永久 claim にする」は**作らずに消す**側に決まった; D6 (15〜16MB は既定で予約) = `MEMORY_BOOT_LEGACY_END`; D11 (RAM の登録上限 2GB) = `MEM_PHYS_RAM_CEILING` を 0x80000000 へ; §2-1 (池は `MEM_POOL_BASE` からモデル経路で全 RAM、**legacy 経路撤去**、T1); R4 (連続確保は起動時だけ) と §2-2 (BB は起動時に池から、owner=boot → gshell) が A2 の `sys_reserve_top` broker 化を置き換える; §7 D は DEVICE_RESERVATION の領分 (1-5)。票の根拠行は 09-13 のもので MD4 (Cirrus 16MiB 窓 → 0xFE000000)・MD5 (hotdeploy)・MD9 (physmem 未リンク) が古い。**残る** (TASK_MEMMAP_V3 に無いもの): (a) §8 の**検出源の妥当性**: 0594h の意味 (MB 単位) を NP21/W と Ra266 以外の機種で確かめていない — 書き込み検証があるので「無かった RAM」側にしか切り詰めないが、機種資料 (`docs/hw`) との照合は未実施; (b) **8MB 機のモデル経路** (上の事実) — T1 の「legacy 撤去」は 8MB で池を成立させることと同義 (D9); (c) H4 の「bitmap と PT が BSS に最大サイズで増えていないこと」— T3 の予算検査に自然に入る |
| (4) どこで | **撤回を提案** (状態行を「撤回 (2026-09-30) — K6 で着地した部分は `memory_boot.c` / `pgalloc.c` に、残りは TASK_MEMMAP_V3 D3 で撤去側に決定」として `docs/archive/settings/` へ)。残る (a)(b) は **T1 の受入に 2 行足す**: 「8MB / 17MB / 64MB のすべてでモデル経路 (legacy fallback 無し)」、「高位 RAM の登録源は `memory_boot_detect` (0594h + 書き込み検証)、機種資料との照合は U 項目」。(c) は T3 |
| (5) 判断 | 撤回に同意するか。同意しない場合の代案: 状態を「完了記録」にして残す (K6 の記録として) — ただし票の本文は K6 の設計とは別物 (A3 / C / D は作られていない) なので「完了」とは書けない。**推奨: 撤回**。D3 の撤去と両立しないので「拾う」の選択肢は無い |

### 1-5. DEVICE_RESERVATION — 任意デバイス窓の予約 broker

出典: [DEVICE_RESERVATION](../../tasks/settings/DEVICE_RESERVATION.md) (計画、09-29 に Cirrus の帯を更新)。V3_PLAN_DRAFT §3 は「P4 の芯」、§4 D17 は「Trident の要求を取り込んで改訂」。

| | 内容 |
|---|---|
| (1) 何を | PEGC (リニア窓 512KB + 主記憶 BB) と Xe10 Cirrus (バンク窓 F60000 + リニア窓 `[0xFE000000, 0xFE400000)`) の窓を、**PFN 半開区間の複数 span を 1 transaction** で owner 台帳に予約する (§4: 全検証 → commit、失敗は全不変、**永久保持**)。予約の前に副作用のある probe を呼ばない (§6: 識別 → 予約 → **写像** → probe / enable → 面公開 → AS 作成)。初回予約は **GUI 境界** (`is_gui` 判定後・`exec_run` 前 = master CR3・live AS=0・exec 不在、§5)。`[MEM_EXEC_LOAD_ADDR, sys_usable_mem_end())` 全域を将来用途として禁止。§5 の未解決: 後発 GUI の PEGC BB (`sys_reserve_top` は使えない) |
| (2) 今 | **transaction 核は着地**: `sys_device_reserve_core(owner, spans, count, cap)` (`kernel/sys.c`) → `pgalloc_device_reserve` (複数 span、owner、capability `IDLE` / `RAM_MAPPED`、`SYS_DEVICE_MAX_SPANS` 16)、ホスト試験 `test_device_reservation.py`。`include/sys.h` の注記「**No current GUI/backend supplies this capability: integration is pending**」のとおり**呼び手は無い**。Cirrus のリニア窓は v3 のデバイス窓の帯 0xFE000000 へ移設 (3f088fc9、09-29。先頭 4MB の PT は静的、auto は NP21/W の上でだけ) — §3 の表は追従済み。**未着地**: gfx バックエンドの識別 / probe の分離 (PEGC の probe は今もモード変更を伴い、BB は `sys_reserve_top` を起動時に取る; Cirrus の probe は SR6 を書く)、写像 → enable の順の逆転、GUI 境界の挿入点、owner 台帳 (核の中の固定配列だけ)、Trident の実測 BAR を渡す口 (TRIDENT §4-2 の 1〜3) |
| (3) 置き換わった / 残る | **置き換わった**: (i) **owner 台帳と MMIO の登録は T1 の台帳** (D2: 種別 RAM / MMIO / 予約 / SURFACE、owner タグ kernel / boot / gshell / モジュール名。§2-3 ⑥ は PEGC のリニア窓を「台帳に MMIO 登録した範囲」として地図検査の期待値にしている) → 核 `pgalloc_device_reserve` は T1 の台帳の上に**載せ直す** (複数 span の transaction と永久保持の規則は T1 の MMIO 登録の仕様として残す。試験は流用); (ii) **§5 の PEGC BB の問題は消えた**: D19・§2-2「BB は起動時 (gfx probe の直後、live AS=0) に台帳から連続確保し owner=boot → gshell へ移譲、GUI 終了後も保持」+ D3 で `sys_reserve_top` 撤去。「後発 GUI で BB が取れず PEGC を拒否」「8MiB PEGC の維持は達成しない」の両方が不要; (iii) **exec arena 全域の禁止**: D3 で arena そのものが無くなる (アプリの物理は池から owner 付き) → 衝突判定は「台帳の他 owner と重なるか」に単純化。**16MB 構成で PEGC 窓が arena と衝突して拒否**、も D6 (15〜16MB は RAM にしない) で消える; (iv) §6 の 4「client 面だけ `paging_addrspace_map_user_keep()`」と TRIDENT §4-2 の記述子 (`virt` / `phys`) 要件 (V3 §4 D18) は **§2-2 の lease (SURFACE を AS の私有 PT に張る) が上位互換**; (v) §2-1「PCI の BAR の PT は列挙時 (`exec_init` より前) に作る」で、Trident の枠の PT 確保時期 (TRIDENT §4-2「T8 の境界で」) も起動時側に寄る。**残る** (TASK_MEMMAP_V3 に無いもの): (a) **順序契約**: 副作用のない識別 → 予約 (台帳 commit) → 写像 (sup+PCD、全戻り値検査) → probe / enable → 面公開、失敗時の永久保持と「予約拒否」と「hardware probe 失敗」の区別、`s_probed` に予約失敗を永久 cache しない (§4 所有と寿命、§6) — gfx バックエンド 2 本 + glue の改修; (b) **Trident の実測 BAR の口** (定数だけ → 検証済み実測範囲、TRIDENT §4-2 の 1〜3、V3 §7-2 X4 で Codex に問う予定); (c) 単独の CPL3 アプリからの初回 `gfx_init` で任意 backend を activation しない規則 (§5) — D10 でバックエンドは CPL=0 モジュールになるので「準備済みの候補の再 init だけ」の形で残る; (d) **予約と probe の時期**: 票の §1・§5 は「GUI 起動要求を受けてから、GUI 境界で」、TASK_MEMMAP_V3 §4-5 の起動順は「ルートマウント → **gfx probe + BB 確保** → `exec_init` → モジュール → スプラッシュ → シェル」(起動時)、TRIDENT T8 の決裁 (i) は「GUI 境界で準備」— **3 か所で時期が揃っていない** |
| (4) どこで | **P4 (帯の資源割当) — T1 の後**: (i) 核と台帳 → **T1** (`pgalloc_device_reserve` を台帳の MMIO 登録 API に載せ直す、`test_device_reservation.py` を流用); (ii) 順序契約 + Trident の口 + バックエンドの識別 / probe 分離 → **P4 の票** (T2 の後、Trident 段 3 = P5 の前。TRIDENT §4-2 の「同票へ追加」はこの改訂で受ける); (iii) §5 (後発 BB) と exec arena の禁止 → **削除** (D19 / D3 で不要); (iv) 票の状態行は「計画」のまま、本文に「T1 の台帳を前提に改訂する」の注記を足す (改訂は P4 の着手時) |
| (5) 判断 | **予約と probe の時期**: (a) **起動時** (TASK_MEMMAP_V3 §4-5 の順。BB がどのみち起動時に確保されるので、窓の予約と非破壊の識別も同時に。GUI=0 でも識別まで行い、破壊的な probe / enable は GUI 起動時) / (b) GUI 境界 (票の §5 のまま。TRIDENT T8 (i) と一致) / (c) 識別と予約は起動時、probe と enable は GUI 境界 (折衷)。**推奨 (c)** — §4-5 の「gfx probe + BB 確保」を「識別 + 予約 + BB 確保」と読み替えるだけで済み、GUI=0 の CUI 機で装置を触らない §1 の既定も守れる。決めたら TASK_MEMMAP_V3 §4-5 と TRIDENT T8 の行を揃える。**もう 1 点**: 許可範囲を「既知候補の定数だけ」から「検証済みの実測 BAR」へ広げること (Trident のため) を認めるか — 認めるなら V3 §7-2 X4 を Codex に問う |

---

## 2. ユーザーの判断が要る点 (まとめ)

| # | 判断 | 選択肢 (推奨に ★) | 出典 |
|---|---|---|---|
| 1 | **F3b (lock 表) の形** | (a) lock 表を P10 で / ★(b) 「同一 DB の排他 open」を TASK_DICT_META の後 (v3 後半) に / (c) v4 | §1-1 |
| 2 | **F2 の残り** (F2b 呼び出し側 + F2c + F2d 隔離 + R0/R1) | ★(a) T4 + T5a の受入に畳む / (b) T4 の前に P10 の独立票 / (c) 捨てる (hot journal 1 つの契約で受ける) | §1-2 |
| 3 | **FEP_BOUNDARY の拾い方** | ★(a) B1 → T2、B4 → T4、B2 → T5a、B3 → T4 の要件として / (b) T5a の後に独立票 / (c) 拾わない。**付随**: 旧 `db_exec` / `db_prepare` の 1024B 超を「切捨てて実行」から「失敗」へ変えてよいか (★変える、D7 の範囲) | §1-3 |
| 4 | **MEMORY_RAM_INTEGRATION の撤回** | ★撤回 → archive、残る 2 点 (8MB もモデル経路 / 検出源の機種照合) を T1 の受入へ / 完了記録として残す (本文と実装が別物なので不正確) | §1-4 |
| 5 | **DEVICE_RESERVATION の時期と範囲** | 予約 / probe の時期: (a) 起動時 / (b) GUI 境界 / ★(c) 識別 + 予約は起動時、probe + enable は GUI 境界。**範囲**: 定数だけ → ★検証済み実測 BAR も (X4 を Codex へ) | §1-5 |
| 6 | **U6 の答え** | ★「一部」= F3a / F3c / F2 / FEP_BOUNDARY / DEVICE_RESERVATION を形を変えて拾う (P10・P4、多くは T 票の要件)、MEMORY_RAM_INTEGRATION は撤回、F3b は 1 の結果次第 | §0 |

決裁の後に PM が行うこと (**2026-09-30 に反映済み** — TASK_MEMMAP_V3 D29〜D34、この状態行を参照): 各票の状態行の更新 (MEMORY_RAM_INTEGRATION → 撤回 + archive、DEVICE_RESERVATION に改訂の注記、F2 / FEP_BOUNDARY に「T4 / T5a の要件として拾う」の注記)、TASK_MEMMAP_V3 §6 の T1 / T2 / T4 / T5a の受入への追記、V3_PLAN_DRAFT §3 P4 / P10 と §7-1 U6 の行の書き換え、ROADMAP §1 v1.3 の「保留 5 件」の行。

---

## 3. 確かめた根拠 (2026-09-30、HEAD 2a6cc836)

| 何を | どこで見たか |
|---|---|
| F3a の未着手 | `lib/sqlite3/os32_sqlite_vfs.c`: `os32Read` / `os32Write` (seek の rc 無検査、`(int)iOfst`)、`os32Truncate` (no-op で `SQLITE_OK`)、`os32Sync` (`vfs_sync()` の戻りを捨てる)、`os32VfsDelete` (`vfs_rm` の rc を捨てる)、`os32VfsAccess` (stat 失敗 = 不存在)、`os32VfsFullPathname` (`kstrncpy`)、`os32Lock` / `os32Unlock` / `os32CheckReservedLock` (no-op)。先頭コメント「シングルタスク: ロック no-op」「xTruncate: ext2 truncate 未実装のため no-op」 |
| F3c の公開側 | `git log`: 08b48790 (KAPI v50 `db_open_existing` RO/RW・CREATE 無し)、77d61b30 (stat は NOTFOUND だけ不存在、他は IOERR)、b99f30ab / 1cd5dc5f (FD の STALE、SQLite 経路の失効検査、開いている DB の rename は BUSY) |
| F2 の着地範囲 | `fs/vfs_fd.c` (`vfs_open_sqlite` … `vfs_quarantine_sqlite`)、`lib/sqlite3/os32_sqlite_vfs.c` (`os32_sqlite_group_*`、be445f4e)、`exec/exec.c` の `exec_reclaim_owned` (`db_cleanup_owned` → `vfs_close_owned` → `shm_free_owned`)。**呼び手が無い**: `kapi/kapi_db.c` は `sqlite3_open(path_copy_buf, …)` と `sqlite3_open_v2(abs_path_buf, &db, flags, NULL)`、`kernel/ime_dict.c` は `sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL)` — `os32_sqlite_group_open` の利用は `lib/sqlite3/` の内側だけ |
| FEP_BOUNDARY の未着手と周辺 | `kernel/ime.c` `ime_user_list_facade / delete_facade / export_facade` (素通し)、`kapi/kapi_generated.c` `wrap_ime_user_list` (`ring3_user_ranges_writable` で出力範囲を検査 — TASK_KAPI_OUTPUT_GUARD)、`kapi/kapi_db.c` `db_user_str_copy` (1 バイトずつ検証、切捨てない) は `db_open_existing` / `db_prepare_only` だけ、旧 `db_exec` / `db_prepare` は `kstrncpy(sql_copy_buf, …)` で finalize の後にコピー、`kernel/isr_handlers.c` は `__sqlite_start / __sqlite_end` で EIP を分類して表示するだけ、`enter / leave` 相当の状態は grep で無し |
| MEMORY_RAM_INTEGRATION の着地範囲 | `git log -- kernel/physmem.c kernel/pgalloc.c kernel/memory_boot.c kernel/sys.c`: 102b4796、f6ec5209、f3abdb34、1a559a07、7faf61f5、01b5751d。`build/kernel.mk` に `kernel/physmem.c kernel/pgalloc.c kernel/memory_boot.c`。`kernel/memory_boot.c` の `memory_boot_detect` (0594h + 書き込み検証)、`memory_boot_init` (legacy 15MB clamp、metadata / workspace の配置、**8MB では `pgalloc_init(mem_kb)` へ fallback** のコメント「PREINIT choice only. In particular 8MiB has no shared workspace above APP_BAND_MAX_TOP; preserve its legacy allocator」)。`exec/exec.c` の `exec_child_claim` / `EXEC_DYN_RESERVE`、`kernel/sys.c` の `sys_reserve_top` は現存 |
| DEVICE_RESERVATION の着地範囲 | `kernel/sys.c` `sys_device_reserve_core`、`kernel/pgalloc.h` `pgalloc_device_reserve`、`include/sys.h` の注記 (integration is pending)、`tools/tests/test_device_reservation.py`。呼び手の grep は試験だけ。`gfx/backend_pegc.c` は `sys_reserve_top(MEM_GFX_BB8_SIZE)` と `pegc_probe()`、`gfx/backend_cirrus.c` は `cirrus_probe()` → `paging_map_phys(lin_base…)` の順 (写像は probe の後)。3f088fc9 (Cirrus の窓を 0xFE000000 へ) |
| TASK_MEMMAP_V3 の該当決定 | D2 / D3 / D6 / D7 / D9 / D10 / D11 / D19 / D21 / D26 / D27 / D28、§2-1 (帯の表)、§2-2 (lease)、§2-3 ⑥⑦、§3-5 R1 / R4 / R7、§4-2 (エクスポート表)、§4-5 (起動順)、§4-6 (FEP の取り分)、§6 (T1 / T2 / T4 / T5a) |
| 文書検査 | 起点で `make check-docs-links` / `check-docs-orphans` / `check-docs-status` はいずれも OK (この草案を足した後も同じ 3 つを通す) |
