# T2e e7 帰路と描画の門

状態: **実装済み** (2026-10-03、ホスト検査。ゲスト未実施)。
正典: [TASK_T2D_T2H §2](../../docs/tasks/v3/TASK_T2D_T2H.md)、e7 実装結果。
PM の ref_e7 §8・§9 に従う。本番の port は NULL、protocol は変更しない。

## C の2実体

`test_gfx_reattach.py` は e6 の足場だけを再利用し、SDK の core と描画実装を
2組リンクする。1組目は e6 と同じ sdk_ 描画記号 (状態記号は原名)、2組目は
shl_ 記号。query/lease/gfx_core/3 backend/ledger/AS は実物、MMU・I/O だけを代用。
ホストの lease VA はホスト RAM を別途確保するため、2 alias の画素共有を検証する
試験ではない。PTE/世代/token会計と各描画入口の書込みを検証する。

正常167条件: planar/PEGC/Cirrus、2本再利用、再init/200行後の両世代・geometry、
旧pointer不使用、双方描画、8本満杯時の片側FULL、各側だけのquery失敗、描画停止。
shlib成功/static失敗で残るshlib tokenは§9の既知の差。
各実体の自己解放・アプリのstatic解放と両描画停止を確認し、終了時に残りも返す。

4変異は正常対照の後、私有コピーで期待する `FAIL` の式まで照合する:

- shlib帰路check除去 → `both_new_generation`
- static帰路check除去 → `both_new_generation`
- 片側失敗時static detach除去 → `static_token_returned`
- 旧bb pointer保持 → `old_bb_not_reused`

## Rust と gshell

`test_gui_reattach.py` は os32api_host のmock rlibに、実 client/draw/clip/gstate/
surface/ffi を取り込む。shlib_init とgdi_testの両実体判定関数も実ソースから読む。
C attachだけを代用し、wait後の照合・失敗の伝搬/自己detach・再回復、bind継続、
旧SurfaceIdのgeometry更新、PACKED8への書込み禁止、static present/shutdown/checkを
2試験で確認する。gdiの依存呼出しだけを代用し、片側失敗で描画に進まないことを確認。
既存entryからのfullscreen再attachはstubの既存SHLIB_INIT/SCREEN_INFOを利用する。

7変異: surface_size/基底クリップの寸法更新除去、wait check除去、Painter門除去、screen_valid失効除去、gdi片側check除去、
gdi static detach除去。各々runtime失敗のメッセージを固定する。

gshellのintegrationへ2試験追加 (計157)。restoreとCUI失敗戻しでscreen_infoと
自分のfbを取り直すことを確認。新規3変異は再読込み2経路とrestoreのfb取得を除去し、
固有のassert文言を照合。既存53変異の意図は維持 (計56)。

## 実行記録

- `TMPDIR=/home/hight/os32-tmp PYTHONPATH= python3 -B tools/tests/test_gfx_reattach.py --runner qemu --mutate`: rc=0、167条件・4/4 RED。
- 同環境 `python3 -B tools/tests/test_gui_reattach.py --mutate`: rc=0、2試験・7/7 RED。
- `make check-gshell-host MUT= NP21W_DIR=/dev/null < /dev/null`: rc=0、157試験。
- 最初の足場作成では未定義のputs、符号比較、API mock不足、Debug未実装型のassert、
  モジュールimport不足を修正した。これらコンパイル失敗やSIGSEGVは変異REDに数えない。
- 読み直しで、帰路直後のsurface_sizeと基底クリップが旧寸法を使う反例を
  リポジトリ外で再現 (rc=101)。両入口のrefreshと2変異を追加してGREENにした。
- 全体検査の結果・成果物サイズは票に集約。native/ゲスト/実機は未実施。
