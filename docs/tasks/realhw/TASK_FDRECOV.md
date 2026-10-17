# FDRECOV-1 — FD 起動から LAN で HDD 一式を復旧する

> 状態: **実装中 (2026-10-17)** — main 3dd05eb で着地し NP21/W の受入 7 項に合格 (`~/os32-tmp/evidence/2026-10-17/fdrecov_np21w/RESULT.md`)。残りは実機の LAN の部分 (台帳 FDRECOV-1)。それまでの状態: 設計は astra 起草 (r1〜r3)、Opus 5.5 の独立レビュー 2 往復 (r1「直してから」P1×2、r2「R2-1・R2-2 を直せば確定してよい」、r3 で反映)。実装は未着手。往復の記録は `~/os32-tmp/run/fdrecov/` (`opus_r1.md`・`astra_r2_response.md`・`opus_r2.md`・`astra_r3_response.md`)。台帳 [FDRECOV-1](../DEFERRED_TESTS.md)。
>
> 設計案 r3 / 2026-10-17。参照ソース: `0c71845781d66a40968b04c932f96e6b31c304cc`。配置世代の違うカーネルだけを `hsync boot` で更新し、旧 shell が起動不能になる事故を防ぐ。PM 決定として F1〜F8・Q1〜Q4・R2-1〜R2-6 を採用する。

## 1. 方針と対象

**道具入り FD → LAN 設定 → HDD 全体の同期 → 成功後に HDD 再起動**の1段で復旧する。最小インストールは挟まない。対象はマウント可能な既存 ext2 HDD。区画・IPL/ローダ・FS 自体の修復は対象外で、ブートセクタは書かない。単一利用者・協調実行とし、同期中は配備元を変更しない。

FD は既存の BOOT + MINIMAL（PCI/82557/SerialFS、shell の rshell/sfs、hsync、install/cdinst/cfg）を維持し、FD 自身のカーネル・shell・hsync は同じビルドで作る。新 NIC ドライバや別の sfs バイナリは不要。**L-F2 `32b07e4` 以降の FD なら、配備元と配置世代を揃えずに使える**。旧 FD は2コマンド、新 FD は1コマンドとする。将来の未対応 FS・媒体/ローダ・通信・名札形式への無期限の対応は意味しない。全体復旧は配備元の OS32X 形式には依存しない。

## 2. LAN 設定

既存の `sfs lan on` で FD の `/etc/system.cfg` の `SFS_LAN` を保存する。`SFS_LAN_PEER` は空（auto）を既定とし、LAN に他のホストがあるときだけ `sfs lan peer <ホストNICのMAC>` を指定する。以前の指定を戻すには `sfs lan peer auto`。MAC は媒体に固定しない。設定は次のセッションから有効で、セッション中の変更拒否・MAC 検査は維持する。

RAM 上書き・KAPI 追加・版上げは行わない。新 FD は cfg 保存失敗時に非0とともに `sfs: /etc/system.cfg に書けない (書込み禁止?)` を表示する。書込み禁止なら解除して再試行する。設定は FD に残り、install で FD の `/etc` を写す場合は HDD にも引き継がれる。hsync の復旧対象は HDD であり、同期中に FD へは書かない。

## 3. 名札と同期の門

**名札は format=2、既存6鍵・ファイル行のまま**とし、新しい鍵は足さない。全件配備成功後だけ公開し、部分配備・失敗時に旧名札を消す契約も維持する。ホストは既存の generations-manifest / deploy-set、`deploy_source_check.py` によるビルド集合・hash 照合を使う。`hostdrv_deploy.py` の OS32X ヘッダ読取りを48Bから v4 の60Bへ直す。

配備元は一式すべてを前提とする。新 hsync は稼働ルートへの同期に限り、書込み前に `/host/sys/shell.bin` の先頭60Bを読み、OS32X の形式を検査する。その門では生成済み `os32_generations.h` の配置・KAPI ABI・OS32X 形式・shlib protocol と `kapi_data_off` を照合する（protocol=0 は依存なしとして既存 exec と同じ扱い）。hsync 自身が稼働カーネルに受理されたことを前提にその定数を使い、世代照会 API は足さない。shell の `min_api_ver` はこの世代比較に使わず、同世代の「カーネルを先に運ぶ」KAPI 版の例外を残す。

| 同期先・範囲 | 新 hsync の判定・動作 |
|---|---|
| 稼働ルート（`--root /` を含む）、boot/sys/全体/部分 | shell ヘッダと稼働世代を比較。通常の全体同期は従来どおり `/sys` を除外 |
| 別デバイスの ext2 マウント根、範囲指定なし | 全体復旧。FD と配備元の世代・KAPI 版の一致は要求せず、`/sys` も同期 |
| 別根への部分同期 | 従来の稼働カーネルに対する名札/KAPI の門を維持。shell ヘッダは検査せず、全体の起動整合は保証しない |

別根は正規化済みマウント根・ext2・稼働ルートと異なるデバイスで判定し、同一デバイスの別名を復旧扱いにしない。**宛先 `/hd0/boot` のカーネルとは比較しない**。旧版・欠損・破損は復旧対象であり、この点の台帳修正は PM が行う。

稼働ルートの門では、世代差は `reason=memory_layout_mismatch host=4 running=3; 0 files written` 等、理由と両値を表示して非0終了する。shell の欠落・短い/不正/未対応ヘッダは「世代を確かめられない」として停止する。どちらも `-f`・`--force-kapi` では越えず、FD 全体復旧を案内する。既存の名札/KAPI の門と調査用 `--force-kapi` は残すが、全体復旧は有効な format=2 を要求する。format=1・名札なし・破損は従来の名札判定に従い、全体復旧には使わない。

## 4. 復旧時の検査と失敗時の扱い

別根全体復旧での配備ファイルの事前検査は **`/host/boot/vmkernel.lz4`・`/host/sys/shell.bin` の存在の stat だけ**とする。shell ヘッダの読取り・解釈や FD との世代比較は求めず、短い/不正/未対応ヘッダを理由に止めない。名札の門は§3のとおり維持する。コピー中の流し CRC を名札の該当行と照合し、不一致はその1件を errors に数える。全行の事前 CRC、全バイナリのヘッダ照合、kdebug の対の検査は加えない。余剰ファイルは従来の `manifest_extra` 集計を保ち、拒否条件にしない。

書く能力は FD カーネルの VFS と必要 API・ext2 操作に依存し、配備元 KAPI 版との大小比較で代用しない。新 hsync の全体復旧では旧直接上書きへフォールバックせず、必要な能力がなければ書込み前に対応 FD を案内する。root_guard、FD 宛先拒否、settings.db 保護、排他的作成・読戻し・rename・エラー集計を保つ。

`--verify` で同サイズ・同mtimeの破損も修復し、`--no-backup` で起動実績不明の `.old` を作らない。通常のバックアップ規則は維持する。一式の原子性は保証しない。通信断・電源断・CRC 不一致時は FD のまま原因を直して再実行し、終了成功かつ `errors=0` の後だけ HDD 起動する。

## 5. 復旧手順（profile_fdd / hsync.1 に記載）

1. ホストで検証済みの同一ビルド一式を配備し、`lanfs_host.py --iface <NIC> --root <配備根>` を起動する。
2. FD 起動後、rshell 接続から `sfs lan on` を実行する。peer は既定 auto。他ホストがある場合だけ MAC を指定する。
3. 新 FD ではホストから次の1行を送る。sfs run は本体キーボードでなく、rshell の行全体として送信する。

```
rshell_serial.py --port <port> --sfs-lan --timeout 600 cmd sfs run hsync --root /hd0 --verify --no-backup
```

旧 FD（L-F2 `32b07e4` 以降、FDRECOV-1 未搭載）は全体同期で `/sys` を除外するため、同じ接続で次の2行を順に実行する。両方の終了成功・`errors=0` を必須とし、失敗時は2行を再実行する。旧 shell は cfg 保存失敗の説明を出さないため、`sfs lan on` が失敗したら書込み禁止を解除して再試行する（hsync.1 の旧 FD の段にも明記）。

```
rshell_serial.py --port <port> --sfs-lan --timeout 600 cmd sfs run hsync --root /hd0 --force-kapi --verify --no-backup
rshell_serial.py --port <port> --sfs-lan --timeout 600 cmd sfs run hsync --root /hd0 --force-kapi --verify --no-backup sys
```

`32b07e4` のソースで3オプションの存在を確認済み。旧 hsync は世代の門や新しい流し CRC 照合を持たないが、format=2 を読める。配備元の新 hsync を旧 FD 上で実行する必要はない。

4. 成功後に FD を外して HDD 再起動し、ver・shell・代表アプリを確認する。シリアル制御と LAN 転送の両方を使い、raw socket 権限・シリアル速度は既存設定に従う。

## 6. 変更箇所・受入

| 箇所 | 変更 |
|---|---|
| `userland/system/hsync.c` | 稼働ルートだけの shell ヘッダによる世代の門、別根全体の `/sys` 同期・存在 stat、コピー中 CRC 照合 |
| `tools/hostdrv_deploy.py` | OS32X ヘッダの60B読取り。名札形式は維持 |
| `userland/shell/cmd_sys.c` | cfg 保存失敗の説明1行 |
| `assets/profile_fdd`、`docs/manpages/hsync.1` | 新旧 FD 手順、cfg 永続化、カーネル先行は世代一致時のみと明記。古い format=1 等の記述も訂正 |
| `tools/tests/hsync_h1_host.c`、`hsync_h2_host.c`、`hsync_h3_host.c`、`h4_manifest_host.c`、`test_hostdrv_manifest.py`、関連 `tools/check_map.d/` | 世代定数の include / sys_read 差替えで shell ヘッダを供給し、門・復旧・名札互換を検証 |

- ホスト: H1〜H4、hostdrv_manifest、deploy_set、packages、LAN設定、install fresh/recover の関連試験。新 `hostdrv_deploy.py` が生成した名札を、`git show 0c71845:userland/system/hsync.c` で取り出した旧 hsync を組み込む `h4_manifest_host` に読ませ、`manifest_invalid` にせず受理すること。新 hsync は稼働ルートの boot/sys/全体の世代差・shell 欠落/不正を `-n`/`-f`/force でも拒否し、拒否時は書込み・mkdir・rename・mtime変更・sync 呼出し0。同世代の KAPI 版だけ上昇は boot 先行可能。別根全体では異世代・短い/不正/未対応 shell ヘッダでも拒否せず、必須ファイル欠落は書込み前に拒否すること。別デバイス/同デバイス別名、コピー中 CRC 不一致、余剰集計、settings.db、通常のsys除外・.old、および `dst_on_fd` / `root_guard` による FD 宛先拒否を確認する。
- FD 像: 2HD/1.44MB を再生成し、BOOT + MINIMAL 集合一致と空き64 KiB以上を確認する。改修前の既存像の測定値は各211 KiB / 420.5 KiB（FAT12空きクラスタ数）。改修後の容量は未測定。
- NP21/W: 両像で起動、cfg 保存・再起動後の持続、書込み禁止時の新 FD のエラー文言と非0を確認し、`sfs lan on` が非0を返すまでの秒数を1回測る（書込み禁止を再現できなければ実機のみで確認）。cfg 設定後と hsync 後の FD 像の SHA-256 が一致することを確認する。HostDrv または既存 SerialFS で、異世代 FD から旧/欠損 boot・旧 shell の HDD を復旧し、sys 更新・同サイズ同mtime破損修復・失敗後再実行・再起動を確認する。旧 FD は `6b4e37e`（または `32b07e4`）のビルドから像を取り、2行とも終了成功・`errors=0` と HDD 再起動後の ver/shell/代表アプリ起動を確認する。PCI/82557 を再現しないので LAN 合格には数えない。
- 実機: 82557 初期化→シリアル制御→auto peer の LAN 転送→HDD 再起動を最小インストールなしで完走する。必要時の明示 peer、wrong peer/ケーブル抜けからの再試行も確認する。errors=0 と ver/shell/アプリ起動を記録する。実機の FD 書込み統計差分は受入から外し、FD 保護は既存の `dst_on_fd` / `root_guard` とホスト試験で担保する。観測口は追加しない。未定義の SerialFS 要求数上限は受入に置かない。

**1段で実装・統合する。** KAPI 追加・名札移行・make clean は本変更の要件にしない。完了条件はビルド・関連試験・両FD容量/集合と NP21/W・実機の観測。実機未実施は未確認と記録する。本改訂は設計文書のみで、ソース変更・ビルド・配備・ゲスト/実機試験は未実施。
