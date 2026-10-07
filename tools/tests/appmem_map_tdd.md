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

## f4 unmap と f3 独立レビュー持越し (2026-10-03)

PM 判断 `/home/hight/os32-tmp/ref_f4.md` §8 Q1〜Q9 / §9 に従う。
実 [appmem_unmap.c](../../exec/appmem_unmap.c) を既存 runner に加え、全 runner は11本のまま。
[appmem.c](../../exec/appmem.c) のprepareは連続被覆/kind/最大2残片/slotを先に検査。
LIBC_INITIALはextentとし、ANONとは併合しない。EXEC_*公開unmapは全拒否。
私有maskを受ける表の口だけを用意し、EXEC_LARGE識別照合を伴う内部入口はf9/f10。

- 全体/先頭/末尾/中抜き/隣接LIBC_INITIAL+ANON、PDE跨ぎ、空PT返却/非空PT保持、
  k=0保持、上半分のPT (k>=32)、2枚のPT返却をmaster/activeで確認 (16成功)。
  A/D付きPTEを受け入れ、返却後の穴へ実mapし直す。32本の中抜きFULLは全不変、
  31本の中抜きは成功。負例ではPD/PT/AS/extent/owner/池のsnapshotが全不変。
- 引数/帯外/image/stack/guard/shlib/lease/穴/EXEC_*、PTE=0/NP/USERやRW欠落/
  PS/PCD/PWT、後段ページの別owner、live SURFACEとclosing+lease、文脈違反を拒否。
  PTE/owner拒否は診断カウンタを増やす。検査後free失敗を1回注入し、負値・NP frame保持・
  extent不変・bad_free増加を確認、足場を復旧して終了時の正常bad_free=0を確認する。
- freeフックは全対象NP/未返却frame保持、active reload=1・master=0、IF=1、owner、
  data→entryゼロ→PT、PDE=0→PT返却、extent最後を実allocatorの前で検査する。
  CR3フックでは返すPTの中身が同期前には不変であることも確認。
  返却後のPDE/app_pt_phys双方0と `paging_app_context` 成功、非0 NP残留0を要求する。
- P3-1/P3-2: map_merge共有のplan再計算 (first/remove_count/merged) と同数古いplanの負例。
  mapはstage前/irq_save前の2回を確認、unmapも残片/件数/位置を再計算して照合。
- P3-3: 新PTのPRESENTは全確保成功後、IRQ保存前。既存PTだけIRQ区間で変更し、
  unmapは残るPTだけNP化、空PTはPDE撤去後に同期する。PTE操作hookでIRQ区間の
  ページ操作数が既存PT (unmapでは残るPT) の対象ページ数を越えないことを検査。
- P3-4: 2ページ目と2枚目PTの非0 PTE、範囲外のPRESENT保持、既存PTだけ/新PT1枚を追加。
  mapは6成功・全21確保失敗を注入 (直接stage/commit/abortの対照も別に実行)。
- P3-5: commit後abortを実行し、公開済PTE/ownerを保持する。
- P3-6: byte/他ASのPT照合は全内容を比較した上でpage単位のCHECKに集約し、
  CHECK数の増幅を抑えた。大きい範囲の全失敗注入は今回追加しない。
- P3-7: Make recipeのecho/MUT/末尾を周辺に統一、TARGET_SRCSをliteral一覧にして目録生成へ公開。

変異は原本を変更せず対象TUだけ差替え、期待FAIL完全行とrc=1を照合する。
既存11本 + f3持越し6本 + unmap15本 = 32本。必須14種類を全て含む。

| 変異 | 期待 FAIL |
|---|---|
| data-zero | data zero |
| free-before-publish | no free before publication |
| PT-zero | PT zero |
| early-PRESENT | staged PRESENT clear |
| rollback-PFN-leak | data before PT free |
| rollback-PT-leak | PFN rollback |
| PT-before-data | data before PT free |
| failure-publish | failure table unchanged |
| active-no-reload | publish reload count |
| alloc-IRQ-disabled | alloc IF enabled |
| nonzero-overwrite | nonzero PTE rejected |
| late-nonzero-only-first | late nonzero PTE rejected |
| abort-whole-existing-PT | existing PT rollback unchanged |
| postcommit-abort | no free before publication |
| IRQ-pending-pages | IRQ page work bounded |
| unmap-FULL-partial | unmap FULL no withdrawal |
| unmap-other-owner | unmap reject before writes |
| unmap-EXEC-public | unmap reject before writes |
| unmap-free-before-TLB | unmap TLB before free |
| unmap-no-reload | unmap TLB before free |
| unmap-early-zero | unmap owner argument |
| unmap-zero-omitted | unmap returned entry zero |
| unmap-PT-before-PDE | unmap free IF enabled |
| unmap-live-PT-free | unmap only empty PT |
| unmap-PT-metadata | unmap context restored |
| unmap-hole | unmap reject before writes |
| unmap-fragment-kind | unmap fragment identity |
| unmap-publish-first | unmap extent last |
| unmap-k0-free | unmap k0 retained |
| map-plan-before-stage | map plan validation twice |
| map-plan-before-IRQ | map plan validation twice |
| unmap-early-empty-NP | unmap empty PT untouched before TLB |

初回正常対照はGREEN。拡張途中の変異は置換位置の重複と先に検出されるFAILラベルの相違を
ERRORとして記録した (`f4-table-initial.log`, `f4-map-base-mut.log`, `f4-map-mut1.log`,
`f4-map-mut2.log` まで)。元の判定は緩めず、置換範囲を限定し、目的に合う負例/ラベルを
固定して修正後32/32 runtime RED・その他分類0。最終件数/時間/rcは票§3-5 f4へ集約。
kernel結線/AS埋込み/caller/KAPI/初期heap登録/実TLBのguest受入はf5以降。

### f5a 内部結線 (2026-10-07)

AS の extent/layout と初期 heap 登録、予約境界・固定帯 hint 拒否を実ソースで検証。
remove_count 単独・空 PT の SURFACE・PDE before PT free の変異を独立に観測する。
実 exec_teardown_app を抽出し、LIBC_INITIAL の穴と ANON の R5 回収、
PT free 失敗・重複 PFN の途中失敗から毒 AS の隔離/残ページ計数を検証。
ログと予算は `/home/hight/os32-tmp/run/f5/report.md`。実 TLB/kill は台帳 F-6。

### f5a fix1
毒 AS の appmem / free_user_range / destroy / lease_unmap 各入口から、対象 slot
だけに IRQ-safe な abort_req を立てる。実 exec の resume と syscall 帰路の末尾・
ring3_abort_check を実行し、非復帰 kill 境界への移譲を確認する (実 kill は F-6)。
WM の abort_clear でも保持、live の計数は一度だけ減り boot context に戻る。
追加変異は ANON 返却欠落、teardown 入口毒判定欠落、毒後 break 欠落、中断要求欠落、
live 補正欠落、resume/syscall 中断点欠落、WM による中断要求消去の 8 本。

### f9 USER exec heap (2026-10-08)

[exec_heap_host.h](exec_heap_host.h) は同じ ILP32 fixture に実 exec_heap/kmalloc、
caller_access、access_walk と生成 KAPI wrapper を追加する。Linux memfd の共有
alias で同一 VA / 別 PFN の親子を再現し、owner 台帳と PTE は実物を使う。
USER/WM/TRUSTED/frame 無効/終了通知の窓、R1、親子と共通 restore helper、
EXACT/TOPDOWN/extent 詰め直し・併合、公開拒否・内部全返却・ARENA teardown を検査。
size 巨大/wrap/縮小/非整列、magic、payload/ANON の偽 header、別 arena へ届く size、
double free、PTE/owner 不整合では alloc NULL・free byte 不変を要求する。resident の全 arena と
KHeap、別 AS の extent/全 arena/PTE、各 owner のページ数を前後照合する。
新規11変異は検証迂回・kind・終端走査・PTE/owner・跨ぎ free・R1・CR3 振分け・
CPL3 init/restore・ARENA teardown・公開 ARENA 許可を個別に崩す。
fix1: INITIAL 256KiB の 65536/131072 全バイト保持、伸長拒否/overflow の状態不変、
所属/候補 arena の PTE walk 数と集計差分、早期64KiB拒否の独立変異を追加。
65535 byte は整列後64KiBでも小要求として伸長し、丸め後で伸長を拒否する変異も検出する。
証拠は `/home/hight/os32-tmp/run/f9/f9_fix1_report.md`。実ゲスト受入は台帳 F-10。
