# ホットデプロイ再設計

> 発行: PM (2026-09-01) / 状態: **受入完了 (2026-09-01)**
> 段 1〜3 実装済み (2026-09-01)。
> 関連: `docs/POLICY_DEBUG.md`、`~/np21w-src/docs/02-architecture.md`

再起動なしで単一バイナリを実機へ送り込む経路が、4 箇所で独立に壊れている。
本書はその実測結果と、aidebug (NP21/W 内蔵デバッグ API) と同じ流儀で
直接転送する仕組みの設計をまとめる。

## 1. 現状 — 壊れている 4 点 (2026-09-01 実測)

| # | 箇所 | 症状 |
|---|------|------|
| 1 | `userland/shell/rshell.c:159` | `cmd_upload` は本体が削除され、赤字で "upload: removed" を出すだけ。`nhd_deploy.py push` はこのコマンドを送り続けている → シリアル・ホットデプロイは常に失敗 |
| 2 | `userland/shell/rshell.c:214` | 代替とされた `recv host:PATH` は `xfer_buf[4096]` に `sys_read` を **1 回**呼ぶだけ。ループが無いので 4KB を超えるファイルは黙って切り詰められる。最小のアプリでも 15KB あるため実用にならない |
| 3 | `tools/hostdrv_deploy.py:29` | `tools/deploy.yaml` を読むが、このファイルは 2cd9872「配備マニフェストを層ごとに分割」で削除済み。`load_deploy_yaml()` は `None` を返すのに **exit 0** で終わるため、`make deploy` は成功扱いで何もしない |
| 4 | `build/deploy.mk:15` | `deploy-kernel` は `hostdrv sync` → `sync-from-hostdrv` → `deploy` の順。#3 で第 1 段が空振りするので、**古い `C:\os32` の木がそのまま NHD へ書かれる**。CLAUDE.md が謳う「HostDrv を先に同期するので ext2 を古いまま上書きする事故を防げる」保護は現在効いていない |

補足: `make deploy-nhd` は `nhd_deploy.py` が層別 yaml
(`build/core.yaml` / `userland` / `apps` / `game`) を正しくマージするので
マニフェストは正しいが、`NHD_LOCAL` (`/tmp/os32.nhd`) が消えていると
`do_write_boot` が `FileNotFoundError` で落ちる。`/tmp` は再起動で消えるため、
`nhd-init` からやり直す必要がある。

なお `/host` マウント自体は生きている (実機で `ls /host` を確認済み)。
壊れているのはホスト側の同期とゲスト側の転送コマンドだけ。

## 2. 使える材料

aidebug は既に必要な原始関数を持っている (`src/win9x/aidebug/aidebug_api.cpp`)。

| API | 用途 |
|---|---|
| `POST /api/mem?addr=&space=phys` | 物理メモリ書き込み。body は hex 文字列。`cpu_memorywrite` を `core_lock()` 下で回す |
| `GET /api/mem?addr=&len=&space=` | 読み戻し (完了ポーリング用) |
| `POST /api/cmd` | rshell へコマンド行を送る |
| `POST /api/serial/write` | 生バイト host→guest |

ゲスト側は 15MB 実装で、`0x400000-` が Program Space。
`MEM_SHM_BASE` (256KB) は既に存在するが KHEAP 末尾からの動的配置なので、
ホストが番地を知るには読み出しが要る。

## 3. 設計方針

**ファイル書き込みはゲストにやらせる。** 稼働中のゲストの背後で
エミュレータが NHD の ext2 を直接書くと、ゲストのブロックキャッシュと
必ず食い違う。ext2 の書き込み実装をエミュレータ側に持つコストも見合わない。
ホストがやるのは「バイト列をゲストの物理メモリへ置く」ところまで。

**転送はシリアルを通さない。** `upload` は hex で 2 倍に膨らませた列を
エミュレートされたシリアルへ流していた。`game.bin` (118KB) なら分単位になる。
物理メモリへの直接書き込みなら HTTP 1 往復で済む。

### 3.1 ステージング領域

物理メモリ末尾に固定窓を切る (`include/memmap.h`)。

```c
/* ホットデプロイ用ステージング窓 (物理末尾 256KB) */
#define MEM_HOTDEPLOY_SIZE   0x040000UL          /* 256KB */
#define MEM_HOTDEPLOY_BASE   (phys_end - MEM_HOTDEPLOY_SIZE)
```

`exec_run()` がシステムメモリ量からヒープを算出しているので、
そこから本領域を差し引くこと。固定番地にする理由は、ホストが
番地を知るための読み出しを 1 往復省くため。番地はブート時に
制御ブロックの magic とあわせてカーネルが出力する。

### 3.2 制御ブロック

ステージング窓の先頭に置く。全フィールドはリトルエンディアン。

| offset | size | 名前 | 意味 |
|---|---|---|---|
| 0x000 | 8 | `magic` | `"OS32DPLY"` |
| 0x008 | 4 | `version` | 1 |
| 0x00C | 4 | `seq` | ホストが要求ごとに増やす |
| 0x010 | 4 | `status` | 0=idle 1=request 2=busy 3=done 4=error |
| 0x014 | 4 | `length` | データ長 |
| 0x018 | 4 | `crc32` | データの CRC32 |
| 0x01C | 4 | `errno` | status=4 のときの理由 |
| 0x020 | 4 | `mode` | 0=通常 1=実行ビット付与 |
| 0x040 | 256 | `path` | ゲスト側の絶対パス (NUL 終端) |
| 0x140 | — | `data` | 本体 |

`status` はゲストだけが 2/3/4 を書き、ホストだけが 1 を書く。
`magic` と `version` が一致しない要求はゲストが黙って捨てる
(ロード失敗時の残骸を掴まないため。`utf8_set_jis_table_ready` の
教訓と同じ理由)。

### 3.3 手順

```
host                              emulator                guest
 |-- POST /api/deploy ----------->|                          |
 |   ?path=/usr/bin/x.bin         | core_lock()              |
 |   body = binary                | data/header を書く       |
 |                                | status=1                 |
 |                                | core_unlock()            |
 |<-- {"ok":true,"seq":N} --------|                          |
 |                                |         status==1 を検出 -|
 |                                |                 status=2 -|
 |                                |          magic/crc 検証  -|
 |                                |    sys_open/write/close  -|
 |                                |          status=3 or 4   -|
 |-- GET /api/deploy ------------>|                          |
 |<-- {"status":"done","seq":N}---|                          |
```

ゲスト側のポーリング位置は 2 案ある。

- **PIT ハンドラ末尾** — シェルが応答不能でも動く。割り込み文脈で
  ext2 書き込みをするわけにはいかないので、フラグだけ立てて
  カーネルのアイドルタスクで実処理する
- **シェルのアイドルループ** — 実装は軽いが、外部プログラム実行中は止まる

検証ループの用途 (ビルド → 配備 → 実行) では前者が要る。プログラムが
ハングしたときこそ差し替えたいため。

## 4. 実装案の比較

### 案 A — エミュレータ変更なし (推奨・第 1 段)

既存の `POST /api/mem` でステージングし、新しい rshell コマンドで流し込む。

```
POST /api/mem?addr=<MEM_HOTDEPLOY_BASE+0x140>&space=phys   body=<hex>
POST /api/cmd   body="hotdeploy /usr/bin/x.bin <len> <crc>"
```

- 変更はゲスト側 60 行程度 (`rshell.c` に 1 コマンド追加)
- エミュレータのリビルド不要。**今日から使える**
- 制約: rshell が応答している必要がある。外部プログラム実行中は使えない

### 案 B — aidebug に `/api/deploy` を追加 (第 2 段)

3.3 の全体を実装する。

- HTTP 1 往復。ホスト側スクリプトが単純になる
- rshell を占有しない。プログラム実行中・ハング中でも差し替えられる
- コスト: `aidebug_api.cpp` にハンドラ 1 本 + ルーティング 1 行、
  ゲストにカーネル常駐エージェント。`np21x64w.exe` の再ビルドが要る

### 案 C — エミュレータが NHD を直接書く (却下)

稼働中のゲストのブロックキャッシュと整合が取れない。ext2 の書き込み実装を
エミュレータへ持ち込むコストも見合わない。

## 5. 案 A/B と別に直すべきもの

設計とは独立に、以下は壊れたままにしない。

1. `tools/hostdrv_deploy.py` — 参照先を `nhd_deploy.py:63-66` と同じ 4 層に揃え、
   マニフェストが無いときは **exit 1** で落とす。「無いのに成功」が今回 10 分溶かした
2. `tools/nhd_deploy.py` の `do_push` — 送り先の `upload` が存在しない。
   案 A の `hotdeploy` に張り替えるか、コマンドごと削除する
3. `userland/shell/rshell.c` の `cmd_recv` — 4096 バイト切り詰めをループに直すか、
   案 A ができた時点で削除する。黙って切り詰める実装は残さない
4. `build/deploy.mk` の `deploy-nhd` — `NHD_LOCAL` 不在時に
   `FileNotFoundError` を投げる。`nhd-init` を促すエラーに変える

## 6. 実装

### 段 1 — 壊れていた経路の修復

- `tools/deploy_manifests.py` を新設し、層別マニフェストのリストとマージを
  一元化。`nhd_deploy.py` と `hostdrv_deploy.py` の両方がここを参照する
- `hostdrv_deploy.py` は削除済みの `tools/deploy.yaml` を見なくなり、
  マニフェスト不在時は exit 1 で落ちる。`guest` が `/` 終わりのとき
  ファイル名を補うようにした (補わないと毎回コピーし直していた)
- `nhd_deploy.py do_write_boot` に `NHD_LOCAL` 不在チェック。素の
  `FileNotFoundError` ではなく案内を出して `do_sync` を止める
- `nhd_deploy.py pull` / `make nhd-pull` を追加。`/tmp` が消えたあとの
  再開用。`init` はフォーマットを伴うのでゲスト側データが消える
- `rshell.c cmd_recv` の 4096 バイト切り詰めをループに修正。パス用の
  バッファを分離し、`xfer_buf` との別名参照も解消
- `resolve_guest_path` は glob パターンをフルパスで照合するようにした。
  以前はパターンのベース名 (`*.bin`) だけを見ていたため、apps/ のアプリが
  最初に現れた `*.bin` エントリ (= `/bin/`) に吸い込まれていた
- `dp-%` パターンルールを廃止し `make hotdeploy FILE=...` に置換。
  GNU make はターゲットパターンにスラッシュが無いとディレクトリ部を
  除いて照合するため、`dp-apps/x/x` はどの規則にも当たらず成立していなかった

### 段 2 — 案 A (rshell 経由)

- `rshell.c` に `hotdeploy` コマンドと 256KB のステージングバッファ
- `tools/hotdeploy.py` — `/api/mem` で 16KB 刻みに転送し、CRC 付きで書かせる
- `np21w_mcp/np21w_client.py` を直結優先に変更 (WSL ミラーモードなら
  Windows curl.exe を経由しない。NAT 環境では従来どおり fallback)

### 段 3 — 案 B (常駐エージェント)

- `include/memmap.h`: `MEM_HOTDEPLOY_SIZE` (256KB) と
  `MEM_HOTDEPLOY_DESC` (0x8C000) を定義
- `kernel/sys.c`: `sys_usable_mem_end()` — 物理末尾から予約分を引いた上限。
  `exec/exec.c` (2 箇所) と `kernel/pgalloc.c` がこれを使う
- `kernel/hotdeploy.c`: 制御ブロックの初期化とポーリング。`lib/crc32.c` を新設
- ポーリング地点: `drivers/serial.c serial_getchar()` の `hlt` 待ち
  (rshell のコマンド待ち)、`kernel/sys.c sys_halt()` (プログラムの待ちループ)、
  `exec/exec.c exec_exit()` (プログラム終了直後)
- エミュレータ: `aidebug_api.cpp` に `GET/POST /api/deploy`、
  `aidebug_server.cpp` の `AIDEBUG_MAX_BODY` を 64KB → 2MB

### 書き換えを許す範囲

**ユーザーランドに限る。** `/boot/` と `/sys/` への書き込みはゲスト側が
`HD_ERR_DENIED` (5) で拒否する。カーネル本体 (`/boot/vmkernel.lz4`) と
システム常駐物 (`/sys/shell.bin`, `sqlite.bin`, `unicode.bin`, フォント) は
稼働中に書き換えると走っている当人を壊すか次回ブートを壊すため、
NHD フル配備 (`os32-cycle deploy`) で入れ替える。

判定はゲスト側 (`kernel/hotdeploy.c hd_path_is_system`) が持つ。
`tools/hotdeploy.py` にも同じ規則があるが、そちらは早めに分かりやすい
エラーを出すためのもので、拒否の権限はゲストにある。

### 到達できていない範囲

**完全にハングしたゲストは救えない。** OS32 はスケジューラを持たず、
外部プログラムが CPU を握ったまま戻らない場合、カーネルに制御が返る地点が
無い。割り込み文脈でディスク I/O はできないため、タイマ割り込みからは
書けない。段 3 が広げたのは「シェルにコマンドを送らずに差し替えられる」
「プログラムが `sys_halt` を回していれば実行中でも反映される」
「プログラム終了直後に自動で反映される」の 3 点で、ハード・ハングからの
復帰は依然として再起動が要る。
