# T2f f3 map transaction のホスト試験記録

票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §3-5 f3。
PM 判断 `/home/hight/os32-tmp/ref_f3.md` §8 Q1〜Q10 を適用する未結線の先行準備。
実 [appmem.c](../../exec/appmem.c)、[appmem_map.c](../../exec/appmem_map.c)、
[paging_app.c](../../kernel/paging_app.c)、paging/pgalloc/physmem を ILP32 で連結する。
MMU・IRQ・Linux write/exit/mmap と確保失敗/物理ページの汚れ値だけを足場にする。
判定・allocator は実物を使い、kernel.mk には追加しない。

## 正常対照と負例

- PDE 境界をまたぐ3 data page。新規PT2枚、既存PT1枚+新規PT1枚をそれぞれ
  master/active ASで実行する。全確保位置1〜5 / 1〜4を注入、合計18失敗、4成功。
- 確保は各1枚、IF=1。新しいPT/dataを0xa5で汚し、次の確保と公開直前にゼロ化を検証する。
  dataのNP PTEはframe|RW|USER。既存PT・pending PTの両方を観測する。
- 失敗後は対象AS/extent/PD/既存PT、masterと別ASの全PD/PT、
  owner表/pages、pool bitmap/owner map/free数、出力、IF/CR3を照合する。
  freeフックはNP PFNの返却→entryゼロ→PT返却を検査し、診断bad_free=0を要求する。
- 成功後はPTE/PDEのUSER/RW、app_pt_phys、data全バイトゼロとowner、extentを検査する。
  IRQ保存の直前はPDE/AS/extent未変更、PRESENTなし。active CR3フックでは
  IF=0かつPTE/PDE/app_pt_phys/extent確定済み、再ロード1回、inactiveは0回。
  正常/失敗の両方でAS全app PTの非0 NP entryが0個。終端で全owner pagesを返す。
- IRQ/例外深さ、別AS CR3、IF=0、PD/PT所有違反、PDE不整合を拒否する。
  extentが空でも既存PTEがPRESENT/NP/frame無しの非0なら全拒否。
  EINVAL/ENOVA/EFULLは確保前、PT/data不足は共通ENOSPC=-4で内部診断を分ける。

## 変異 RED と原本 GREEN

`python3 -B tools/tests/test_appmem_map.py --runner qemu --mutate`。
共通paging/pgalloc/physmem/fixture objectと正常製品objectを再利用し、変異TUだけを
再コンパイル/再リンク。写しへの当たり数1、期待FAILの完全な行、rc=1を照合する。
原本入力hashを開始/終了で確認。`mutpar.run_ordered` の固定並列度を使う。
compile/link error・signal・timeout・別ラベル・生存はruntime REDに含めない。

| 変異 | 期待 FAIL |
|---|---|
| dataゼロ化省略 | data zero |
| 公開前free | no free before publication |
| PTゼロ化省略 | PT zero |
| 準備中PRESENT | staged PRESENT clear |
| rollbackでPFN返却省略 | data before PT free |
| rollbackでPT返却省略 | PFN rollback |
| PTをdataより先に返却 | data before PT free |
| 失敗時publish | failure table unchanged |
| active CR3再ロード省略 | publish reload count |
| 確保中IRQ禁止 | alloc IF enabled |
| 既存非0 PTE上書き | nonzero PTE rejected |

初回試験は入力一覧に存在しないphysaddr.hを記載してrc=1。
次はcleanupでfree_user_rangeのend引数をbytesと取り違えてowner残留を検出 (rc=1)、足場を修正。
初回変異は9 RED、PFN漏れの期待ラベル違い1、置換の警告によるcompile ERROR1 (rc=1)。
期待を実際の返却順違反ラベルへ合わせ、警告を出さない置換に修正した。
これらをREDには算入しない。修正後11/11 runtime RED・その他分類0を確認した。
最終のCHECK数・時間・コマンドrcは票§3-5のf3実装記録へ集約する。

## f2持ち越しと接続境界

P3-3: publish入口のcount/range_free照合、同数で範囲が占有済みのproposalと
countが変わったproposalの拒否対照を追加。
P3-6: 未知flag/非整列hint/hint下限を緩める3変異を追加し、f2は139 CHECK・16変異。
[appmem_tdd.md](appmem_tdd.md) の既存136 CHECK/13変異の記録は当時の履歴として保持する。

pending[64]は256 B、txはtargetで268 B、planは36 B、AS/AppSlotは増加0。
stage失敗時に割当は残らず、成功stageだけをabort/commitへ渡す。
prepare〜stage〜publishは同じ表の直列・非再入で、callback/AS切替を挟まない。
IRQ保存はcommit→extent publish→active CR3まで、全体stack high-waterはhで測る。
f4 unmapは同じpaging_appへ、AS埋込み・caller/KAPI・heap端とkernel結線はf5以降。
native・独立レビューはPM、ゲスト受入は今回対象外。
