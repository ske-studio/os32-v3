# KAPI の範囲検査 — 実ターゲットのホスト試験

対象票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) T2e
「KAPI の範囲検査の欠落の修正」。2026-10-03、GPT-6。

## RED → GREEN

- 修正前の実 console.c に対し `test_kapi_bounds.py --runner qemu` は
  `FAIL putchar bounds`、rc=1。模型の座標計算を試験するものではない。
- 修正後は実 console.c / fm.c / kapi_db.c / gfx_core.c / gfx_vram.c を ILP32
  GNU11 -O2 で組み、MMIO と port を足場へ置換。未使用関数は gc-sections で除去する。
- TVRAM は両面と両面間の未使用領域を含む配列、その外の番兵、アクセス回数を確認。
  正常対照は先頭・最終セル・最終行の漢字2セル。負値、COLS、ROWS、30行、
  INT_MIN/MAX、x=0x40000000、y=0x08000000 (u32 offset が周回)、漢字の右端。
  不正な read は code/attr 不変で、MMIO を読まないことも確認する。
- FM の音階・音色の負値、DB の改竄 column_count / data_offset、16/256色の
  palette first/count、dirty の巨大矩形、raster count を実関数で確認。
  DB は SQLite の実行ではなく、実 slot_get と実 column_text の検査を通す。
- `test_gfx_bounds.py` は既存の `test_gfx_kernel_fb.py` の実3バックエンド・台帳・
  paging 足場を再利用。PC98/PEGC/Cirrus の通常/200行初期化を通し、矩形が
  領域外なら転送なし、巨大幅高さは最終画素へクリップ、PEGC の面後方16Bの番兵不変、
  Cirrus のエンジン引数が画面内、scroll INT_MIN/MAX の正規化を確認する。
  実機 I/O、NP21/W、NHD、配備は行わない。

## 変異

全て一時ディレクトリの写しだけを変更。本物の木は変更しない。

| suite | 変異 | 判定 |
|---|---|---|
| kapi bounds | putchar/readchar/putkanji の各検査削除 (3)、FM 音色/音階 (2)、DB 列/offset (2)、palette16/256 (2)、dirty clip (1)、raster count (1) | 11/11、固有 FAIL と rc=1 |
| gfx bounds | PEGC/Cirrus を旧加算 clip へ戻す、scroll の剰余削除 | 3/3。PEGC は実範囲外アクセスの SIGSEGV、Cirrus/scroll は固有 FAIL |

コンパイルエラー・timeout は RED に数えない。PEGC の SIGSEGV は既知の1変異だけで
許容し、他の変異は期待する FAIL で照合する。正常対照は signal が1件でも失敗。
正常系条件数・実行コマンド・予算・全体検査の確定結果は対象票に記録する。

足場作成時に GFX の signed/unsigned 比較を -Werror が拒否したため、既存の u32
寸法定数との比較に明示 cast を追加した。palette16 変異は first ではなく count の
条件で落ちたため、期待メッセージを実反例に合わせた。いずれも修正して再実行済み。

集計出力を追加した際、足場の Linux write syscall の eax clobber 指定が不足して
rc=1となった。入力専用からread/write operandへ修正後、84条件と11変異が再通過。
文書リンクの見出しアンカーも初回rc=1から修正。全体検査の最初の投入はslot取得前に
中断 (rc=130、検査自体は未開始) してから文書を直した。
