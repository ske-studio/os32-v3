# T7a-2c 表示復元の受入

対象は [T7 §3 の T7a-2c](../../docs/tasks/v3/TASK_T7_AND_FOLLOWUPS.md)。配備・NP21/W 操作は PM が行う。`make kernel` は kcg.mod を含む PAIR 全体を生成し、`hsync boot` は /boot の一式を更新する。新旧の kernel / kcg.mod / kdebug.bin を混ぜない。

- KDEBUG=1 の通常起動で `[display]` 2 行を保存する。post_unicode の固定診断は ANK 'A' を cache に入れ、A1=0/A3=41h/A5=0 の A9h を CODE・68h=0Bh・復帰後に読む。A9h は値の記録だけで合否値を仮定しない。復帰は実物の `v86_io_reset_policy` → core CODE / 搭載時失効 → v86 body の CUI 復帰を通す。`kcg=1` は miss が1増え、CG=WB・B/R/G/I=UC と前後属性一致が自己試験を通ること。KCG 未搭載でも CODE 復帰を実行し、miss は増えない。
- 通常 / STOP 後 / `-g -t` の各 V86 終了前後を `pt0_snapshot.py` で採り、`--compare --ignore-ad` で比較する。CG/VRAM を含む低位 PTE と復帰監査を既存の観測口で確認する。KDEBUG=0 でも通常 V86 の復帰と CUI 編集ができること。
- 9801 相当は PM 管理の機種構成で `gui_gate` を使い、V86 後に GUI へ入り日本語再描画→CUI へ戻る。GFX=pc98 の指定だけを機種9801の代用にはしない。9821 は標準/拡張の切替、R7FIX2 の FFF00000h 窓を保持した再描画、CUI 復帰を観測する。
- 実機 T4T7-RH へ: Ra266 の同じ `[display]` A9 値・属性、V86/GUI/CUI 往復の再描画、GRCG/EGC 残状態の解消。実機 A9 値と再描画は NP21/W の結果で閉じない。

[HW3] 根拠対 (実装前固定): UNDOCUMENTED 9801/9821 Vol.2 の `io_disp.md` 207–218 (68h)、253–328 (6Ah、EGC→GRCG 前の 04A0h=FFF0h)、1068–1081 (A9h)、`memsys.md` 4100–4137 (054Dh bit6)。Bible §2-6-5 `2-6_テキスト.md` 349–371 (CGアクセスと窓)。ローカル鏡は main の `docs/hw/`、矛盾時は UNDOCUMENTED を採る。

既存実装: [FreeBSD releng/10.4 sys/pc98/cbus/gdc.c](https://github.com/freebsd/freebsd-src/blob/releng/10.4/sys/pc98/cbus/gdc.c) 1–28 (BSD license)、601 (PEGC識別)、1095–1107 (6Ah 標準/拡張)、1356–1364 (GRCG停止)。CGのCODE復帰は資料から導出し、同実装にCODE復帰列があるとは主張しない。NP21/W `src/io/gdc.c` 481–510、`src/io/cgrom.c` 186–218 は挙動確認のみで、構造を流用しない。CG WB は既存ページ台帳の契約を保持し、資料からキャッシュ可否の新結論を導かない。
