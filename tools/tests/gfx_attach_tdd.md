# T2e e6 — C SDK checked attach と互換 void 橋

票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §2 e6。

対象: `test_gfx_attach.py` / `gfx_attach_host.c`。正常対照は指定された全
HOST32_RUNNERS、変異は先頭 runner。kernel/query/lease/ledger/paging/3 backend と
SDK core・描画 C の実物を連結する。SDK と kernel の同名描画関数だけを試験の写しで
改名し、KAPI member 名は維持する。CS は試験用の CPL 値へ置換する。
I/O/probe/MMU は e4 の足場、asm は C の門を通った回数を数える。
SDK の lease VA は host に確保した領域であり、実 CR3/TLB と物理への alias は
ゲスト合格ではない。SDK port は実物 surface_query/surface_lease の B1 コピーを
アプリ帯 scratch 経由で呼び、release は実物 lease_release。

## 正常対照

qemu ILP32 で 268 条件成功。3 backend の pointer/geometry/format、200 行でも
32000 B stride、query-only の反復取得、世代変更後の再取得、旧 VA を他 token が
占有した場合の新 VA、8 slot FULL、取得後 view/geometry 不正の巻戻し、not-ready、
TRUSTED/未結線 legacy 経路、compat token/flags/B1 出力/abort を検証。
描画拒否は clear/fill/blt/surface/sprite/font/save/restore/present/raster の実 C 入口を通す。
初回 FULL → slot 返却 → present 回復後の surface/sprite 作成・描画、
再 attach で既存 surface/sprite を保持、TRUSTED の不正出力先で abort しないことも検証。
公開ヘッダ gnu89 は既存 check-c-dialect が検査する。

## RED → GREEN

正常ソースは GREEN、私有コピーへの以下の変異だけで目的の assertion が RED。
復元した正常ソースを毎回の開始に実行する。変異は 15/15 runtime RED。

| 変異 | 検出する違反 |
|---|---|
| skip-failed-pool-init | 初回 FULL でプール初期化を飛ばす |
| reset-live-pools | 再 attach で使用中プールをリセット |
| bridge-trusted-abort | TRUSTED 失敗で abort を立てる |
| attach-two-tokens | 反復 attach で 2 本消費 |
| skip-generation | revoke 後に旧世代 view を再利用 |
| partial-no-release | 取得後の不正 view で token を返さない |
| draw-after-failure | ready=0 でも clear の asm/dirty へ進む |
| retain-old-bb | 新 VA 取得後も旧 pointer を残す |
| bridge-user-alias | USER に kernel alias を返す |
| compat-no-reuse | 互換 token を反復利用しない |
| trusted-query | CPL=0 で USER query を呼ぶ |
| bridge-no-abort | 互換 void 失敗後に終了要求がない |
| bridge-dirty-output | 失敗時の out に pointer が残る |
| metadata-in-pte-check | compat 印を PTE 属性に混ぜる |
| present-no-query | present の世代照合がない |

全変異は rc=1 と固有の FAIL 文言を照合。初回足場のコンパイル/リンク不備と
試験追加後の FAIL 文言更新は修正し、runtime RED には算入しない。
レビュー修正後の単独実行では最短 0.83 秒、最長 2.86 秒 (並列4)。
試験足場の SDK allocator は複数の独立した領域を返すよう修正した。
初回は足場のメモリ不足、TRUSTED 失敗条件の選択、present 変異の先行 FAIL 文言で
rc=1。いずれも足場/期待値を修正して再実行し、15/15 の runtime RED を確認した。
依存 closure は e4 runner を使い、正常 object を hash で再利用し、入力 hash を
開始/終了で照合する。既存 e4 10/10・e5 11/11 変異も意図を維持して成功。

コマンド (PYTHONPATH 空、TMPDIR=/home/hight/os32-tmp):
`OS32_MUT_JOBS=4 python3 -B tools/tests/test_gfx_attach.py --runner qemu --mutate`。
最終全体検査の実行中はこの記録とソースを固定し、rc はコーダーの完了報告へ記載。
