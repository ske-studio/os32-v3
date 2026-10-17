# T4a-3 — MEMSYS5 hook / タグ方式の host prototype

契約: [TASK_T4_T6_MODULES §3-3、§7-1 T4a-3](../../docs/tasks/v3/TASK_T4_T6_MODULES.md)。
製品には結線しない。製品 SQLite・FEP・割当器・KAPI・配置定数の変更はない。

`make check-sqlite-memsys5-host MUTATE=0 HOST32_RUNNERS=qemu`。
検査は `~/os32-tmp/bin/check_slot.sh` を通す。
`CROSS_DIR` の i386-elf GCC / newlib で SQLite を GNU89、器具を GNU11 として
コンパイルし、`host32` の明示 runner で ILP32 実行する。host の multilib は不要。
SQLite の設定は製品の `os32_sqlite_config.h` をそのまま使う。
OS 境界だけを :memory: 用 VFS に替え、file open は失敗して回数を検査する。
Linux 側の出力と終了だけを実装し、newlib の sbrk は失敗する。

## 実装と観測

`sqlite_memsys5_proto.c` が `HEAP → GETMALLOC → MALLOC → initialize` の順で
実 MEMSYS5 を保存・ラップする。前置き方式は8 Bのownerタグを追加し、
外部表方式は `{pointer, owner}` の固定4,096枠を使う。後者の表は ILP32 で32,768 B。
どちらも heap・タグ・固定管理領域 (保存したallocator表、文脈、観測計数を含む)
を合わせ512KiB以内に置く。外部表はその予算の内側で、heapと別の範囲を使う。
前置きタグはheap内の実ブロックに含め、二重に足さない。
MEMSYS5自身の管理バイトと丸めの余りも渡したheapの内数。

`xRoundup` は前置きタグ込みの実丸め量からタグ分を引いたpayloadサイズを返す。
`xSize` は同じpayloadサイズ、ownerへの請求は元 `xSize` の実ブロック全量。
小さくするreallocでもMEMSYS5が旧blockを保持する限り請求量を減らさない。
free/reallocは呼出し文脈でなく元のタグを使い、表満杯なら元のmalloc前に拒否する。
既存blockのreallocは満杯でも元の枠を更新できる。
APPの予約門・共通予約量・APP上限はT4a-4の対象で、このprototypeでは決めない。

| 通常対照 | 判定する観測 |
|---|---|
| hooks (2方式) | 13サイズのbuddy境界±1、8 B整列、heap内、payload長と実請求量、異文脈free、init/shutdown各1回 |
| realloc (2方式) | 実MEMSYS5に512KiB要求を渡して失敗、旧全payload/サイズ/各勘定/SQLite使用量保持。その後の移動成功・縮小・NULL/0・異文脈回収 |
| exhaust (2方式) | 1KiB要求を実heap枯渇まで保持、全丸め量と各勘定一致、タグ・固定管理込みの予算、全回収 |
| sql (2方式) | SYSTEM scratch、FEP接続と辞書作成、APP prepare/bind/1語検索、SYSTEM文脈でfinalize/closeし全回収 |
| capacity (外部表) | 4,096枠到達、拒否時の元malloc呼出し0/勘定不変、満杯realloc、異文脈free、全4,096枠の再利用 |

`peak_bytes` はラッパの呼出し完了時の実請求量の最大で、元MEMSYS5の
realloc内部の旧新block同時生存ピークを含まない。`heap_control_slack` は
heap長から保持block請求量を引いた値であり、最大連続空きや断片化保証ではない。
ここで使う小さな :memory: 辞書はS/M/L最大負荷fixtureではない。

## 変異の接続と引渡し

`--mutate` は写しのprototypeにだけ、丸め勘定削除・タグ解放削除・realloc失敗時の
勘定破壊・表満杯時の先行割当・タグ予算の誤計上・割当時文脈の欠落を入れる。
通常対照がすべて成功してから、各変異をビルドし、名前つき `FAIL` とrc=1を要求する。
コンパイル失敗・signal・timeoutは検出成功に数えない。
実装者はMUTATE=0だけを実行し、変異の実行と判定はレビュー後のPMへ渡す。

実測・コマンド・rc・初期の環境/ビルド失敗は
`~/os32-tmp/run/t4a/report_T4a-3.md` と同ディレクトリのログに残す。
T4a-4ではこの器具を使ってS/M/L全切替・sort/prepare圧迫・変換/学習・断片化を
測定し、表容量、タグ方式、共通予約量とAPP上限を決める。USER mallocは別計上。
線形走査する外部表の性能もP100の確定値とはせず、ここではRAMと挙動を比較する。
