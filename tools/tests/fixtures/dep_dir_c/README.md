# DEP_DIR_C L14a の固定列

製品コード移動前の `6d2c91e9963d472e1fcc313b439ac3fac6f3367e` から採取。
設計 §7.1 の `e59aa4e931dc2f51e4fdf7a374b5965057bc6528` と、採取に使う
kernel / platform/pc98 / include / arch/x86 / lib / drivers / exec /
sdk/include/os32 の内容は同一であることを採取時に `git diff` で確認した。

入力は inputs.json の 24 ケース。manifest.json は基点、入力・shim・採取器・
ソースとヘッダの SHA-256、解凍後の期待列の SHA-256 を保存する。
通常実行は git 不要。期待列の採取・更新オプションは提供しない。

採取手順 (L14a の一度だけ実施):
1. 上記基点の対象ディレクトリを `git archive` で外部の base/ に展開する。
2. CROSS_DIR、ディスク上の TMPDIR、HOST32_RUNNERS=qemu を設定する。
3. inputs.json の各ケースについて、test_dep_dir_c_memory.py の
   `build(work, sample, source_root=base)` → `trace(exe)` → `validate(data)`
   → `comparator_selftest(data)` を check_slot.sh l14a 経由で実行する。
4. 得た列を `gzip.compress(data, mtime=0)` で保存し、ハッシュを記録する。
   採取スクリプトは既存の期待列への上書きを拒否する。
5. 通常の verifier で作業木と固定列を比較する。変更後の製品から再採取しない。

各行は tag と 32bit hex 4 列。r/w は連番・物理番地・値、b は BDA offset・値。
BDA の OR 両辺の評価順だけを正規化し、volatile probe の列は並べ替えない。
l/L は低位全語の前後、H は高位全語の残存値、R/E は検出値と attestation。
A/W と a/z は bootstrap の配置と DMA 予約前後の全物理モデル、Z/O は戻り値。
m/n は最終モデル、g/o は登録順の台帳、I/M/G/c は容量・配置・集計。
S はリンク記号由来の配置条件であり、数値の機種区間とは別レコード。
P は初期化前後のフェーズ、D は全 1024 PDE と PT 有無、T は全 PT の連続列。
T の列は開始・排他終端・先頭 PTE・増分。各 PTE は先頭 + index × 増分
(mod 2^32) で復元できる。validate は欠落・重複・穴を拒否する。
比較器は全列を比較し、期待 PTE 1 bit 反転の拒否も毎回確認する。

L14a は初期化時の地図を固定する段。L15 の動的 cache/V86 操作、
製品の移動、既存 runner の追従、変異試験、容量・実機受入は後続段が担当する。
