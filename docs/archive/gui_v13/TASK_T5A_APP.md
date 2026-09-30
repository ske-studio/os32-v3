# T5a — 独立ゲスト表示試験アプリ実装

> 発行: PM (2026-09-09) / 状態: **受入完了 (2026-09-12)**

担当Codex、PM Hermes、独立レビューclaude-opus-5。
親: [TASK_T5_DISPLAY.md](TASK_T5_DISPLAY.md)、[REVIEW_T5_GATES.md](../gui_v13_reviews/REVIEW_T5_GATES.md)。

## 範囲

`userland/rust/t5a_display/` の新規staticlib。既存gui_demo/v12_api_testのinit/run/Window所有型に従う。
固定fixtureだけを表示する。CUI子実行、FD捕捉、FEP、gshell/共有ライブラリ/KAPI変更は禁止。
配備・エミュレータ操作は許可しない。実行可能なOS32Xのビルドとゲスト動作合格を区別する。

## 接続

依存はos32api、libos32gui_stub（libos32gui名）、既存libos32term、libos32term_renderのみ。
ユーザー空間C変換はlib/utf8_prog.oを最終リンクする。kernel版を使わずreadyを強制設定しない。
GlyphsのJIS変換は既存unicode_to_jis、ANK/漢字読出しは初期化済みKAPI。
SinkはPaint中のGUI hlineへ接続する。全座標・長さのGUI範囲検査を描画開始前に済ませる。
背景/前景色は既存GUI定数を使用。物理VRAMやgfx_initを直接扱わない。

## 固定表示と寿命

通常fixtureは40列×64保存行。名前付き定数と容量チェックを使う。
大配列をstackに生成しない。単一所有の静的領域を一度だけ貸すか、既存heapから確保する。
方式は周辺実装とAPIを読んで選びREADMEに根拠を書く。自己参照structや無根拠なlifetime延長は禁止。
状態はTerminal、top、消費バイト、停止理由、fixture識別を保持。
fixture切替時は旧Terminalを破棄して領域を再初期化し、prefix/pending/limit/topを残さない。
Full/TooWide/finishエラーで無限再試行せず、本文外に停止理由をASCIIでも明瞭に表示する。
consumedを表示済み文字数と呼ばない。pendingと未消費末尾を区別する。

通常窓はscreen_infoから画面内に収まる寸法を選び、表示行数はclient高さから計算する。
Paintごと保存状態から描き、保存行不足部分は背景。モデルを描画で変更しない。
同じアプリ内の第二窓で遮蔽/再露出試験を可能にする。新規文字入力カーソルは不要。
操作は既存on_key/ボタンからfixture切替・top移動・終了のみ。キーバインドをREADMEへ明記。

## fixtureと試験

- ASCII、日本語、全角英数、半角カナ、NUL/ESC、無効UTF8、改行/TAB/BS。
- 容量ちょうどと次入力Full、1列モデルTooWide、最終未完了UTF8のfinish失敗。
- topの先頭/途中/末尾/先頭、fixture切替で旧状態が残らない。

状態遷移・消費数・停止処理・viewport計算をhostで試験可能なモジュールへ分離する。
新挙動ごとに実装前assertion RED→最小実装→GREEN。ログはREWORKでなく最初からTDD_LOGへ残す。
初回GREENの追加回帰は明記し、新挙動のREDを後付けで代替しない。
ゲストFFI配線の未実行箇所をhost合格と主張しない。hostコマンドは既存APIとmanifestを見て選ぶ。

## 分担と受入

PMがworkspace/build/programs/deploy登録を直列に担当。worker編集は新規アプリ内のみ。
ビルド: `make t5a_display_rust`。ゲスト単体リンクとELF/BSS/stackへの配置確認を行う。
最終受入は独立レビュー、make check、承認後のPC98/PEGC/Cirrus画面・再露出・終了復元・配備同一性。
ホスト実装提出だけでT5a完了とはしない。共有編集、commit/push、外部依存追加、他エージェント起動は禁止。
