# T2e e10a SHM/boot USER 試験記録

正典: [TASK_T2D_T2H.md e10a](../../docs/tasks/v3/TASK_T2D_T2H.md)。

## 対象と境界

`test_shm_user.py` / `shm_user_host.c` は実物の paging.c、pgalloc.c、shm.c、
v86_mem.c を ILP32 で実行する。実 paging_addrspace_create で二つの PD を作る。
特権 CR3/TLB は host_arch、V86 BIOS/I/O は stub。Linux では低位 alias を
参照できないため、V86 setup のゼロクリア先だけ backing のホストポインタへ
差し替える。PDE/PTE の写像・復元処理は実物。CPL3 実機の書込みではなく、
実 as_va_to_pa の権限検査と、その物理先への書込みで二本目の AS を検証する。

## RED → GREEN

- 実装前の結線検査: rc=1、`FAIL boot before AS`。
- 初回ホスト実行: V86 の低位 alias 参照で signal11。上記の backing 代替後 GREEN。
- 正常対照: 47 CHECK、結線4条件 (boot順序/一回、起動時SHM/trampoline撤去)。
- lock/free/free_owned/cleanup_all の USER/RW/WB、二本目ASの書込み、
  USER欠落時の無変更拒否/カウンタ/SHM状態保持、範囲/整列/PFN/present/共有PT、
  live AS中の汎用3口拒否、boot二回目/AS中拒否、V86専用USERと終了後PDE0、
  active CR3のTLB同期、V86後の新ASのSHM/trampolineを確認。
- 変異11/11: runtime9 (lock/free/owned/cleanup USER脱落、汎用USER許可、
  boot再実行、SHM範囲削除、USER欠落見逃し、V86 PDE0復元欠落)、
  結線2 (ASの後へboot、毎起動SHM map再挿入)。期待FAIL文言とrcを照合。
  一時コピーだけに変異を適用し、コンパイル失敗やsignalをREDに数えない。
- boot の live 判定だけを外す試作変異は汎用口のガードで拒否されたため、
  同値変異として件数に含めない。AS中の拒否自体は正常対照と結線変異で検証する。

## 回帰と実行環境

`CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、
`PYTHONPATH=`、`HOST32_RUNNERS=qemu`。makeのstdinは`/dev/null`、
`NP21W_DIR=/dev/null`。全体検査は`check_slot.sh e10a-coder`経由。
既存memmapの13変異の意図と、予算超過/空予約/固定PT/IFの対照は維持する。
`test_memory_boot.py` は19件。`test_owner_reclaim.py` のpaging stubを専用口へ追随。
実行ログは `/home/hight/os32-tmp/e10a-*.log`、全体結果は正典のe10a節へ追記する。
ゲスト、native runner、配備、ini、NHD、実機、commit/push は未実施。

初回全体検査rc=2: surface bundleのexecコード抽出が撤去したSHMコメントに依存。
検査終了後、抽出終端だけ次のフォントコメントへ追随し、既存UC変異は維持した。

再検査: check_slot.sh e10a-coder経由、slot0、full選択、rc=0。
全体内でもSHM正常47条件と11/11変異、surface bundle 21/21変異が通過。
検査中の票/ソース変更なし。最終結果の追記のみ検査終了後に行った。
