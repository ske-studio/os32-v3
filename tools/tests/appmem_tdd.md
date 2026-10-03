# T2f f2 extent / 穴探索のホスト試験記録

票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §3-5 f2。

対象は [appmem.c](../../exec/appmem.c) と [appmem.h](../../exec/appmem.h)。
正典 [TASK_T2D_T2H §3-5](../../docs/tasks/v3/TASK_T2D_T2H.md)、
PM 判断 `/home/hight/os32-tmp/ref_f2_pm.md` Q1〜Q8/Q10 に従う未結線の先行準備。
kernel の判定を fixture に複製せず、実 appmem.c を ILP32 でリンクする。
MMU/物理確保/呼出元の検査は f3/f5、unmap は f4。ゲスト試験は範囲外。

## 正常対照と負例 (2026-10-03、GPT-6 Codex)

`python3 -B tools/tests/test_appmem.py --runner qemu --mutate` rc=0。
136 CHECK GREEN、13/13 runtime RED、生存0、compile/link ERROR0、signal0、timeout0。
変異は source の写しだけに適用し、置換当たり数1を確認する。
共通 fixture object は再利用、変異 TU だけ再コンパイル/再リンク。
入力 hash は開始/終了で確認。実ソースを改変しない。

- 両窓の最下/最上ページ、窓全体のちょうど一致、1ページ不足、
  断片穴のちょうど一致/不足、空窓、占有済み上端の次候補。
- size0、未知 flags、非整列/帯外 hint、EXACT+NULL、
  `0xfffff001` の丸め overflow、hint+丸めbytes の wrap。
- 両窓の hint 優先 (flags0/TOPDOWN の窓を越えて希望可能)、
  EXACT|TOPDOWN の EXACT 優先/衝突失敗、hint 衝突後の代替と旧表不変。
- image/BSS 端ページ/初期 primary heap/初期 exec_heap/stack/guard の除外。
  shlib、lease、低位、未使用上位の hint は INVAL。
- flags0 は下側窓の上端から探索し primary break 直上を先に塞がない。
  TOPDOWN は guard 直下から探索し exec_heap 現在端を下回らない。
- prepare は成功時も表不変、失敗時は出力 proposal も不変。
  publish の先頭/中間/末尾挿入、片側/両側併合、末尾全欄ゼロ。
- 同kind/同flagsのみ併合、EXEC_LARGE は同識別子でも非併合。
  固定32本 FULL と表/出力不変、満杯でも片側併合可能、両側併合でslot回復。
  INVAL / ENOVA / EFULL を区別し、不整合 table/layout も拒否。

## 変異による実行時 RED → 原本 GREEN

新規実装なので既存の未実装版へのコンパイル失敗を RED には数えない。
実装後、下記の判定を写しで壊して失敗ラベルを照合し、原本の正常対照を確認した。
最初の版は134 CHECK/13 RED、目的別負例の文言と EXACT|TOPDOWN 衝突を追加後136 CHECK/13 RED。
最終対象実行の中央値0.14秒、最大0.18秒 (OS32_MUT_JOBS=4、compile+run)。
ログ: `/home/hight/os32-tmp/f2-appmem.log`。

| 変異 | 期待 FAIL |
|---|---|
| EXACT を別 VA 成功にする | exact collision |
| hint 重複を上書きする | exact collision |
| flags0 下端探索で break を妨げる | flags0 preserves break |
| kind 併合条件を緩める | distinct extents |
| flags 併合条件を緩める | distinct extents |
| EXEC_LARGE を併合する | distinct extents |
| FULL 時に表を変更する | full table unchanged |
| 丸め overflow 検査を外す | round overflow |
| hint 加算 overflow 検査を外す | hint addition wrap |
| TOPDOWN が hint/EXACT に勝つ | lower hint topdown |
| 併合前に FULL 判定する | full left merge prepare |
| TOPDOWN の現在端境界を無視する | upper short |
| publish の残片移動を省略する | full bridge sorted tail |

runtime RED は rc=1 かつ期待 FAIL の完全な1行だけ。別ラベル、signal、
compile/link error、timeout は ERROR として合格に数えない。
`mutpar.run_ordered` で13本を固定並列に実行し、失敗分類も集計する。

## 接続境界

`appmem_plan` は36 B、table は512 B、extent は16 B。
prepare〜PTE staging〜publish は非再入・同一表のまま直列化すること。
publish は成功 proposal 専用の内部口で、古い proposal や caller metadata を受けない。
extent.flags は map flags とは独立した内部属性/EXEC_LARGE 識別子。
固定領域は layout で除外し、extent に数えない。
f5 の AS 埋込みで型移動とtarget sizeof予算検査を行う。
kernel.mk 未変更、カーネル増分0 B。native/ゲスト/独立レビューは PM へ引渡す。

## f3でのP3持ち越し対応 (2026-10-03)

publish入口のcount/range_free検査と古いproposalの2拒否対照を追加し、139 CHECK。
未知flag検査省略 (`unknown flag`)、非整列hint検査省略 (`unaligned`)、
hint下限をshlibへ緩和 (`outside`) の3変異を追加。合計16/16 runtime RED。
詳細/最終rcは [appmem_map_tdd.md](appmem_map_tdd.md) と票§3-5 f3記録。

## f4 の純粋な表操作 (2026-10-03)

票 [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §3-5 f4。
全体/先頭/末尾/中抜き/隣接異kind、32本FULL/31本成功、残片kind/flagsと
EXEC_LARGE識別、古いunmap proposal (属性/端) と同数古いmap proposal (挿入/左右併合) を検査。
LIBC_INITIALの返却後はimageのpage境界をlayoutに渡し、flags0で穴を再利用する。
ANONとLIBC_INITIALは併合しない。失敗は表/output不変、末尾の空slotは全欄0。
既存16変異に同数plan照合省略・unmap残片照合省略・残片flags取り違えの3本を追加。
期待FAIL完全行照合で19/19 runtime RED、194 CHECK GREEN。
初回はmerge条件とextent_equalの置換文字列が重複して2 ERROR。
共有走査を維持して置換を一意にし、テスト追加による先行FAILラベルは既存対照を先に実行して
解消した。ERRORはREDへ数えない。最終rc/時間は票に記録する。
