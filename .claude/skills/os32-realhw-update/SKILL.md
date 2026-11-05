---
name: os32-realhw-update
description: 実機 PC-9821Ra266 の OS32 を、HDD 起動のまま新しい版 (カーネル・部品・ユーザーランド) に更新する。ノート (Ubuntu、Remote Control のセッション) に手順を頼み、LAN 版 SerialFS の hsync で boot → 再起動 → 全体 → 再起動の順に入れて ver で確かめる。「実機更新して」「実機に入れて」の依頼で使う。FD 起動・HDD の作り直し・NP21/W への配備は扱わない。
---

# os32-realhw-update — 実機 Ra266 を HDD 起動のまま更新する

PM は実機に直接つながっていない。実機のシリアル (/dev/ttyUSB0) と LAN はノート側にある。
ノートのセッション名は変わるので (2026-11-03 は「実機ノート」)、毎回 `ListAgents` で Remote Control の行を探して
`SendMessage` で頼む (返事は来ないことがある。結果はノートの証拠の所在を報告させて読む)。
**一覧に別の機械のセッションが 1 つも出ないときは、このセッション自身が Remote Control につながっていない** — ユーザーに `/remote-control` を頼む (2026-11-03、3 回探して見つからなかった)。
**PM が ssh でノートに入り、配備・再起動・シリアルの台本を直接流さない** (2026-10-30 にこれをして、ノートに依頼の記録が残らず、CI の成果物でない手元ビルドを入れた — ユーザー指摘)。ssh で PM がしてよいのは**ファイルを置くこと** (試験を飛ばして手元ビルドの成果物を入れるとき、scp で送って ~/os32-ci/os32-local-<SHA> に展開する) と状態の確認 (ファイルの有無・git の版) まで。**置いた後の処理** (lanfs_host の起動・hsync・再起動・シリアルの試験) は依頼文に書いてノートに頼む (ユーザー指示 2026-10-30)。

## 0. 前提の確かめ (頼む前に PM が行う)

1. **承認**: 実機での情報収集とカーネル更新は**許可済み** (ユーザー 2026-10-21)。条件は、同じ成果物で NP21/W を起動し
   **シリアル (rshell) で ver が返ること** — それでカーネルの正常動作は OK とみなす。画面はカメラで PM が判定してよい (§2、RA266 §3-1)。
   物理操作 (電源・FD・ケーブル)・音の確認・HDD の上書きや作り直し・配置の世代が変わる更新はユーザーに残す。
2. **入れる版**: push 済みで **CI の `artifacts` ジョブが成功した SHA** を選ぶ (`ci_fetch.sh --sha <SHA> --dry-run`)。
   取得条件・検査状態の表示は [08_build §8-6](../../../docs/08_build.md#8-6-github-actions-での本体ビルド) が正典。
   文書だけのコミットには build が走らないので、その前の成果物がある SHA を使う (コードは同じ)。
   **実機の試験・調べの版** (作業木の診断ビルドなど) は push・CI を待たず、ビルドが通れば入れてよい (ユーザー決定 2026-10-28、
   [ROLES §0](../../../docs/tasks/agents/ROLES.md)「検査」の行: ビルド rc=0・既定の成果物が基点と同じ・直接の試験 `MUTATE=0` だけ。固まる危険は受け入れ、戻し方を依頼に書く)。
3. **世代の照合** (`sdk/include/os32/os32_generations.h` を今の実機の版と比べる):
   | 変わったもの | 扱い |
   |---|---|
   | `OS32_MEMORY_LAYOUT_GENERATION` | 配置の作り替え。この手順では入れない。ユーザーに手順を示して別に決める |
   | KAPI の版 (`KAPI_VERSION` / `OS32X_MIN_API`) が上がった (追記だけ) | 実機の旧い hsync は「配備物の版がカーネルより新しい」と全体同期を断る (`userland/system/hsync.c` の kapi_version 照合、`/boot` だけは通す)。**boot を先に入れて再起動**してから全体を入れる |
   | どちらも同じ | 同じ順でよい (boot → 再起動 → 全体) |
   互換の無い更新 (配置・ABI が変わる版) は、実機に入れる前に手順をユーザーに示す。
4. **旧いユーザーランドが新しいカーネルで動くか**: boot の後の 1 回目の再起動から全体同期までは、
   新カーネル + 旧ユーザーランドで動く。アプリの要求する KAPI がカーネル以下なら受け付ける
   (`exec/os32x_hdr.c`)。shell の起動時の積み方や service 表を変えた版は、旧 crt0・旧 gshell が
   壊れないことを差分で確かめておく。

## 1. ノートへの依頼の形 (この順で書く)

```
OS32 実機 (Ra266) を os32-v3 main <SHA> に更新してください。ユーザー承認済み (<日付と言葉>)、PM セッションからの依頼です。
世代: <memory_layout と KAPI の新旧、boot を先にする理由>。新しく配られるもの: <あれば>。
手順 (シリアルで転送するときは `--fast 115200`、取りこぼしたら速度を下げる):
1. ノートで git pull、`tools/ci_fetch.sh --sha <SHA> --wait` (artifacts の完成を待って取得、検査状態も確認)。lanfs_host を止め、
   新しい成果物の hostdrv を root にして起動し直す (peer は今の値)。
2. `sfs run hsync -n boot` (計画だけ確認) → `sfs run hsync boot` → reboot → ver で Commit <SHA>。
3. `sfs run hsync` (全体、/sys は既定で外れる) → `sfs run hsync sys` (常駐 shell・共有ライブラリ・フォント) → reboot → ver (Commit・Image CRC・services の行が SERVICES_READY)、
   起動ログの部品の PAIR と rc、`mem` の leftover/audit_fail、`db_test`。
4. <画面不要の試験があれば、各コマンドと期待値>
5. 最後に CUI のまま `ver`。
止まる条件: 2 か 3 で shell まで来ない・シリアルが応答しない → それ以上触らずに状態を報告 (物理操作はユーザー)。
手順外の上書き・settings.db の削除・GUI に入って画面を要する操作はしない。
証拠は ~/os32-tmp/evidence/<日付>/<名札>/ (ノート側でよい、所在を報告に書く)、報告は短く (各段の rc、ver、試験の結果、異常)。
```

- **経路の優先度: LAN 版が先、シリアル版は LAN が使えないときだけ** (FD 起動でも LAN は使える。正典は [RA266](../../../docs/decisions/realhw/RA266.md) §0 の「転送の経路の優先度」)。依頼文にも「LAN で」と書き、シリアルで始めていたら CTRL+STOP で止めて LAN でやり直させる。
- LAN 版 SerialFS の使い方 (`sfs lan peer`・`sfs lan on`、ノートの `lanfs_host.py --iface … --root … --peer …`、
  `rshell_serial.py --sfs-lan … cmd "sfs run hsync boot"`) は
  [TASK_LAN_82557](../../../docs/tasks/realhw/TASK_LAN_82557.md) の状態行。LAN が使えないときは
  シリアル版 (`--serve-host <hostdrv>`) で同じ順 ([RA266](../../../docs/decisions/realhw/RA266.md) §0 の引用)。
- `hsync boot` は `/boot/vmkernel.old` を残す。戻すときは旧カーネルで起動できる (hsync の bootold)。

## 2. 結果の受け取り (PM)

- ver の Commit と Image CRC が依頼した SHA の成果物と一致するか。services が SERVICES_READY か。
- 起動ログの部品の PAIR と rc。`mem` の leftover・audit_fail が 0 か。
- **画面はカメラで PM が見る** (ユーザー決定 2026-10-31、[RA266](../../../docs/decisions/realhw/RA266.md) §3-1): `curl http://hight-pc-vj24lfwz1srg:8080/api/frames` の末尾の名前で `/frames/<名前>` を取り、写った・崩れ・真っ黒を判定する。文字の照合はシリアル。
- 音・物理操作が要る項目は実施しないで、台帳 ([DEFERRED_TESTS](../../../docs/tasks/DEFERRED_TESTS.md)) の該当行に
  「立会い待ち」として残す。
- 票の状態行と証拠の所在を記録する。実機の基準構成が変わっていたら [RA266](../../../docs/decisions/realhw/RA266.md) §1-1 を先に照合。

## 3. 観測と受け渡しの道具 (2026-11-03 に確かめた形)

- **ssh / scp**: `ssh -i ~/.ssh/brain_pwsh4 -o BatchMode=yes hight@100.76.241.127` (tailscale の IP)。ホスト名 `hight-pc-vj24lfwz1srg` で入ると鍵が違い `Permission denied`。scp も同じ鍵と IP。してよいのは §冒頭のとおりファイルを置くことと状態の確認まで。置いたら sha256 を両側で照合する。
- **カメラ** (画面): `curl -s http://hight-pc-vj24lfwz1srg:8080/api/frames` (JSON、約 5fps・直近約 300 秒分の名前の配列 `YYYYMMDD-HHMMSS-mmm.jpg`、末尾が最新) → `curl -s -o x.jpg http://hight-pc-vj24lfwz1srg:8080/frames/<名前>` を Read で見る。CUI の文字はおおむね読める — 写った・崩れ・真っ黒の判定だけ (正典 [RA266](../../../docs/decisions/realhw/RA266.md) §3-1)。**ノートに reboot の時刻 (秒まで) を先に返させ**、その時刻からの枚を見る。
- **起動の途中のシリアル** (起動ログ・診断の行): ノートが rshell で `reboot` を打ち、そのまま約 150 秒シリアルを記録して `<名札>/splr-boot.log` と `.raw`、各段の開始・終了・rc を `TIMES.txt` に残す (2026-11-02 の splash-diag-b9c239d4 の形)。依頼文に「前回と同じ採り方」でなく、この 1 行を書く。
- **診断の像 (別の BUILD_OUT、例 `make kernel-splash-diag` → `build/out/splash-diag/`) の渡し方**: `deploy.yaml` は `build/out` を前提にするので、次の手で HostDrv の根を作る。
  1. 診断の作業木で `make all` と診断の像を組む (重い仕事は同時 2 本まで — メモリ不足で止められた)。
  2. NP21/W の予行 (罠の 1 行目): `np21w_ctl.py stop` → 作業木で `nhd_deploy.py pull` → `make deploy-nhd` → `umount` → `start --wait-ready`。続けて `make deploy` (HostDrv `/mnt/c/os32`) の後、`build/out/splash-diag/{*.mod,vmkernel.lz4,kdebug.bin}` を `/mnt/c/os32/boot/` に上書きし、**目録 `/mnt/c/os32/.deploy/manifest.txt` の `boot/` の行を `tools/hostdrv_deploy.py` の `manifest_line()` で作り直す** (そのままだと hsync が `manifest_crc_mismatch` で 13 件とも断る)。ゲストで `hsync -n boot` → `hsync boot` → `emu_reset`。
  3. NP21/W の起動の途中のシリアルは `curl.exe -s http://127.0.0.1:8025/api/serial/read` (JSON の hex、読むと消える) を 2 秒ごとに約 90 秒集めて読む。最後に `ver` で Image CRC と `kdebug: on (pair …)`。
  4. 実機へは `/mnt/c/os32` を丸ごと tgz にして scp で `~/os32-ci/os32-local-<名札>/` に置き、ノートには root をそこの `os32` にして `sfs run hsync boot` と頼む。戻しの root (今の常用の成果物の場所) は、準備の問い合わせでノートに返させる。
  5. 終わったら NP21/W と `/mnt/c/os32` は `make deploy` (と必要なら `deploy-nhd`) で常用に戻す。

## 罠

- **実機に入れる像は、その像そのものを NP21/W で起動してから** (2026-11-01): 常用の像の受入だけ回して、診断用の像 (make kernel-splash-diag など別の BUILD_OUT の一式) を NP21/W で一度も起動せずに実機へ入れ、起動のスプラッシュで #PF (probe が写像の無い BIOS の領域を P2V_IO で読んだ) → HDD から起動できなくなり FD 起動が要った。別の構成の像は、検査の作業木で deploy-nhd して NP21/W で ver まで来るのを見てから送る。

- 文書だけのコミットの SHA を頼むと CI の成果物が無い (`ci_fetch.sh` が見つけられない)。
- 全体同期を先にすると旧 hsync が 1 件も書かずに断る (KAPI が上がった版)。必ず boot が先。
- 全体の `hsync` は `/sys` を既定で外す (`userland/system/hsync.c:45-48`)。shell のコマンドや共有ライブラリを変えた版で `hsync sys` を抜かすと、新しいカーネルに古い shell のまま動く (2026-10-24、route-test の Usage が出ないことで気づいた)。
- シリアルで hsync するときは `rshell_serial.py --fast 115200 --serve-host …` を使う (ユーザー指示 2026-11-05、9600 の 1.2MB の boot に 20〜30 分かかった)。取りこぼし・化け・rc≠0 が出たら 57600 → 38400 → 9600 と下げ、通った速度を報告に書く。LAN が使えるなら LAN が先 (下の経路の優先度)。
- ノートの os32-v3 が古いと `tools/` の約束が合わない。最初に git pull。
