# kernel/ime/ime_dict.c — システム辞書と学習 DB の 2 接続、検索の合成、切替

> 状態: **現行 (2026-11-18)** — 出所: DICT_META_DESIGN・TASK_DICT_META (要約して移した)
ソース: `kernel/ime/ime_dict.c`・`kernel/ime/ime_dict_api.inc`・`kernel/ime/ime_suspend.c`・`kernel/ime/ime.c` (`ime_dict_boot`)・`kernel/ime/fep_body.c`

型・定数・誤りの意味は [ime_store.h の契約](ime_store.md)、学習 DB の中身は [ime_user_store](ime_user_store.md) にある。

## 何をするか

- システム辞書を読み取り専用 (RO) で、学習 DB を読み書き (RW) で、別々の常駐の group に開く (`FepStore`)。
- 変換では学習候補を先に返し、その後にシステム辞書の候補を続ける。学習は学習接続だけに書く。
- S/M/L の辞書を切り替える。学習 DB は 3 つの辞書で共通 (同じ `dict_id`)。
- 旧い 1 接続の口 (`ime_dict_open`・`ime_dict_search`・`ime_dict_reopen` ほか `IME_Dict`) はファイルの前半に残っているが、製品の経路はもう通らない (host 試験だけが呼ぶ)。

## 開く (`fep_store_open`)

起動 (`ime_dict_boot`) と、停止からの再開 (`ime_dict_suspend(0)`) が呼ぶ。

1. 学習 DB のパスを `user.path` に写す。長すぎるパスは切り詰めず、学習を止める。
2. `fep_meta_probe` で辞書を検査する (core の口、一時の RO 接続を開いて閉じる)。失敗ならそのまま返す。
3. 予約の照合: 辞書の `mem_reserve_kb` が `max(boot_reserve_kb, 今の meta の予約)` を超えたら `AGAIN` (「restart required」)。
   起動時に受け入れた辞書を、settings の小さい予約を理由に再び断らないためにこの形にしてある。
4. `store_system_open`: RO で開き、`PRAGMA cache_size=<meta の cache_pages>`、`exact`・`prefix` を準備する。成功してから `s->meta` を差し替える。
5. 学習 DB を開く (`fep_user_open` と文の準備)。失敗しても変換は続ける (戻り値 0)。理由を赤で表示する。

起動では常に `IME_DICT_PATH` (M) を開く。起動の前の S/L の選択は保存されない。

## 切替 (`fep_store_switch`、KAPI `ime_switch_dict`)

`ime_switch_dict(0|1|2)` が S・M・L を選ぶ (`ime_dict_api.inc`)。それ以外は -1。

1. どちらかの接続が隔離されていれば `BUSY` (再起動の案内)。
2. **今と同じパスなら probe せず `fep_conn_ready` だけ返す。** 開いている inode を新しい接続で開くと VFS の同一 inode の排他で失敗するため。
3. 切替先を `fep_meta_probe` で検査する。この間、system・user と probe の 3 本の常駐 group が同時にある。`OS32_SQLITE_RESIDENT_GROUPS` が 3 なのはこのため。
4. 切替先の `mem_reserve_kb` が `boot_reserve_kb` を超えたら `AGAIN`。今の辞書はそのまま。予約はここで増やさない。
5. 旧 system を閉じて新しい辞書を開く。開けなければ旧パスで開き直す。それも失敗したら「dictionary unavailable; restart required」。学習データは残す。
6. `dict_id` か品詞の版が変わったら学習接続を閉じて開き直す (`fep_user_open` が不一致を検出して学習を止める)。

## 検索の合成 (`fep_store_search`)

- 読みが 3 文字以上なら前方一致、2 文字以下なら完全一致の文を使う。
- 学習候補を先に採り、残りを system の cost 昇順で埋める。system の前方一致は完全一致の行だけ `cost-500` とする。
- 学習候補は入力した読みと一致するものを優先し、同じ読みでは直近の確定順（`last_ts` 降順）、同順位なら頻度順とする。両方の検索に 32 行の上限がある。
- 採った行の数 (重複・NULL を含む) が `IME_MAX_RESULTS` (32) か `max` に達したら打ち切る。重複は完全な表記で除き、除いた分は補充しない。
- 結果の欄に収まらない長い表記は、重複の比較のために全体を `kzalloc` で写す。SQLite の TEXT のポインタは次の step・reset で失効するので持ち越さない。
- 非短縮の罰則: cost が非負で、表記の byte 長が読み以上なら `IME_NONSHORTENING_COST_PENALTY` (300) を足す。
- 学習側が途中で失敗したら、合成の結果を全部捨て、system だけで検索し直す。system の I/O 失敗は system を回復してから 1 回やり直す。

## 回復 (`store_recover`)

- 接続ごとに行う。学習の障害で system を閉じない。
- 開き直すのは FD の失効 (`STALE`) か、初回の I/O 失敗 (`io_retried` が 0) のときだけ。
- 開き直す前に `<path>-journal` を見る。空でないジャーナルが残っていればその接続を止める (ホットジャーナルを別の inode で開かない)。
- system を開き直して辞書の ID・品詞の版が変わっていたら、学習接続も検査し直す。

## 学習と学習 DB の管理

- `fep_store_learn`: `INSERT … ON CONFLICT(yomi,kanji) DO UPDATE SET freq=freq+1, last_ts=excluded.last_ts`。確定時に同じ読みの最大の `last_ts` に 1 を足し、確定順として保存する。学習が止まっていれば何もしない。
- `fep_store_user_list`・`_delete`・`_export`・`_clear` (KAPI `ime_user_*`) は学習接続だけに送る。確定時に同じ読みの最大の `last_ts` に 1 を足し、確定順として保存する。学習が止まっていれば -1。
- KAPI の入口 (`ime_dict_api.inc`) は呼び手の番地を検査し、結果を内部の staging に置いてから写す (`copy_to_caller`)。

## 停止と再開 (`ime_dict_suspend`、KAPI、`hsync --fep-off`)

- `on=1`: 今の辞書と学習のパスと入力モードを保存し、IME を OFF にして両接続を閉じる。
- `on=0`: 保存したパスで `fep_store_open` し、モードを戻す。辞書が大きくなっていれば `AGAIN` で FEP を止めたままにする。
- 使用中の辞書のファイルは差し替えない。更新は「選んでいない辞書を更新 → 切替 → 元の辞書を更新」か、この停止の間に行う。

## core との境目

- 起動後、core は `ime_boot_connections()` (`fep_body.c`、実体は `fep_store_live_connections`) で常駐の接続数 (学習ありで 2、なしで 1) を受け取り、`os32_sqlite_outer_boot_check(live, reserve)` に渡す。閉じ損ねた group も数える。
- 予算の状態の持ち主は core と SQLite。`boot_reserve_kb` は参照用の写し。

## 落とし穴

- 製品の SQLite は `CAST` を省いて組む (`lib/sqlite3/os32_sqlite_config.h` の `SQLITE_OMIT_CAST`)。FEP とゲストの SQL に `CAST` を書かない。
- 同じ辞書を選び直すと probe が排他に当たる (上の切替の 2 で避けている)。
- 予約の照合の位置を動かすと、起動の経路 (`fep_store_open`) と切替の経路の両方に効く。直したら起動の試験 (`sqlite-module-proto-host`・`fep-core-boot`) も回す。

## 検査

- `fep-store-search` (`tools/tests/test_fep_store_search.py`): 合成の順・同点・打ち切り・学習の失敗で system だけに戻ること・学習の有無で接続数が 1/2。
- `fep-boundary-host` (`tools/tests/test_fep_boundary.py`): 製品の入口が store を使うこと、切替・起動の失敗の扱い。
- `fep-meta` (`tools/tests/test_fep_meta.py`): 実物の SQLite で probe・切替・旧形式・未来版。
- `fep-budget-host`・`fep-worst-load-host`・`fep-standard-host` (`tools/checks/`): 2 接続と probe の最大の使用量が予約に収まること。
- `hsync-fep-host` (`tools/tests/test_hsync_fep.py`): 停止と再開を hsync から。
- ゲスト: `userland/tests/fep_meta_test.c` (S→L→S で学習の頻度が残る、予約を超える切替で今の辞書が残る、ID・品詞の不一致で学習が止まり変換は続く)。
