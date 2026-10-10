# T2h — 外部 apps / game の再開時ゲート

apps / game はユーザー決定 (2026-09-30) により v3 のビルド対象外。
**H-7 は決定による持越しであり、v3 の完了条件ではない。** この一覧の引渡しを
T2h で確かめる。外部集合の移行・動作を PASS とした記録ではない。
担当は再開を決めるユーザーと、再構築・監査・guest 受入を行う PM。
持越しの正典は [DEFERRED_TESTS](../DEFERRED_TESTS.md) H-7、契約は
[TASK_T2D_T2H](TASK_T2D_T2H.md) §5-1 / §5-2。

## 再開前に揃える 8 項目

| # | caller の追随・再開条件 | 確かめる証拠 |
|---|---|---|
| 1 | 面は query / lease / bundle から取得する。旧 token の VA を保存して再利用しない。RO 面へ書かず、部分取得失敗では全返却する | caller の監査、取得失敗・古い token の拒否、終了後 lease / owner の回収 |
| 2 | TVRAM / GVRAM / font / Unicode の低位直読を撤去する。描画・文字・フォントは授権された入口を使い、Unicode は CRT / shlib の RO lease 経由にする | 低位ポインタの検索・caller ごとの監査、日本語表示、未取得時の代替表示 |
| 3 | 高位アドレスを符号付き整数で比較・加算しない。範囲・overflow・整列を検査する | 高位ポインタの境界負例と caller 監査 |
| 4 | OS32X ヘッダ v4 の 4 世代 (`os32x_format` / `kapi_abi` / `memory_layout` / `shlib_protocol`) を現行 SDK と一致させる。memory_layout 3 に追随する前の旧成果物は世代不一致で拒否される | 正典 `sdk/kapi.json` とヘッダ刻印、旧形式・世代不一致の入口前拒否 |
| 5 | 私有 heap / map / unmap と C / Rust allocator、整列、可変 stack に追随する。SDK CRT の `libos32nano.a` は `-lc` より前にリンクする | 65535 / 65536 / 65537B と整列の対照、枯渇時失敗、解放後 owner 回収 |
| 6 | backend / 面世代変更時に再 attach する。park / resume と trim 通知・TRIM_DONE に追随し、失敗後に古い面を描かない | 200 ライン化・backend 切替・park / resume・trim の guest 往復 |
| 7 | 外部 Makefile を `link_guard.py` 経由のリンク・入力刻印へ移し、旧 `.o` / archive を残さず staged SDK だけで再構築する。配備物を自層 `deploy.yaml` に登録する [V2] | 新 SDK + 旧 `.o` のリンク拒否、正常再構築、build ID / hash / 世代 / リンク入力と配備集合の記録 |
| 8 | 再開時の一式を配備し [D1][D2][V1]、対象構成で実 guest 受入を行う。外部未ビルドを外部 PASS に置き換えない | 起動・描画・入力・STOP / fault・終了・次起動、lease / owner 回収、反映した版・hash の確認 |

L-h で **KAPI 74 / MemStat 200B** となる SDK で `make clean-external` →
`make external` を実施する (再開を決めた後)。MemStat の旧 132B prefix は維持されるが、
外部の手書き構造体・Rust binding と size 引数を監査し、200B 欄を使う caller は再構築する。
4 世代の正典・リンク規則は [KAPI_SPEC](../../KAPI_SPEC.md) §3-1 と
[SDK README](../../../sdk/README.md)。現行で `make external` がリンク刻印未追随のため
失敗すること、c2 より古い libos32gfx の caller が面変更に追随しないことは H-7 の既知事項。

## T2h の配備集合と配備元の照合

`make all` の最後に世代表の後で `tools/gen_deploy_set.py` が
`build/out/deploy-set.json` を生成する。`format=1` の表は L-h の
`generations-manifest.json` の `build_id` を `generation_build_id` に保持し、
`kernel_commit` / `kapi_version` / `generations` も保持する。`files` の各行は
プロジェクト相対 `host` → 絶対 `guest` / `size` / `sha256`。並びは guest・host 順、
時刻を含めない。同じ木なら同じ JSON となる。

集合は `deploy_manifests.py` の本体 merged 定義 (`build/core.yaml` と
`userland/deploy.yaml`、assets を含む) を glob / exclude / 宛先名の規則で展開したもの。
`vmkernel.lz4` / `unicode.bin` / `.shlib` と配備するデータも含む。
非配備成果物の `kernel.bin` / `sqlite.bin` / SDK、install が LBA に書く raw boot の
`boot_hdd.bin` / `loader_hdd.bin`、H-7 の apps / game はファイル集合に入れない。
`allow_list` は `/etc/settings.db`、`/etc/system.cfg`、`/var/log/*` の 3 種で
`check=exists` (存在だけ、内容 hash は照合しない)。ログの wildcard は実在するログに
適用し、新規ログの作成を要求するものではない。世代表の既存集合・形式は維持する。

名札照合は host で実施する (配備元ルートを明示する):

```sh
python3 tools/deploy_source_check.py --root <HostDrv-or-SerialFS-root> \
  --deploy-set build/out/deploy-set.json --guest-paths <managed-paths.txt>
```

`.deploy/manifest.txt` の全ファイル行 (format 1 / 2) を配備元の存在・size・CRC-32 で
照合する。count 不一致・不正行・重複・管理対象行の欠落も列挙して rc≠0。
外部など期待集合外の名札行も照合を省かない。mtime は内容照合に使わない。
成功時は deploy-set の guest 列を並べた管理対象一覧を stdout と指定ファイルへ出す。
失敗時は同期・HDD 起動へ進まない。旧一式を使う復旧時も旧一式の名札と管理対象集合で
同じ照合をする (新一式の deploy-set を旧一式の照合に流用しない)。

この結果に続き、PM は `hsync --root /hd0 --no-backup --verify` と
`hsync --root /hd0 sys --verify` の両方で変更 0 / errors 0 を確認し、管理対象一覧と
HDD の `ls -R` を突き合わせ、allow-list 以外の余剰を列挙する。
NP21/W の隔離 NHD では stop → umount → pull → `verify-set` により
deploy-set と集合・size・hash 一致、余剰 0 を確認する (h4b / PM の受入)。
host の名札照合だけを HDD 内容一致の証拠にしない。
