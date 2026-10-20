---
name: os32-realhw-update
description: 実機 PC-9821Ra266 の OS32 を、HDD 起動のまま新しい版 (カーネル・部品・ユーザーランド) に更新する。ノート (Ubuntu、Remote Control のセッション) に手順を頼み、LAN 版 SerialFS の hsync で boot → 再起動 → 全体 → 再起動の順に入れて ver で確かめる。「実機更新して」「実機に入れて」の依頼で使う。FD 起動・HDD の作り直し・NP21/W への配備は扱わない。
---

# os32-realhw-update — 実機 Ra266 を HDD 起動のまま更新する

PM は実機に直接つながっていない。実機のシリアル (/dev/ttyUSB0) と LAN はノート側にある。
ノートのセッション名は変わるので、毎回 `ListAgents` で `hight-pc-…` の Remote Control の行を探して
`SendMessage` で頼む (返事は来ないことがある。結果はノートの証拠の所在を報告させて読む)。

## 0. 前提の確かめ (頼む前に PM が行う)

1. **承認**: 実機での情報収集とカーネル更新は**許可済み** (ユーザー 2026-10-21)。条件は、同じ成果物で NP21/W を起動し
   **シリアル (rshell) で ver が返ること** — それでカーネルの正常動作は OK とみなす。画面確認の要らない試験は立会い無しで可。
   物理操作 (電源・FD・ケーブル)・画面確認・HDD の上書きや作り直し・配置の世代が変わる更新はユーザーに残す。
2. **入れる版**: push 済みで **CI の build が成功した SHA** を選ぶ (`gh run list`)。文書だけのコミットには
   build が走らないので、その前の build 成功の SHA を使う (コードは同じ)。
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
手順 (運用は 9600 で sync してから、--fast は読み出しだけ):
1. ノートで git pull、`tools/ci_fetch.sh --sha <SHA>` (build がまだなら `gh run watch`)。lanfs_host を止め、
   新しい成果物の hostdrv を root にして起動し直す (peer は今の値)。
2. `sfs run hsync -n boot` (計画だけ確認) → `sfs run hsync boot` → reboot → ver で Commit <SHA>。
3. `sfs run hsync` (全体) → reboot → ver (Commit・Image CRC・services の行が SERVICES_READY)、
   起動ログの部品の PAIR と rc、`mem` の leftover/audit_fail、`db_test`。
4. <画面不要の試験があれば、各コマンドと期待値>
5. 最後に CUI のまま `ver`。
止まる条件: 2 か 3 で shell まで来ない・シリアルが応答しない → それ以上触らずに状態を報告 (物理操作はユーザー)。
手順外の上書き・settings.db の削除・GUI に入って画面を要する操作はしない。
証拠は ~/os32-tmp/evidence/<日付>/<名札>/ (ノート側でよい、所在を報告に書く)、報告は短く (各段の rc、ver、試験の結果、異常)。
```

- LAN 版 SerialFS の使い方 (`sfs lan peer`・`sfs lan on`、ノートの `lanfs_host.py --iface … --root … --peer …`、
  `rshell_serial.py --sfs-lan … cmd "sfs run hsync boot"`) は
  [TASK_LAN_82557](../../../docs/tasks/realhw/TASK_LAN_82557.md) の状態行。LAN が使えないときは
  シリアル版 (`--serve-host <hostdrv>`) で同じ順 ([realhw PLAN](../../../docs/tasks/realhw/PLAN.md) の冒頭)。
- `hsync boot` は `/boot/vmkernel.old` を残す。戻すときは旧カーネルで起動できる (hsync の bootold)。

## 2. 結果の受け取り (PM)

- ver の Commit と Image CRC が依頼した SHA の成果物と一致するか。services が SERVICES_READY か。
- 起動ログの部品の PAIR と rc。`mem` の leftover・audit_fail が 0 か。
- 画面確認の要る項目は実施しないで、台帳 ([DEFERRED_TESTS](../../../docs/tasks/DEFERRED_TESTS.md)) の該当行に
  「画面確認待ち」として残す。
- 票の状態行と証拠の所在を記録する。実機の基準構成が変わっていたら [realhw PLAN](../../../docs/tasks/realhw/PLAN.md) §1 を先に照合。

## 罠

- 文書だけのコミットの SHA を頼むと CI の成果物が無い (`ci_fetch.sh` が見つけられない)。
- 全体同期を先にすると旧 hsync が 1 件も書かずに断る (KAPI が上がった版)。必ず boot が先。
- `--fast` での sync は運用しない (取りこぼしの調査を増やさない)。読み出しだけ速くしてよい。
- ノートの os32-v3 が古いと `tools/` の約束が合わない。最初に git pull。
