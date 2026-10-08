# T2f f1b 実 nano の単体接続記録

票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §3-5 f1b。

2026-10-03、Codex gpt-6-astra、wt/t2f1b、基点 d4d5a2b。
`test_nano_adapter.py --runner qemu --mutate` は rc=0。
ILP32 は実 toolchain の newlib ABI でコンパイルし、host gcc の
`-m32 -nostdlib -static -no-pie` でリンク、`host32.run` 経由で実行する。

- 正常対照: 88 CHECK (malloc/free/calloc/realloc と reentrant 入口、穴再利用、
  不整列 break の追加取得と後半失敗、末尾不足分だけの取得と失敗後の再利用、
  USER EXACT の成功/失敗、resident 固定上限、負増分/INT_MIN/加算overflow/
  page丸めoverflow/丸め後上限、busy中の全入口再入とarena選択拒否、
  2 arena のfree list/start/統計/生存データ分離、境界で隣接するchunkの所属拒否と
  非併合、実libc_a-reallocf.oの内部reentrant呼出し、realloc失敗で旧内容保持)。
- リンク負例46件 (driverで実数を集計): 未結線22名、接続8名の重複をadapterの前後で16件、
  sbrk/_sbrk/_sbrk_rの独立した定義をadapterの前後で6件、
  実newlibのmalignr/msizer 2 member。全リンクを `--allow-multiple-definition`
  で成立させた後、選択された入力の全定義を検査して拒否し、出力物も削除する。
- C変異21件: 統計復元/保存、負増分下限、加算overflow、丸め後上限、
  busy中の選択/再入、free list復元/保存、sbrk_start復元/保存、固定上限、
  EXACT失敗無視、mapped_endの非page化、breakの非連続化、返値の非連続化、calloc迂回、pointer所属検査迂回、page切り上げoverflow迂回、
  resident分岐をUSERにも強制、busy外morecore許可。
  各置換の当たり数は1 (返値だけ2)、実行rc=1かつ変異ごとの期待CHECK式 (文字列ラベル) と一致した失敗だけRED。
- Pythonリンク検査変異3件: 未結線/重複/独立break所有者の拒否を迂回。構文compile後、
  成立した実リンクに対する実行時AssertionErrorだけRED。
- 最終: 24 runtime RED / 0 survived / 0 ERROR。

途中のfixtureコンパイルは符号比較・巨大定数の警告で失敗し、型とreentrant呼出しを修正。
初回の丸め後上限変異は生存した (高位の丸めoverflow試験だけでは独立した上限分岐を
通らなかった)。通常番地の非page上限を追加してREDを確認。これらをRED件数に含めない。

独立レビュー対応: Codex gpt-6.1-sol (2026-10-03)。追加CHECKの符号比較警告は
size_tへ修正。既存変異の期待ラベルを合わせる途中、free list restoreとstart restoreが
想定より早いCHECKで失敗したためERRORとして扱い、最初の検出点を明記して再実行。
これらの途中結果は最終RED件数へ加算しない。normal archiveのinputs.checkは1回だけ、
変異は検証済み私有memberを再利用してadapterだけを差し替える。

全runnerの正常対照はMake recipeで列挙し、変異は先頭runner。
このsandboxではqemuのみ実施、nativeはPMホストで実施する。
public link/実CRT/自動副arena は f6/f7、大塊/Rust整列は f8、trim は f11 で実装。

f11 (2026-10-08、基点 f43344c): 実 nano の末尾 free list のみを対象に、
primary/副 arena の部分 unmap・再伸長・末尾不足分の併合を検査する。
EFULL「中抜きで slot 不足」/EINVAL は callback で返し、全状態と生存全バイトを照合。
page 先頭/中ほど/末尾の header、image page、USED 末尾、直接 sbrk、busy 再入、
空副 arena の再返却、arena ごとの成功ページ数の和と失敗後継続も対象。
非選択 arena の live size を毒値にしても結果は変わらず、物理 chunk の検査を足す変異を検出する。
新規 6 変異は既存 32 変異と期待 CHECK の対応を維持する。image 端 page の floor 除去は
有効な tail ≥ initial かつ page_up(tail+12) ≥ page_up(initial) なので等価変異として除外。
ホスト正常/変異・リンク負例と検査対象は `/home/hight/os32-tmp/run/f11/f11_report.md`、
`f11_check_scope.json` と `f11_*.log`。ゲストと native の持越しは台帳 F-13。
