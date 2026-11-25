# P1-1 配置案（A1 確認用）

P1 の A1 で判断する対象は[配置台帳](data/layout.tsv)の各ファイル。P3 の確定移動は 1,791 本。P8 の 54 本も兄弟と同じ行き先に丸ごと移し、中身の分割理由は根拠欄に残す。保留は不要の候補 7 本。

同じディレクトリの兄弟は据え置きも含めて行き先を照合する。所属の違いで分ける 24 ディレクトリは[行き先と理由の一覧](data/layout_splits.tsv)に載せ、p1_verify.py で実際の分岐先と照合する。gfx/v2/ は内部ヘッダを含め全て sys/hal/gfx/v2/ に置く。

| 判断 | 推奨と根拠 |
|---|---|
| D-1 | kernel の共通処理を sys/kern/ に置き、mm・exec・init の下位を保つ。初期化と実行形式の責任を引き続きディレクトリで判別できる。 |
| D-2 | GeForce の三系統を sys/dev/video/geforce/ にまとめる。細分と同名ファイルの置き分けは P1-3 の行き先を採用し、依存を分ける行は P8 に送る。 |
| D-3 | build/kdebug.mk の KDEBUG_OBJS に入る kselftest とその入力を製品として残す。共通の診断は sys/kern/kdebug/、CR3・PDE/PTE・i386 命令に依存する診断とスタブは sys/arch/x86/kdebug/ に置く。PC-98 の BIOS・PEGC・V86 に依存する診断は sys/platform/pc98/kdebug/、Trident・8237・シリアルドライバに直接依存する診断は各装置の下に置く。HAL 表にある行はその判断を採る。 |
| D-4 | P1-3 の分野別契約・共通処理と追加契約ヘッダ3本を sys/hal/ に置く。sys/gfx/・sys/sound/ は将来の置き場。[HAL の配置案](../../design/build/hal_layout.md) |
| D-5 | kstring 系と kprintf 系を sys/kern/ に置く。前者のゲスト比較ビルドも同じソースを参照する。ライブラリ独自の共有処理や第三者の配布単位は lib/ に保つ。 |
| D-6 | build/images/ と build/features/ の選択宣言を sys/conf/images/ と sys/conf/features/ に置く。コンパイルとリンクの規則は build/ が持つ。 |
| D-7 | sample/ の配布見本と game・apps の gitlink を現位置に保つ。packages/ と images/ に該当する基点の追跡パスはない。 |
| D-8 | ゲスト診断を userland/diag/ にまとめる。build/programs.mk の ELF・BIN の規則、userland/deploy.yaml の test 区分、その入力・実機台本を同じ単位で追う。同じ配備物を生成する Rust クレートは現在の Cargo 入力位置に保つ。PPC_USER_APPS が組む atadeny.bin と learnprb.bin のソースも userland/diag/ に置く。ホストの QA は P1-4 が持つ。 |
| D-9 | lib/os32_opna/upstream・lib/softfloat・userland/lib/font/vendor は各消費側の近くに保つ。上流からの更新単位と Cargo の相対依存を維持する。 |
| D-10 | sdk/link、mkos32x.py、Rust のターゲット定義を SDK の配布単位に保つ。新しいアプリがリンク・包装・クロスビルドするための入力として扱う。 |
| D-11 | P0-1 の候補9本のうち、kernel/v86/ の互換転送ヘッダ5本と sys/include/vk32.h を別票の調査対象に残す。6 本とも build/kdebug.mk の HWDEV_HEADERS（kernel/v86/*.h と include/*.h）が拾い、HWDEV_GENERATED の前提になる。削除判断ではこの生成依存も調べる。個別のパスは台帳の「D-11 残骸の候補」で引ける。 |
| D-12 | A3 で P1-4 が扱う。 |

sys/include/diag_stack.h は SQLite のトレース構成、sys/include/snd_writer_probe.h は音声診断、sys/include/type_contract.h は毎ビルドの abi-contract-probe が参照する。P0 の像の欄が空でも、これらの利用者がいる。tools/abi/type_contract.c は build/abi_contract.mk が読むので P1-1 に残す。

仕様 §3.2 の直下名は kern、hal、arch、platform、dev、gfx、sound、input、fs、net、kapi、include、conf。sys/gfx/ と sys/sound/ は将来の置き場。今回の確定移動は両方とも 0 本で、P3 では作らない。

## 運用の調査と外部利用者

[運用と外部利用の一覧](data/layout_review.tsv)には tool_callers.py の静的な呼出元と判断を載せる。運用170本には自動読込の設定やビルドの入力も含まれる。呼出元が0件の行は、設定・Make の変数・手動の手順・外部からの呼出しを確認した。静的な参照は最終実行日時の記録ではない。不要の候補の行き先は保留。削除は別票で判断する。

外部が使う運用の入口は現行パスを保つ。外部が使う旧受入の本体・支援は行ごとに終了先を示した。PM が P3 の前に相手と最後に使える版を共有し、P7 の前に呼出し停止を確認する。今回の通知は未実施。

## P2-3 / P3 で直す所

| 段 | 規則 | 修正と確認 |
|---|---|---|
| P2-3 | build/sdk.mk の CHECK_REGISTRATION | Make の解析時に旧 tools/checks/run.py --make が走る。製品ビルドをこの登録から切り離し、旧ランナーを外した状態で Make が解析できることを確かめる。 |
| P3 | build/programs.mk の C_TESTS | wildcard userland/tests/*.c と filter-out、個別規則を userland/diag/ に合わせ、移動前後の ELF・BIN の集合を照合する。 |
| P3 | build/ppc-user.mk の atadeny.o・learnprb.o | 明示したソースを userland/diag/ に変更し、両方の BIN が生成されることを確かめる。 |

P3 では build/kdebug.mk の生成先・オブジェクト規則・ヘッダ探索と kselftest.c の相対 include を台帳に合わせる。KDEBUG_GENERATED のうち i386 スタブと PC-98 の輸入表は出力先が分かれる。

## 再生成と照合

P0-1 の owner-empty.tsv と P1-0 の所属種類をパスで突き合わせ、該当する配置台帳の根拠に持ち主を記す。台帳を作る入口は次のとおり。

```sh
python3 -B tools/restructure/layout.py --caller-log "$TMPDIR/callers.tsv" --owner-empty "$P0_DIR/p0-1/owner-empty.tsv"
python3 -B tools/restructure/p1_verify.py
```

確定した移動だけを [SRC_MOVE](../../SRC_MOVE.md) の JSON に渡す。保留一覧は p1_verify.py が理由つきで表示する。
