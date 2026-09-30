---
name: os32-local-ai
description: OS32 のビルド・ホスト試験・配備・実機検証をローカル AI (tools/emu_agent) に実行させる。PM は指示と合否判定だけを行う。手を動かす前にこれを読む。設計判断やコード編集には使わない。
---

# ローカル AI に実行させる (テスト・配備・ビルド)

体制上、**実行はテスター役 (ローカル AI) が行い、PM は判断だけ**する
([`docs/tasks/agents/ROLES.md`](../../../docs/tasks/agents/ROLES.md))。
何をどのビルド・どの配備経路で確かめるかの選択は **`os32-build-verify`** が正典。
本スキルは「選んだものをローカル AI にどう実行させ、結果をどう判定するか」だけを扱う。

## 1. 起動

```bash
python3 tools/emu_agent/agent.py run   "<1 つのタスク>"
python3 tools/emu_agent/agent.py suite tools/emu_agent/tasks/regress.txt   # 配備後の定型回帰 6 本
python3 tools/emu_agent/agent.py tail                                      # 直近セッションのログ
```

モデルは `gemma4-it:e4b` @ `http://127.0.0.1:52625/v1` (FastFlowLM)。
**ユーザーがロード済みのモデル以外に投げない** — `/v1/models` に並ぶことは
ロード済みを意味せず、未ロード名を投げると自動ダウンロードが始まる。
長く無応答なら flm 自体が落ちている。

## 2. ローカル AI に渡せる操作

`tools/emu_agent/agent.py` の `ACTIONS` が唯一の正典。プロンプトの文言では制限できない。

| 種別 | 操作 |
|---|---|
| 実機 | `cmd` `cmd_nowait` `wait` `key` `tvram` `status` `screenshot` `selftest` |
| ホスト | `make <許可 target>` `deploy` (= `os32-cycle deploy`) |

`make` の許可リストは `MAKE_TARGETS` (agent.py)。`check-*` の個別ターゲットも入れてある
(落ちた 1 本だけ回し直すため)。**`build/sdk.mk` に `check-*` を足したら `MAKE_TARGETS`
にも足す** — 2026-09-09 にここが古く、回し直せなかった。

**ini はローカル AI に触らせない。** `ACTIONS` に ini 操作は無く、追加もしない。
NP21/W の設定変更は PM だけの権限 (スキル `os32-emu-config` §0)。

誤操作が実機を壊す経路 (GUI 入力など) は `run` ではなく
`tools/emu_agent/playbook.py` を使う。承認済み JSON 台本と操作・引数・順序が
完全一致したものだけを実行し、既定は dry-run。行動名の許可リストとプロンプトだけでは
`CTRL+STOP` のような意図しないキーを防げない。

## 3. 合否は PM が決める

**`RESULT: ok` はモデルの自己申告**。判定材料は
`tools/emu_agent/logs/<session>/steps.jsonl` の `obs` (実際の観測) と `obs_failures`。

```bash
python3 - <<'PY'
import json,glob,os
for d in sorted(glob.glob('tools/emu_agent/logs/<session-prefix>*')):
    for line in open(os.path.join(d,'steps.jsonl')):
        r=json.loads(line)
        if r.get('obs'): print(os.path.basename(d), r.get('action'), '|', str(r['obs'])[-200:])
PY
```

- `obs` を `error` `fail` などのキーワードで機械判定しない。`kselftest_fail=0` や
  `49 passed, 0 failed` を誤検出する。中身を読む。
- 要求した操作列と `obs` の操作列を突き合わせる。OK 報告でも前提の 1 手が抜けていることがある。
- 画面の状態を古いシェル出力から推測しない。`status` と `tvram` を突き合わせる。
- **`RESULT` が `ok:false, reason:"unparseable"` でも失敗とは限らない。** 長い出力
  (コンパイラ警告など) を `done` の report に貼らせると小型モデルの JSON が壊れ、
  観測は成功なのに ok:false になる。2026-09-09 の `make check` が実例 (obs は `exit=0 (31s)`)。
  ビルド系を投げるときは **`exit=` の行だけを報告させる**。ログの解釈をさせない。

## 4. 配備をさせるときの前提 (PM が先に済ませる)

`deploy` は `os32-cycle deploy` = 停止 → `make deploy-nhd` → 起動 → `ver` →
ゲストの `/boot/vmkernel.lz4` サイズ照合。**`make deploy-nhd` の最終段は
ローカル `build/nhd/os32.nhd` を丸ごと Windows 側へ上書きコピーする** (差分同期ではない)。

したがって配備前に PM が必ず:

1. NP21/W を停止し、プロセス消滅を確認する ([D1]) — `python3 tools/np21w_ctl.py stop` (消えるまで待ち、残れば 1 で終わる)
2. `make nhd-pull` で**ライブイメージを取り込む**。これを飛ばすと古いローカルイメージで
   上書きされ、ゲストが書いた `/home` `/data` `settings.db` が消える
3. `make nhd-umount` してからバックアップを取る ([D2] の承認対象)

その後で `deploy` をローカル AI に投げてよい。**コーダーには配備させない。
エミュレータは同時に 1 オペレータ。**

## 5. カーネル到達の決定打

`ver` の `Build:` はシェルの `__DATE__` なので、カーネルだけ更新すると古い日付のまま。
確実なのは新 `kernel.map` の**新規シンボル**の実コードを照合すること:

```bash
addr=$(i386-elf-nm build/out/kernel.elf | grep ' <新規シンボル>$' | cut -d' ' -f1)
curl -s "http://127.0.0.1:8025/api/mem?addr=0x00$addr&space=phys&len=16"   # ゲスト
python3 -c "print(open('build/out/kernel.bin','rb').read()[0x$addr-0x100000:][:16].hex())"
```

kselftest は `kselftest_pass` / `kselftest_fail` を**新しい `kernel.map` の番地**で読む
(古い番地に旧値が残っていると旧カーネルを新物と誤認する)。

## 6. 読み戻しの検証

- 配備したバイナリの同一性: ゲストで `cp /usr/bin/<app>.bin /host/<未使用の名前>.bin`
  → HostDrv 側で `sha256sum` と `cmp`。**サイズ一致は存在とサイズの証明でしかない**
- GFX バックエンドはゲストの `gfxmode pc98|pegc|cirrus` → `/etc/system.cfg` 読み戻し →
  リセット。**GFX 切替のために再配備しない** (deploy が `system.cfg` を上書きする)。
  実際のバックエンドは要求値ではなく `hal_test` の 1 行目で確認する。ただし
  **`hal_test` は `gfx_init()` を呼ばず起動時に選ばれた値を報告するだけ**なので、
  `gfxmode` の後は**リセットしないと値が変わらない** (2026-09-09 に誤診した)。
  ホスト ini で Cirrus を有効にしてもゲストの `GFX=pc98` は上書きされない (2 層ある)
- ホスト試験・リンク・配備同一性・ゲスト実行・見た目・性能は**別々のゲート**として扱う

## 7. GUI 入力

`tools/gui_gate.py` のヘルパを再利用する。`key(text=...)` は 4 文字 / 0.35 秒刻みで送る
(まとめて送るとゲストの入力リングが取りこぼす)。`/api/mouse` は `ax/ay` の絶対座標。
Cirrus リレー中は `scrn_xmax/ymax` がテキスト画面のままで、実画面は
`wab_width/wab_height` 側 — 400 ライン基準で 480 ラインのクリックを正規化しない。
入力前に `running=1` `user_pause=0` `trap_pause=0` `fault_generation=0` を整数で確認する。
