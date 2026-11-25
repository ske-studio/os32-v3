# P1-1 配置案（A1 確認用）

> 上位: [P1](P1.md)。判断の対象は[配置台帳](data/layout.tsv)の各ファイル。

| 判断 | 推奨と根拠 |
|---|---|
| D-1 | kernel の共通処理を sys/kern/ に置き、mm・exec・init の下位を保つ。初期化と実行形式の責任を引き続きディレクトリで判別できる。 |
| D-2 | GeForce の三系統を sys/dev/video/geforce/ にまとめる。細分と同名ファイルの置き分けは P1-3 の行き先を採用し、依存を分ける行は P8 に送る。 |
| D-3 | build/kdebug.mk の KDEBUG_OBJS に入る kselftest とその入力を製品として残す。診断モジュールを sys/kern/kdebug/ に置き、機種や装置への帰属を調べた行は HAL 表を優先する。 |
| D-4 | P1-3 の分野別契約・共通処理と追加契約ヘッダ3本を sys/hal/ に置く。[HAL の配置案](../../design/build/hal_layout.md) |
| D-5 | kstring 系と kprintf 系を sys/kern/ に置く。前者のゲスト比較ビルドも同じソースを参照する。ライブラリ独自の共有処理や第三者の配布単位は lib/ に保つ。 |
| D-6 | build/images/ と build/features/ の選択宣言を sys/conf/images/ と sys/conf/features/ に置く。コンパイルとリンクの規則は build/ が持つ。 |
| D-7 | sample/ の配布見本と game・apps の gitlink を現位置に保つ。packages/ と images/ に該当する基点の追跡パスはない。 |
| D-8 | ゲスト診断を userland/diag/ にまとめる。build/programs.mk の ELF・BIN の規則、userland/deploy.yaml の test 区分、その入力・実機台本を同じ単位で追う。同じ配備物を生成する Rust クレートは現在の Cargo 入力位置に保つ。ホストの QA は P1-4 が持つ。 |
| D-9 | lib/os32_opna/upstream・lib/softfloat・userland/lib/font/vendor は各消費側の近くに保つ。上流からの更新単位と Cargo の相対依存を維持する。 |
| D-10 | sdk/link、mkos32x.py、Rust のターゲット定義を SDK の配布単位に保つ。新しいアプリがリンク・包装・クロスビルドするための入力として扱う。 |
| D-11 | P0-1 の候補9本のうち、kernel/v86/ の互換転送ヘッダ5本と include/vk32.h を別票の調査対象に残す。個別のパスは台帳の「D-11 残骸の候補」で引ける。 |
| D-12 | A3 で P1-4 が扱う。 |

include/diag_stack.h は SQLite のトレース構成、include/snd_writer_probe.h は音声診断、include/type_contract.h は毎ビルドの abi-contract-probe が参照する。P0 の像の欄が空でも、これらの利用者がいる。tools/abi/type_contract.c は build/abi_contract.mk が読むので P1-1 に残す。

仕様 §3.2 の直下名は kern、hal、arch、platform、dev、gfx、sound、input、fs、net、kapi、include、conf。今回 gfx と sound の移動確定行はすべて HAL または実装の所有者に帰属し、一部は P8 に留める。

## 運用の調査と外部利用者

[運用と外部利用の一覧](data/layout_review.tsv)には tool_callers.py の静的な呼出元と判断を載せる。運用170本には自動読込の設定やビルドの入力も含まれる。呼出元が0件の行は、設定・Make の変数・手動の手順・外部からの呼出しを確認した。静的な参照は最終実行日時の記録ではない。不要の候補の行き先は保留。削除は別票で判断する。

外部が使う運用の入口は現行パスを保つ。外部が使う旧受入の本体・支援は行ごとに終了先を示した。PM が P3 の前に相手と最後に使える版を共有し、P7 の前に呼出し停止を確認する。今回の通知は未実施。

## 再生成と照合

P0-1 の owner-empty.tsv と P1-0 の所属種類をパスで突き合わせ、該当する配置台帳の根拠に持ち主を記す。台帳を作る入口は次のとおり。

```sh
python3 -B tools/restructure/layout.py --caller-log "$TMPDIR/callers.tsv" --owner-empty "$P0_DIR/p0-1/owner-empty.tsv"
python3 -B tools/restructure/p1_verify.py
```

確定した移動だけを [SRC_MOVE](../../SRC_MOVE.md) の JSON に渡す。保留一覧は p1_verify.py が理由つきで表示する。
