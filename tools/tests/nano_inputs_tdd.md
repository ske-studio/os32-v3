# T2f f1a nano入力台帳のホスト記録

票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) §3-5 f1a。

実toolchainの固定台帳をGREEN対照に、アーカイブhash、member hash、symbol提供元、
専用名アーカイブの有無、CI receiptの由来/設定/builder、およびreceipt生成の
tarball/ソース/configure/CFLAGS/target/patchを変えた18ケースがGREEN。
有効なアーカイブの末尾へ1 byte追加するケースでもhash差を拒否した。
実toolchainの9 memberが `build-newlib-nano` のobjectとbyte一致、関連ソース12ファイルが
固定hashの公式tarballと一致。実config.log/Makefileを使うreceipt生成も、TMPDIR内の
隔離prefixでrc=0 (既存toolchainへの書込みなし)。

`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp PYTHONPATH= python3 -B tools/tests/test_nano_inputs.py --mutate`
はrc=0。入力拒否を無効化する9変異が実行時RED (source、build flag、CFLAGS、hash、
provider、archive-name、builder、upstream、configure)。各置換の当たり数は1に固定、
構文を事前compileし、期待する拒否が失われたAssertionErrorだけをREDに数える。
生き残り0 / ERROR0。コンパイル/外部コマンド失敗をREDへ読み替えない。

リンクprobeは実cross GCCでコンパイルし、実cross ldの `-r -lc` 出力とmapを調べる。
実行物を起動しないためILP32実行は無し。allocatorの動作/arena/trimはf1b以降の試験。
GitHub Actionsの新規toolchain構築は未実施。CIは固定upstream入力からreceiptを生成して
同じ検査を `make check-fast` で行う。詳細と限界は [08_build.md](../../docs/08_build.md) §8-5。


前回レビュー修正 (2026-10-02): 38ケースGREEN、20変異の実行時RED / 生き残り0 / ERROR0。
追加はGCC版/target、dlmalloc混入、先頭-Lの有効な偽libc.aによるlink map拒否、
upstreamソースhash、nano.LICENSE (tarball先頭27行との一致)、SDK patchの構築からの分離、
キャッシュキーのSDK/local/symbol変更での不変性と構築入力/builder変更での変化、CI呼出し確認。
configure競合/target、toolchain patch、tarball/source hash、license、link mapの各requireも
置換当たり1の実行時変異で閉じる。全12 reentrant入口とwrapper/状態/sbrkr/lockをmapで検査。
最初の変異実行は移行後の置換文字列が旧名だったため当たり数検査でrc=1、修正して再実行。
この準備エラーはREDに数えない。CI新規構築/receipt生成は初回build.yml runで受入。

再レビュー N1〜N5 (2026-10-02): 50ケースGREEN、25変異が実行時RED / 生き残り0 / ERROR0、rc=0。
N1は新試験を旧検査器へ適用し、名前追加時にキーが変わらずAssertionErrorでRED (rc=1) を確認。
修正後はmembers/archivesの名前追加でキーが変わり、hash値変更と列挙順変更では変わらない。
名前除外の2変異とhash値混入の変異で、この契約を固定した。
N2は旧echo行を失敗するpython3 stubとbash -eで実行し、rc=0でFAIL文言がkeyに入ることを確認。
修正後の実workflowのキー生成ブロックを同じstubで実行するとrc=1、key出力無し。
N3はbuilder不一致の復旧案内を試験し、案内削除の変異は実行時RED。
N4はprovider/dlmalloc/link map/GCCのinventory失敗へreceipt absentを付けないことを確認し、
inventoryをtry内に戻す変異は実行時RED。hash差だけには引き続き復旧案内が付く。
N5はlicenseがtoolchain節にありSDK節に無いことを確認。
前回の「local証拠変更でキー不変」の契約を、今回の名前/hash値の区別へ更新した。
置換当たり数は各1、構文・外部コマンド失敗をREDに数えない。
ログ: `/home/hight/os32-tmp/t2f1-rereview-nano.log`。

今回の `make all`、memmap生成、TESTS再生成、lintはrc=0。
make allの既存image.mkによるNP21/W宛自動コピーは失敗 (警告)、配備成功無し。
票を検査開始前に固定し、最終make check-changedのrcは完了報告と
`/home/hight/os32-tmp/t2f1-rereview-check-changed.log` に残す。
