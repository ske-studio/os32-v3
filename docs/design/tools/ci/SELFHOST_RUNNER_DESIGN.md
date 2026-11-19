> 出所: ~/os32-tmp/run/ci-runner/DESIGN.final.md (Fable 起草、sol レビュー 3 回、2026-11-19 確定)。
> 状態: **確定 (2026-11-19)** — wip の置き場 (ci-server の機械の準備の参照用)。main に入れる時に file:line を直す。

# CI に自前の runner を足す設計 (r3)

> 状態: 起草 r3 (2026-11-19、往復の最後の回)。sol のレビュー第 2 回 (REVIEW.r2.md) を全部反映。第 1 回の反映とユーザー決定 (U1〜U6)・PM 決定 (`ci-runner` は lxd グループに入れない、像の apt は action 3 本の和) は r2 のまま。前提 (PM から): 書き込みの権限を持つのは所有者 ske-studio 1 人だけで、協力者・bot は今は無い。段 0a の途中の事実 (ci-server): lxd は 5.21.9 LTS (snap、5.21/stable)、ホストに LAN 直結の unmanaged の bridge `br0` が在る、プールは loop mount の形に変えた、`ci-runner` は uid 1002・補助のグループ無しで作成済み。
> 先に結論: **自前の 1 台は CI の全部を引き受けられない** (重いジョブは行列の数で build の checks 6 本 + ppc-qemu 4 本、標準は 1 本 4 コア (10 本が同時に動いた実測は無い)。機械は 16 スレッドで、10 本を寄せれば 1 本あたりが標準を下回る)。向けるのは **build の checks の最長シャード 1 本から**。「2 時間の外れ」を止める本体は自前の runner でなく **手順の時間切れ** (§4) で、これは runner と独立に先に入れる。

## r2→r3 の変更 (指摘ごと。r1→r2 の変更は DESIGN.r2.md の冒頭)

| 指摘 (第 2 回) | 直した所 |
|---|---|
| P1 承認の例外と主体 | §2-1: 信頼する主体を **ske-studio 1 人**に確定。承認の例外になる人 = 書き込みの権限を持つ人 = 信頼する主体、が一致することを登録の条件にし、段 0b で画面と API (協力者・deploy key・App) で確かめる。同じリポジトリの PR の扱いも書いた |
| P1 ホスト・LAN の遮断 | §2-1a を作り直し: 本体は**ホスト側の nftables** (root の物)。ホスト宛ては input の口で全部の番地をまとめて落とし、LAN は経路から作る集合で落とす。順に評価するので許可と遮断の重なりが無い。LXD の ACL は使わない |
| P1 DHCP・DNS・IPv6 | §2-1a: 初回の DHCP の同報・更新、DNS の上流、戻りの通信を規則に明記。LXD の基礎の規則を含む実効の規則の確かめ。試験の一覧 (待受のある宛先と規則のカウンタで見る) |
| P1 project | §2-1b: 利用者用の口が初回に作る project の特定の手順、像・プール・bridge の参照の固定。NIC の上書き・追加の NIC・DevLXD の検査。必須の遮断は `ci-runner` が変えられない所 (ホストの nftables) に置いた |
| P1 振り分け (大文字小文字・主体) | §4-1: 式に `github.actor`・`github.repository` の条件。大文字小文字は `on:` の枝の絞り (区別する) との積で決まることを書いた。段 3 の手本にも同じ条件 |
| P1 復旧 (paths-ignore・本番の式) | §4-1: 式に `github.run_attempt == 1` を足し、**回し直しは必ず標準**に乗る形にした (同じ run・同じ SHA・push のまま。変数にも paths-ignore にも頼らない)。別の枝への push は 2 番目の道。実演は本番の式が入る段 5 (有効にする前) |
| P2 PAT | §2-1: 作った登録の ID の記録、やめる時の順 (登録を消してから失効)、更新の順、漏れを疑う時 |
| P2 像 | §0 E2 を日付 + ハッシュに。§2-2: JIT で起こした時の自動更新を採る版で確かめる |
| P2 資源 | §3: 生成 AI を起こす時の待ちはジョブ全体で 15 分まで、越えたら取り消して回し直す |
| P2 併走 | §5: 段 5b から手本は標準の側だけ。段 7 に取り込み前の照合 |
| P2 変異 | §5: 式の実物の確かめは段 5・6 に移した。`run_attempt`・主体の条件の変異を足した |
| P2 所要 | §6: main の再利用の回と実際に検査した回を分ける。走る時間の比較は段 3 (1 対ずつ、ほかに runner を使う物が無い時)、段 6 は待ちを別の列に |
| P3 | 冒頭の「10 本を同時に回す」を E5 に合わせた |
| 追加の事実 (ci-server、段 0a) | §2-1a: コンテナを `br0` につながない (明記と試験)。§2-1b: project の名前の見込みを `user-1002` に、`br0` を親にする NIC の試験。§3・段 0a: プールは `/mnt/data/ci/pool` への loop mount を source にする形。lxd の版 5.21.9 |
| 反論 2 件 (妥当と認められた) | §6 に残す条件を書いた (タグは本番の式の静的な検査と変異で、G4 は手本が同じ入口を使うことを差分で) |

## 0. 実物との食い違い (5 件)

| # | 依頼・背景の前提 | 実物 | この設計での扱い |
|---|---|---|---|
| E1 | 自前の runner に向ける | `tools/tests/test_ci_split.py:42` が build.yml の全ジョブに `runs-on == 'ubuntu-latest'` を要求し、L367 に「self-hosted に向ける」変異がある (根拠はユーザー決定 2026-11-05「自前の runner substream は GitHub より 2.5〜4 倍遅いので使わない」)。同じ決定が 08_build.md §8-6 (L1133)・PARALLEL_BRANCHES §3 にも書いてある | 新しい決定 (2026-11-19) で上書きする範囲を正典に書く (未決 U1)。試験・変異・文書 2 か所は workflow と同じコミットで直す (R6) |
| E2 | LXD の 24.04 なら CI と同じ | `build-setup/action.yml:44` が `test -n "$ImageVersion"` で止まる。`ImageVersion` は GitHub の runner が入れる環境変数で、素のコンテナには無い (apt のキャッシュの鍵に使う。test_ci_split.py:115・337 が鍵の形を固定) | action は変えず、コンテナの runner の `.env` に `ImageVersion=os32-lxd-<日付>-<ハッシュ 12 桁>` を置く (§2-2)。鍵が GitHub の像と分かれるので apt のキャッシュは混ざらない |
| E3 | `on:` を絞れば外部の PR で動かない | fork からの `pull_request` は **PR 側の workflow ファイル**で走る。こちらの `on:`・`runs-on` をどう書いても、PR が `runs-on: self-hosted` の workflow を足せば自前の runner に届く。check.yml は今 `pull_request:` を無条件で受けている (build.yml には無い) | 守りの本体はリポジトリの設定 (§2-1)。`on:` の絞りは 2 番目の守り |
| E4 | checks (4) は別の回で 8 分、CI の所要は 30〜43 分 | 回を特定できた実測は run 44292264241 の checks (4) = 32:25 (うち `make check` 31:30、impl.log:1735・1744)。同じログの 337 行には全シャードで 8.1〜41.8 分の履歴があり、8 分の回があること自体は記録と合う。ただし **(4) が最長とは回によって限らない** (checks (0) の中央値が 25.5 分) | どのシャードを向けるかは、段 3 で同じ SHA の全シャードを測ってから決める (`[4]` は仮)。前後の比較は同じ SHA の対だけを使う |
| E5 | 時間を食う 6 シャードと QEMU 4 本を自前へ | 6 と 4 は行列の数 (`build.yml:184`・`check.yml:205`) で、10 本が同時に動いた実測ではない (未計測)。標準は 1 本 4 コアで、機械は 8 コア 16 スレッド (空きメモリは ComfyUI 常駐の時 7.5GiB、停止後 28GB — どちらも ci-server の測定)。10 本を 1 台に寄せれば 1 本あたりのコアが標準を下回る。自前の runner は待ち行列が詰まっても GitHub の標準へ自動では流れない | 向けるのは 1 本 (多くて 2 本)。残りは GitHub のまま (混在) |

## 1. 実物の確認: どのジョブを向けるか

| ジョブ | 今の所要 (棚卸し) | 中身 | 道具 | 向ける価値 |
|---|---|---|---|---|
| build / checks ×6 | 16:37〜32:25。最長は (4) で `make check` が 31:30、用意は 38 秒 | 変異込みの全検査 (`make -k check CHECK_SHARD=k/6`) | build-setup (apt 15 個・Rust・i386-elf・フォント 3 種・ccache) | **第一候補は最長の 1 本**。取り込みの待ちは最長のシャードで決まる。8 スレッドを与えれば 4 コアの標準より速い見込み (未計測) |
| check / ppc-qemu ×4 | 12:14〜30:18。QEMU の検査は 12.7〜15.0 分、apt のキャッシュが外れた回は用意だけで 16:17 | `gate.py run --env qemu-ppc --shard k/4` | gate-setup (上に加えて gcc-13・qemu-system-ppc・seabios・powerpc-elf) | 第二候補。道具を焼いた像なら用意の外れ (約 13 分) が消える。ただし check.yml は `pull_request:` を持つ (E3) ので build より後 |
| build / artifacts | 2:20 | `make all`・配布物 | build-setup | 向けない (短い。配布物は標準の環境で作る) |
| check / static-check | 4〜5 分 | 静的ゲート。main でも毎回回す | static-setup・setup-python・lychee | 向けない (短い。文書だけの push の関門) |
| check / ppc-compile・ppc-toolchain-cache | 4〜7 分 / 0.2〜13 分 | PPC の像と host の検査 / キャッシュ作り | gate-setup / ppc-setup | 向けない |

POLICY_CI との関係:

| 決まり | この設計で守る形 |
|---|---|
| G1・R3 (関門を弱めない) | 回る検査の集合は変えない。シャードの分け方 (k/6・k/4)・`make -k check`・変異はそのまま。runner が変わるだけ |
| G5 (用意は 1 か所・どのジョブも同じ環境) | composite action は自前でもそのまま通す (像に先に入れておくのは速くするためで、action が正)。像の中身は action の一覧から機械で作る (§5 段 0a) |
| G7 (同じ入口・YAML に分岐を書かない) | `runs-on` の式は 1 行 (§4-1)。合否の分岐は足さない。切り分けの記録は `tools/ci/` の道具 1 本を呼ぶ |
| G10 (全部に timeout) | `make check` と QEMU の手順に時間切れを足す (§4-2) |
| G12・§3 | 赤の実演を自前の runner の上でも 1 回行う (§6) |
| R1〜R5・R7・R8 | §5 の段。手本は比較の終わりまで残し (R3)、前後の数字と SHA を設計書に書いてから消す (R7・R8) |
| reuse_success.py | 変えない。main の早送りは `wt/**` の成功を再利用して数秒で終わる。この数秒のジョブも自前に向くと行列に並ぶので、**再利用の判定の後に重い手順が来る形のまま**でよいが、行列の待ちは §6 で測る |

## 2. 置き方

### 2-1. 隔離 (公開リポジトリ)

**信頼する物の範囲 (確定)**: 自前の runner が動かしてよいのは、**ske-studio (所有者、書き込みの権限を持つただ 1 人。PM はこの資格で動く) がこのリポジトリに push した `main`・`wt/**` の SHA** だけ。段 5・6 のあいだはさらに `wt/selfhost-**` だけ。fork の PR・タグ・dispatch・回し直しは動かさない。

- 承認の設定の例外 (承認なしで走る人) は書き込みの権限を持つ人なので、**例外の集合 = 書き込みの権限の集合 = {ske-studio}** であることが登録の条件。段 0b で、設定の画面 (Collaborators、Deploy keys、入っている GitHub App の権限)、`gh api repos/ske-studio/os32-v3/collaborators` (1 人・admin)、`gh api repos/ske-studio/os32-v3/keys` (書き込みの鍵が無い)、所有者が個人のアカウントであること (組織なら成員も数える) を確かめて実物を残す。1 つでも合わなければ登録しない。
- 協力者・bot・書き込みの鍵を足す時は、**足す前に** runner を止めて変数を空にし、この設計を見直す (ROLES §6 の一覧に書く)。像を作り直す時 (3 週間ごと) に同じ確かめを繰り返す。
- 同じリポジトリの枝からの PR は ske-studio の push でしか作れないので信頼の範囲の内。こちらの式では `pull_request` は必ず標準に乗る。

| 層 | 設定 | 何を防ぐか |
|---|---|---|
| リポジトリの設定 (本体。**runner の登録より前に入れる**) | Settings → Actions → General → fork の PR の workflow を **Require approval for all external contributors** に。**書き込み権のある人は誰も承認のボタンを押さない** (ROLES §6 の一覧と os32-ci-read に書く。PM は承認待ちの run を見たら触らずにユーザーへ知らせる) | fork の PR の workflow が承認なしで走ること (E3) |
| workflow の式 (§4-1) | 自前に向くのは `push`・決めた ref・決めたシャードだけ。`pull_request`・タグ・dispatch は式の上で必ず `ubuntu-latest` | こちらの workflow が信頼の範囲の外で自前に乗ること |
| ラベル | `[self-hosted, linux, x64, os32-ci]`。絞り込みであって守りではない (リポジトリ単位の登録には枝・イベントの制限が無い) | 取り違えだけ |
| 使い捨て | 1 ジョブごとに新しいコンテナ (project の中の像から `lxc launch --ephemeral`)。登録は **JIT の API だけ** (`POST /repos/{owner}/{repo}/actions/runners/generate-jitconfig` → コンテナの中で `run.sh --jitconfig <値>`)。`config.sh` は使わない。1 ジョブ受けたら登録が消える | 前のジョブの残り物・居座り |
| 資格 | PAT (fine-grained、このリポジトリだけ、Administration の読み書き、期限 90 日) はホストの `/etc/os32-ci/pat` (0400、`ci-runner` だけ) に置く。コンテナ・unit のログ・コマンド行には出さない (curl は `-H @ファイル`、`set -x` を使わない)。コンテナに入るのは JIT の値 1 つ (標準入力で渡す)。unit は JIT を作るたびに返ってきた登録の ID・名前・時刻を `/var/lib/os32-ci/runners.log` に書く。**更新は 75 日目**: 新しい PAT を置いて登録の一覧が読めるのを確かめてから、古い物を失効 (unit が期限の 15 日前から起動のたびに知らせる)。**やめる時の順**: unit を止める → 記録の ID を API で消す → 一覧に `os32-ci-` が無いのを確かめる → 最後に PAT を失効。漏れを疑う時だけは先に失効し、登録は所有者が設定の画面で消す。ジョブの token は今の `permissions` のまま、Secrets は増やさない | 資格の持ち出し |
| 落ちた時の登録 | unit は起動のたびに、名前が `os32-ci-` で始まる offline の登録を API で消してから新しい JIT を作る (ジョブを受ける前に落ちた JIT の登録は自分では消えない) | 幽霊の runner が行列を抱える |
| ファイル・装置 | 非特権コンテナ。デバイスはプールの root と NIC 1 つ。`security.nesting=false`・`security.devlxd=false` (コンテナの中に LXD の口を出さない)。GPU・unix の口・ホストの disk は付けない。unit が付けようとしても project の制限で通らない (§2-1b)。網の遮断は NIC の設定に頼らない (§2-1a) | 生成 AI のファイル (`/mnt/data/hf`・`models`)・`/home` を読む・書く |
| 利用者 | ホストの `ci-runner` (ログイン不可、sudo 無し、鍵無し、**lxd グループに入れない**)。LXD を触る権限は §2-1b | hight の資格の流用・ホストの root |

残る危険 (承知して進める): 承認の設定は「承認された後の実行」と「書き込み権のある人の push」を止めない。前者は決まり (誰も承認しない)、後者は信頼の範囲そのもの。外部の PR を受ける運用を始める時は、先に VM (案 B) へ替える。

### 2-1a. 網 (確定: 本体はホスト側の nftables)

必須の遮断は、root が持つホストの nftables の表 `inet os32_ci` に置く。鍵は**入ってくる口の名前 `lxdbr-ci`** だけで、コンテナの番地・NIC の設定・LXD の ACL に頼らない (`ci-runner` もジョブも変えられない)。別の表の accept は drop を覆さないので、LXD が自分の表に入れる基礎の規則 (DHCP・DNS・転送の許可) があっても、実効は「両方が通した物」になる。規則は上から順に評価する (許可と遮断の重なりは順で決まる)。LXD の bridge の ACL は使わない (境目にしか効かず、規則の勝ち負けに頼ることになるため)。

| 鎖 (どれも `iifname "lxdbr-ci"` の物だけ。それ以外は素通し) | 規則 (この順) |
|---|---|
| input (ホスト自身に宛てた物。**ホストの全部の口の全部の番地**が対象になるので、番地を並べない) | 1. IPv6 は全部 drop (近隣探索も。カウンタ付き)。2. 確立済み・関連の戻りは accept。3. DHCP: udp 68→67 を accept (宛先を見ない — 初回の `0.0.0.0→255.255.255.255` の同報と、更新の bridge の番地宛ての両方)。4. DNS: bridge の番地宛ての udp/tcp 53 を accept。5. 残りは全部 drop (カウンタ。ホストの ssh・ComfyUI・LXD の 8443・LAN の側の番地の 80/443 もここ) |
| forward (コンテナから外へ) | 1. IPv6 は全部 drop。2. 確立済み・関連は accept。3. 宛先が集合 `ci_local4` に入れば drop (カウンタ)。4. tcp 80・443 を accept。5. 残りは全部 drop (外の DNS へ直に出る 53 もここ) |
| forward (外・LAN・ホストのほかの bridge から `oifname "lxdbr-ci"` へ) | 確立済み・関連だけ accept、新しい接続は drop |

- 集合 `ci_local4` は **実物から作る**: `ip -4 route show table all` の default 以外の全部の宛先 (LAN が公開の番地でも、VPN・ほかの bridge でも入る)、`ip -4 addr` の全部の番地、それに固定で `10.0.0.0/8`・`172.16.0.0/12`・`192.168.0.0/16`・`169.254.0.0/16`・`100.64.0.0/10`・`224.0.0.0/4`・`255.255.255.255`。bridge の帯は LAN と重ならない物 (案 `198.18.77.0/24`) にする。
- DNS の上流: コンテナは bridge の番地の dnsmasq にだけ聞く。上流へはホストの dnsmasq がホストから出す (この表の対象外)。NAT (`ipv4.nat=true`) は LXD の物をそのまま使う。bridge は `ipv6.address=none`。
- 載せ方: root の oneshot の unit `os32-ci-net.service` が、集合を作り直して表を入れ、入った規則を読み返して確かめる。runner の unit は `Requires=`・`After=` でこれに従い、表が無い・経路が集合と合わない時は起きない。LXD の再起動・snap の更新・ホストの再起動の後も、runner より先にこの unit が走る。経路が変わった時 (口の追加・VPN) は runner の次の起動で作り直す。
- **コンテナを `br0` につながない**: ホストには LAN に直結した unmanaged の bridge `br0` が在る。コンテナの NIC は `lxdbr-ci` だけで、`br0` を親にする NIC (`nictype=bridged parent=br0`・macvlan・physical) は project の `restricted.devices.nic=managed` と `restricted.networks.access=lxdbr-ci` で断る (§2-1b の試験)。`br0` の帯は経路から `ci_local4` に入るので、`lxdbr-ci` から `br0` の側への転送も forward の 3 で落ちる。`br0` に直に乗ったコンテナはこの表の鍵 (`iifname "lxdbr-ci"`) の外になるので、runner の unit は起こしたコンテナの NIC が `lxdbr-ci` の 1 つだけであることを `lxc config show --expanded` で確かめてから JIT を渡す。
- 補助 (本体ではない): profile の NIC に `security.port_isolation`・`mac_filtering`・`ipv4_filtering`・`ipv6_filtering`。`ci-runner` が上書きできても、上の表は口の名前で効くので遮断は変わらない。

試験の一覧 (段 0b。**遮断は「待受のある宛先に届かない」と「その規則のカウンタが増えた」の両方**で見る。1 つでも外れたら登録しない):

| # | 何を | どうやって | 期待 |
|---|---|---|---|
| N1 | 実効の規則 | `sudo nft list ruleset` の全部 (自分の表と LXD の表、鎖の優先度)、`lxc network show lxdbr-ci` | 上の規則。LXD の基礎の規則と並んでいて、drop が先に効く並び |
| N2 | DHCP の新規と更新 | コンテナを起こす (新規)。中で貸し出しを手放して取り直す・更新を促す | 両方とも番地が取れる |
| N3 | DNS | bridge の番地へ udp と tcp (`dig`・`dig +tcp`)。外の解決器 (例 `1.1.1.1`) へ直に udp/tcp 53 | 前者は引ける。後者は届かず、forward の 5 のカウンタが増える |
| N4 | ホスト | ホストで仮の待受 (80・443・22 と ComfyUI のポート) を全部の番地に立て、コンテナから bridge の番地・LAN の側の番地・ほかの口の番地へ | 全部届かない。input の 5 のカウンタが増える |
| N5 | LAN | 80/443 の待受がある LAN の機械 (ルータの画面・pc98emu・実機ホスト。`br0` の先) と `br0` の番地へ tcp、ping | 全部届かない。forward の 3 のカウンタが増える |
| N6 | IPv6 | bridge の link-local・ホストの大域の番地・外の v6 の番地へ ping と tcp 443。`ip -6 addr` | 大域の番地が無い。届かない。IPv6 のカウンタが増える |
| N7 | 要る物 | `apt-get update` (80)、`curl https://api.github.com` (443)、actions/cache の取り出し | 通る |
| N8 | 入ってくる向き | LAN の機械・ホストのほかの bridge からコンテナの番地へ新しい接続 | 届かない |
| N9 | 載り直し | `snap restart lxd` の後、ホストの再起動の後に N1 と N4 をもう 1 回 | 同じ |
| N10 | ファイル・装置・資源 | `lxc config show --expanded`、コンテナの中で `mount`・`ls /dev/nvidia* /dev/dri /dev/lxd`・`nproc`・memory.max・swap.max | デバイスは root と NIC だけ。ホストの場所・GPU・`/dev/lxd` が無い。§3 の値 |
| N11 | 終わった後の残り | ジョブの正常な終わり・runner の `kill -9`・`lxc stop --force`・ホストの再起動のそれぞれの後に `lxc list` と登録の一覧 | 使い捨てのコンテナが 0。`os32-ci-` の登録は待っている 1 つだけ |

### 2-1b. unit を誰の権限で動かすか (確定: 制限付きの project)

ホストの lxd グループは root 相当。`ci-runner` はそこに入れず、**LXD の制限付きの project 1 つだけ**を触らせる。守る相手はジョブ (コンテナの中。LXD には触れない) で、project の制限は `ci-runner` の手順が乗っ取られた時の 2 番目の守り。**網の遮断は project の外 (§2-1a、root の物) にあり、`ci-runner` には変えられない**。

project の特定と参照の固定 (段 0a):

1. グループ `lxd-ci` を作り `sudo snap set lxd daemon.user.group=lxd-ci`。`ci-runner` はこのグループだけ。
2. `ci-runner` の権限で `lxc project list` を 1 回呼ぶ。利用者用の口が初回に project を作るので、その名前 (見込みは `user-1002`。`ci-runner` の uid は 1002) と、作られた時の `lxc project show`・既定の profile・一緒に作られた網があればその名前を**実物で記録**する。
3. 管理者が、その project に下の表の値を入れ直す (初回の既定に頼らない)。自動で作られた網が `lxdbr-ci` でなければ project から外して消す。
4. 参照は 4 つの名前に固定し、unit のスクリプトは名前を引数に取らない: project = 2 で記録した名前、プール = `ci`、網 = `lxdbr-ci`、像 = project の中の別名 `os32-ci-base` (project は `features.images=true`・`features.profiles=true`。像は管理者が `lxc image copy … --target-project` で入れ、別名を付け替える)。project の `default` の profile は管理者が「root はプール `ci`、NIC は `lxdbr-ci`」だけにする。
5. unit は system の unit で `User=ci-runner`。行うのは `lxc launch os32-ci-base --ephemeral`・`lxc exec` (JIT の値を渡す)・`lxc delete` と GitHub の API だけ。

project の制限 (管理者が設定し、`lxc project show` の実物で報告):

| 鍵 | 値 | 止める物 |
|---|---|---|
| `restricted` | `true` | 下の制限を有効にする |
| `restricted.containers.privilege` | `unprivileged` | 特権コンテナ |
| `restricted.containers.nesting`・`restricted.containers.lowlevel` | `block` | 入れ子・`raw.*` の設定 |
| `restricted.devices.disk` (と `restricted.devices.disk.paths` は空) | `managed` | ホストの disk の付与 (`/mnt/data/hf`・`models`・`/home`) |
| `restricted.devices.gpu`・`unix-char`・`unix-block`・`unix-hotplug`・`usb`・`pci`・`proxy`・`infiniband` | `block` | GPU・装置・ホストの口の持ち込み (proxy はホストの側で待ち受ける道になる) |
| `restricted.devices.nic` と `restricted.networks.access` | `managed`、`lxdbr-ci` だけ | ほかの網・ホストの NIC。`managed` はつなぐ先を縛るだけで NIC の設定は縛らない — だから遮断を NIC に置かない |
| `restricted.virtual-machines.lowlevel`・`limits.virtual-machines` | `block`・`0` | VM |
| `limits.containers`・`limits.memory`・`limits.disk` | 1・12GiB・プールの内 | 同時の数と資源 (§3) |

確かめ (段 0a の終わり。`ci-runner` の権限で):

| 試すこと | 期待 |
|---|---|
| 特権コンテナを作る / `raw.lxc`・`raw.idmap` を付ける / 入れ子を有効にする | 断られる |
| ホストの `/mnt/data/hf` を disk で付ける / GPU・unix の装置・proxy を付ける | 断られる |
| NIC を `br0` に付ける (`nictype=bridged parent=br0`)。2 つ目の NIC を足す (`lxdbr-ci` 以外・`nictype=physical`・`macvlan`) | 断られる。`br0` が通ってしまう版なら project の形は採らず、下の「成り立たない時」の形 (root の決まったスクリプト) にする |
| 2 つ目の NIC を `lxdbr-ci` に足す / NIC の `security.*` を外す | 通るかもしれない。通っても §2-1a の N4〜N6 が同じ結果になることを、その状態で 1 回確かめる |
| `security.devlxd=true` に戻す | project の鍵では止められない見込み。通る時は、コンテナの中の `/dev/lxd` から読めるのが自分のインスタンスの情報だけで、ホストや project を操作できないことを確かめる (unit は false で起こす) |
| project の設定・`default` の profile・網 `lxdbr-ci`・プールを書き換える / 既定の project やほかの project を見る | 断られる |
| 2 つ目のコンテナを起こす | 断られる (`limits.containers`) |
| `id ci-runner`、`sudo -l -U ci-runner`、ホストで `nft` を呼ぶ | lxd グループ無し・sudo 無し・nft は権限なし |

成り立たない時 (入った lxd の版で、利用者用の口の project に上の制限を掛けられない・unit から使えない): **root の system の unit が決まった 1 本のスクリプトで LXD を触り**、PAT を読んで API を呼ぶ部分だけを `ci-runner` に落とす。`ci-runner` はどのグループにも入れない。§2-1a はそのまま効く。どちらになったかは ci-server が実物を付けて報告し、PM が設計書に書く (ユーザー判断は要らない: どちらも lxd グループの利用者を増やさない)。

### 2-2. OS の差の吸収 (第一案: LXD のコンテナ)

| 案 | 評価 |
|---|---|
| **A. LXD の Ubuntu 24.04 コンテナ (推奨)** | `ubuntu-latest` は今 24.04。apt の版 (gcc-13・clang・qemu-system-ppc) が CI と揃う。docker 無しで済む。使い捨ては像からの `launch --ephemeral` で足りる |
| B. LXD の VM | 隔離は強いが、メモリを先取りする。A で網とファイルを絞れば足りる。外部の PR を承認する運用に変える時はこちらへ |
| C. ホスト (26.04) に直に runner | gcc・clang・qemu の版が CI とずれ、検査の結果が runner で変わる。生成 AI のファイルが同じ利用者空間に見える。採らない |

像に先に入れる物 (action が毎回確かめるので、入っていれば速く通るだけ):

- apt は **build-setup (15 個)・gate-setup (4 個: gcc-13・qemu-system-ppc・qemu-system-data・seabios)・gate-setup が呼ぶ ppc-setup (キャッシュが外れた時に入れる 7 個。今は全部 build-setup の 15 個に含まれる) の 3 本の和**。3 本の action.yml から機械で抜き出す (ppc-setup の一覧が先に増えても漏れない)。ほかに `git`・`curl`・`xz-utils`・`sudo`・`python3-pip`・`gh` (reuse_success.py が `gh api` を呼ぶ)。
- `rustup` (action は rustup がある前提で toolchain だけ入れる)、`requirements.txt` の pip。24.04 の素の像は pip が PEP 668 で止まるはずなので、GitHub の像がどう通しているかを段 0 で確かめて同じにする (未確認)。
- runner の利用者に、コンテナの中だけのパスワード無し sudo (action が `sudo apt-get` を呼ぶ)。
- `~/opt/cross`・`~/opt/cross-ppc`・フォント・ccache は **焼かない**。actions/cache の復元に任せる (鍵と中身の対応を崩さない)。復元が遅ければ段 3 の数字を見て考える。
- runner の `.env` に `ImageVersion=os32-lxd-<日付>-<ハッシュ 12 桁>` (E2)。ハッシュは像を作る手順のファイルと `dpkg-query -W` の出力の sha256 (同じ日に作り直しても中身が違えば変わる)。runner のプロセスからジョブへ渡ることを、段 3 の手本の 1 行目で `echo "$ImageVersion"` して確かめる。
- actions/runner は版と sha256 を固定して入れる。**JIT で起こした runner が自分を更新しようとするかは、採る版で段 0b に確かめる** (使い捨ての 1 回を回し、runner の診断のログに更新の跡が無いこと。JIT の API に更新を切る口が無く更新が走るなら、毎ジョブ更新の時間が乗るので、runner の新しい版が出たら 1 週間以内に像を作り直す運用にし、unit が GitHub の公開の版の一覧と像の版を毎日比べて知らせる)。GitHub は古い版 (新しい版が出て 30 日を過ぎた物) にジョブを渡さなくなるので、**像は 3 週間ごとと、action.yml の apt の一覧・Rust の版が変わった時に作り直す**。unit は像の日付が 21 日を越えたら起動のたびに知らせる。作り直した像は段 3 の手本を 1 回通してから差し替える。
- 素の 24.04 で同じ action が通ることは未確認。段 3 で、キャッシュが全部当たる回と全部外れる回 (鍵が新しい `ImageVersion` なので初回は apt が必ず外れる。クロス 2 本は鍵を変えた試しの枝で外す) の両方で、apt・pip・Rust・フォント 3 種・`gh` の手順が通り、同じ checks が通ることを確かめる。

## 3. 資源の割り当て

**段 3〜7 は同時 1 本だけ** (ユーザー決定 U4)。2 本は受入の後の別の判断 (末尾)。空きメモリは ComfyUI を落とした後の 28GB (ci-server の測定) を前提にし、常駐のまま 7.5GiB の時は runner を起こさない。

| 資源 | 値 | 理由 |
|---|---|---|
| 同時ジョブ数 | 1 (コンテナ 1 つ・JIT の登録 1 つ。project の `limits.containers=1` でも縛る) | 上のとおり |
| CPU | `limits.cpu` に **番号の並び**を書く (数だけの `8` は固定にならず、LXD が割り振る)。物理 4 コアとその SMT の相方の 8 スレッド。組は `/sys/devices/system/cpu/cpu*/topology/thread_siblings_list` を読んで決める (3700X の見込みは `4-7,12-15` だが実測の値を使う)。`limits.cpu.priority` を低く | 残りの物理 4 コアはホストと生成 AI |
| メモリ | `limits.memory=12GiB` (hard)、`limits.memory.swap=false` | 標準の runner (16GB) に近づけ、メモリが理由の差を減らす。越えたらジョブが OOM で赤になる (ホストを巻き込まない)。swap 31GiB に逃がすと「ゆっくり進み続ける」外れを自分で作る。ホストの余りは 28 − 12 = 16GB。段 3 でジョブの山 (cgroup の memory.peak) を記録する |
| ディスク | 40GiB の btrfs の像 `/mnt/data/ci/lxd-ci.img` をホストが `/mnt/data/ci/pool` に loop mount し、その mount 先を LXD のプール `ci` の source にする (lxd 5.21 の btrfs のプールは任意の場所の loop file を source に取れないため。`snap.lxd.daemon` に `RequiresMountsFor` の drop-in)。上限は像の大きさで効く。`/` には置かない。mount が無いまま LXD が起きると `/mnt/data` の本体に書くので、runner の unit は起動のたびに `findmnt /mnt/data/ci/pool` を確かめる | 1 ジョブの山は 10GiB 台の見込み (段 3 で測る) |
| 掃除 | ジョブごとにコンテナごと消える。残るのは像 2 世代。プールの使用が 80% を越えたら unit は次の登録を作らない | 手の掃除を要らなくする |
| 持ち越すキャッシュ | 無し (GitHub の actions/cache だけ) | ジョブをまたぐホスト側のキャッシュは、汚れが次のジョブに移る道になる |

生成 AI を起こす時 (U4 の決定の形):

1. ユーザーが PM に一言伝える。PM は **先に** `gh variable set OS32_SELF_SHARDS --body '[]'` (新しい run は全部標準へ)。
2. 自前で動いているジョブは **15 分まで待つ** (60 分は検査の手順だけの上限で、用意・後始末を入れたジョブ全体は最長 150 分ありうるので、上限には使わない)。15 分で終わらなければ、その run を取り消して回し直す (§4-1。回し直しは標準に乗る)。自前で待っているだけのジョブは待たずに同じ手順。
3. ci-server が unit を止め (`systemctl stop`)、待っている JIT の登録を消す。`lxc list` が空になってから ComfyUI を起こす。
4. 戻す時は逆順 (ComfyUI を落とす → 空きを測る → unit を起こす → 変数を戻す)。

2 本にする条件 (受入の後。今は採らない): 空き 26GiB 以上 (12GiB × 2 + 余り 2GiB)、段 3 の山が 1 本 10GiB 未満、6 スレッド (物理 3 コア、重ねない) でも標準より短い、プールの山が 1 本 15GiB 未満、NIC の `port_isolation` の確かめが済んでいる。3 本以上は採らない (1 本が標準の 4 コアを下回る)。

## 4. 落ちた時・遅い時

### 4-1. 振り分けの式と、GitHub の標準に戻す道 (確定)

式 (build.yml の checks だけ。artifacts は `ubuntu-latest` の固定のまま):

```
runs-on: ${{ fromJSON(github.event_name == 'push' && github.run_attempt == 1 && github.repository == 'ske-studio/os32-v3' && github.actor == 'ske-studio' && startsWith(github.ref, 'refs/heads/wt/selfhost-') && contains(fromJSON(vars.OS32_SELF_SHARDS || '[]'), matrix.shard) && '["self-hosted","linux","x64","os32-ci"]' || '"ubuntu-latest"') }}
```

| 条件 | 自前に向く時 | 外れた時 |
|---|---|---|
| イベント | `push` だけ | `workflow_dispatch` (どの ref でも)・`pull_request` は標準 |
| 回 | 1 回目 (`run_attempt == 1`) だけ | **回し直しは全部標準** (下の戻す道の本体) |
| 主体 | このリポジトリで、push したのが `ske-studio` | ほかの主体 (協力者・bot を足した時を含む) は標準。fork に写された workflow も標準 |
| ref | 段 5・6: `refs/heads/wt/selfhost-` で始まる枝。段 7 で `refs/heads/main` と `refs/heads/wt/` に広げる (別のコミット) | タグ (`refs/tags/v*`、build.yml:19)・ほかの枝は標準 |
| シャード | 変数 `OS32_SELF_SHARDS` (JSON の配列) に入っている番号 | 変数が無い・空なら全部標準。JSON として壊れていればジョブが式の誤りで赤になる |

大文字小文字: 式の比較 (`==`・`startsWith`) は区別しない。枝の名前の区別は `on: push: branches` の絞り (`"wt/**"`・手本は `"wt/selfhost-**"`、区別する) が先に行うので、実際に自前に向くのは「`on:` を通った ref」と式の積 — `WT/…` の枝はそもそも起動しない。主体が ske-studio 1 人のあいだは `Ske-Studio` の別人も居ない。大文字小文字だけが違う枝を作らないことを PM の決まりに書く。

戻す道 (自前が落ちている・遅い・生成 AI を起こす時):

1. **本体: 取り消して回し直す。** `gh run cancel <id>` → `gh run rerun <id>` (落ちた分だけなら `--failed`)。回し直しは `run_attempt` が 2 以上なので、変数の値に関係なく全部のシャードが標準に乗る。**同じ run・同じ SHA・同じ push のイベント**のままなので、取り込みの関門 (その SHA の check・build の success) も reuse_success.py の条件 (`wt/` の push の success) もそのまま満たし、`paths-ignore` の評価もやり直されない。
2. あわせて `gh variable set OS32_SELF_SHARDS --body '[]'` (次の push が自前に向かないように。runner が直るまで)。
3. 2 番目の道 (1 が期待どおりに動かなかった時): 変数を空にした上で、同じ SHA を別の枝の名前で push する (`git push origin <SHA>:refs/heads/wt/std-<名>`)。新しい枝の push は `paths-ignore` の比べ方が元の push と違い、**build が起動しないことがある**。3 分待って build の run が無ければこの道は使えない。
4. 1 も 3 も使えない時: runner を直して元の run を回し直すか、式を戻す revert を main に入れる。revert の後は SHA が変わるので、その枝は取り込み直しの扱い (新しい SHA で check・build を通す) になる — 同じ SHA の関門は満たせないことを承知の上の最後の手。
5. 同じ SHA の dispatch は使わない (PARALLEL_BRANCHES §3: dispatch の成功は再利用の証拠にならない)。

- 1 と 3 の実演は、本番の式が入る**段 5 (変数を入れる前)** に行う: 式の入った試しの枝で、変数を試しの値にし、unit を止めたまま push → 対象のシャードが待つ → 取り消し → 回し直し → 全部標準で success、を run の実物 (各ジョブの runner の名前) で残す。回し直しが式を評価し直さなかった時は、段 6 に進まず設計に戻る。
- **自動では戻らない**: runner が落ちていても GitHub はジョブを待たせ続け、`timeout-minutes` は待ちを数えない。**10 分は PM が手を入れる目標**であって仕組みではない — os32-ci-read の見張りに「自前のラベルのジョブが queued のまま 10 分」の知らせを足す。
- runner 側: unit は落ちたコンテナを作り直す。3 回続けて失敗したら止まり、記録の ID の登録を消して知らせる。

### 4-2. 手順の時間切れ (自前の runner と独立。先に入れる)

| 手順 | 今 | 案 | 理由 |
|---|---|---|---|
| build / checks の `make check` | 無し (ジョブの 150 分だけ) | `timeout-minutes: 60` | 正常の最長は 31:30。約 2 倍。外れの回 (2 時間以上) は 60 分で赤になり、落ちたシャードだけ回し直せる。45 分だと再配分の途中の偏り (最長÷最短 2.76) で正常な回を切る恐れ |
| check / ppc-qemu の QEMU の検査 | 無し (ジョブの 90 分) | `timeout-minutes: 40` | 正常は 12.7〜15.0 分。約 2.5 倍 (TCG は機械の速さに敏感) |
| ジョブの 150 分・用意の 90 分 | そのまま | 変えない | クロスを一から作る回 (最長 60 分) の余裕。test_ci_split.py が `== 150`・`>= 90` を固定 |

どちらも **標準の環境で先に入れる** (段 1・2)。値は正常な回の実測で確かめる (入れた後の 5 回で、正常の最長が上限の 6 割を越えるなら値を見直す)。切れた回は「検査の失敗」と区別が付くように切り分けの記録 (下) を見て回し直す。3 回続けて切れたら値でなく原因を見る。

### 4-3. 遅い回の切り分けの記録

- `tools/ci/runner_probe.py` (新規・純関数に分けて host の試験と変異を付ける、G9) を、重い手順の前と後 (`if: always()`) に 1 行で呼ぶ。
- 出す物 (ジョブの要約に 6 行ほど): runner の名前と種類、`nproc`、`/proc/loadavg`、`/proc/pressure/cpu`・`io`・`memory` の `some avg60`、`MemAvailable`、作業ディスクの空き、決まった計算 1 秒分の所要 (CPU の速さの物差し)、steal (`/proc/stat`)。
- 読み方: 物差しが普段の 2 倍以上・steal が高い → runner の外れ。io の pressure が高い → ディスク。どちらも普通で遅い → 検査の側。

## 5. 段 (1 段ずつ。各コミットは main の CI を 1 回通してから次へ、R5)

| 段 | 何をするか | 触る物 | 確かめ・見る数字 |
|---|---|---|---|
| 0a | 機械の準備のうち GitHub に関わらない物 (**先に行う**、下) | 機械だけ | 像の大きさ・作る時間・SMT の組・空きメモリ・§2-1b の確かめ |
| 1 | 時間切れ 60 分を build の checks の `make check` に (標準の環境のまま) | build.yml の 1 ジョブ・test_ci_split.py | 正常な回が通る。試しの枝 (その枝だけ上限を 1 分にした物、main に入れない) で、手順が切れてジョブが赤になり、`if: always()` の計時の成果物が上がり、ほかのシャードが最後まで回ること |
| 2 | 時間切れ 40 分を check の ppc-qemu の QEMU の手順に (標準のまま) | check.yml の 1 ジョブ・ppc_ci.py | 同上 (切れた時に失敗のログの成果物と診断の手順が動く) |
| 0b | 登録と網 (この設計の確定の後、下) | 機械・リポジトリの設定 | §2-1 の主体の確かめ・§2-1a の N1〜N11・§6 の「外部の PR」(a)(b) |
| 3 | **手本 (R2)**: 枝専用の workflow `selfhost-probe.yml` (`on: push: branches: ["wt/selfhost-**"]` だけ)。同じ SHA の checks の 1 シャードを自前と標準に並べる。自前の側のジョブは `if:` に §4-1 と**同じ信頼の条件** (push・このリポジトリ・actor が ske-studio・1 回目) を持つ。検査の入口は build.yml と同じ 1 行 (`make -k check CHECK_SHARD=k/6`)。全 6 シャードを 1 回ずつ (どれを向けるかを決める)、決めたシャードを 5 回、キャッシュが当たる回・外れる回の両方。**1 対ずつ、ほかに runner を使う物が無い時に** | 新しい workflow 1 本・比較の道具 `tools/ci/runner_compare.py` と試験・検査の登録 | 走る時間の対・メモリとディスクの山・比較の道具の結果 (§6)。自前でだけ出る赤は次の段の前に直す (R4) |
| 4 | 切り分けの記録 (§4-3) を build の checks に | build.yml・`tools/ci/runner_probe.py` と試験・検査の登録 | 要約に出ること |
| 5 | build の checks の `runs-on` を §4-1 の式に (変数は空 = 全部標準)。artifacts は固定のまま。main に入った後、**変数を入れる前に**戻す道の実演 (§4-1) と式の実物の表 (下) | build.yml・test_ci_split.py・08_build §8-6 (L1133)・PARALLEL_BRANCHES §3・TESTS の一覧 | 下の「試験の直し」。変数が空の回で検査の集合が前と同じ (R3)。戻す道にかかった分数 |
| 5b | 手本から自前の側のジョブを外し、**標準の側だけ**にする (これ以降、自前の runner を使うのは build.yml の本番の式だけ — 同じ SHA で 1 本の runner を取り合わない) | 手本の workflow | 手本が標準で 1 本だけ回ること |
| 6 | 変数を `[決めたシャード]` にして、`wt/selfhost-**` の枝で 1 週間・10 回以上。同じ SHA で、本番 (自前のシャード) と手本 (同じシャードを標準で) が並ぶ (R3 の並べる期間。この間、main と `wt/**` の関門は全部標準のまま) | 変数だけ | §6 の受入。重なる push の実演。G12 の実演 (自前の上で 1 本わざと落とす)。式の実物の表の残り |
| 7 | 受入が通ったら、式の ref を `main` と `wt/` に広げ、手本の起動も `wt/**` に広げる (ここで初めて関門の一部が自前に乗る)。**最初の 5 回の取り込みは、早送りの前に照合する**: その SHA の build (自前のシャードを含む)・check・手本 (標準) の 3 つが success で、`runner_compare.py <SHA>` が一致を返した時だけ main へ早送り。1 つでも合わなければ取り込まず、その SHA を回し直して (標準に乗る) 原因を見る | build.yml・test_ci_split.py・手本の起動の枝・os32-land の手順に照合の 1 行 (5 回のあいだ) | 5 回分の SHA・3 つの結論・照合の結果 |
| 8 | 前後の数字と実行の SHA を設計書 (`docs/design/tools/ci/` に 1 枚) に書いてから、手本の workflow と os32-land の照合の行を消す (R7・R8) | workflow 1 本の削除・設計書 | — |
| 9 | (別の判断) ppc-qemu を自前へ。式は同じ条件で、check.yml は `pull_request` を持つのでイベントの条件が効くことを同じリポジトリの枝の PR で確かめる | check.yml・ppc_ci.py | 同上 |

試験の直し (段 5・7。R6):

- test_ci_split.py:42 を、artifacts は `== 'ubuntu-latest'`、checks は式の文字列と一致、に分ける。
- 変異 (どれも赤になること): (1) artifacts を自前のラベルに、(2) イベントの条件を外す、(3) ref の条件を外す・`refs/heads/` や `refs/` に広げる (タグが入る)、(4) `run_attempt == 1` を外す、(5) actor・repository の条件を外す、(6) 変数が無い時の既定 `'[]'` を `'[0,1,2,3,4,5]'` に、(7) ラベルを `substream` に (今の L367 を式の形で残す)。
- **式の実物の表は段 5・6 で** (式が入る前の段 3 では作れない): 試しの枝の push で対象のシャード → 自前 / 対象でないシャード → 標準 / ほかの `wt/` の枝 → 標準 / 試しの枝の dispatch → 標準 / 変数が空 → 標準 / 回し直し → 標準。各ジョブの runner の名前を run の実物から写す。タグは実物では回さない (反論 1。静的な検査と変異 (3) で確かめる)。
- 出す前に `python3 -B tools/checks/run.py --select --files .github/workflows/build.yml tools/tests/test_ci_split.py <足した道具>` が挙げる検査と `check-map` を手元で変異込みで回す。

段 0a の手順 — GitHub に関わらない準備 (ci-server が先に行う。sudo の所はユーザーの承認済み = U6):

- `sudo snap install lxd --channel=5.21/stable` を 1 回 (済み: 5.21.9 LTS)。snap の自動更新は時間帯を決めて留める。
- `sudo lxd init` は preseed で (既定のプールを作らない)。40GiB の btrfs の像 `/mnt/data/ci/lxd-ci.img` を `/mnt/data/ci/pool` に loop mount (fstab か mount の unit、`RequiresMountsFor` の drop-in) し、mount 先を source にプール `ci` (済み。`hf`・`models`・`a` には触らない)。再起動の後に mount → LXD の順で起きることを 1 回確かめる。
- 利用者 `ci-runner` (uid 1002、ログイン不可、sudo 無し、補助のグループ無しで作成済み)。足すグループは `lxd-ci` だけ。§2-1b の 1〜4 (グループ `lxd-ci`、project の特定、制限、参照の固定)。
- 管理者の側で `ubuntu:24.04` から元のコンテナを作り、§2-2 の物を入れる。apt の一覧は action.yml 3 本 (build-setup・gate-setup・ppc-setup) から抜き出す (手で写さない)。actions/runner は版と sha256 を固定して展開するだけ (登録しない)。この段のコンテナは信頼できる物しか動かさないので、網は像を作るあいだだけの仮の bridge でよい (終わったら外す)。
- 元のコンテナを像にして project に入れ、別名 `os32-ci-base` を付ける。
- §2-1b の確かめの表 (網に関わる行は 0b で)。SMT の組・停止後の空きメモリ・像の大きさ・像から起こすまでの秒を測って報告。

段 0b の手順 — 登録と網 (この設計の確定の後。この順):

1. 主体の確かめ (§2-1: 協力者・鍵・App・所有者の種類) を実物で残す。
2. リポジトリの設定を「全部の外部の人に承認が要る」に変え、画面の値を記録 (ユーザーか PM)。
3. bridge `lxdbr-ci` と、ホストの nftables の表・`os32-ci-net.service` (§2-1a)。project の `restricted.networks.access`。使い捨てのコンテナを 1 つ起こして N1〜N10 と、§2-1b の網に関わる行 (NIC を足す・`security.*` を外した状態で N4〜N6) を通す。まだ登録しない。
4. §6 の (b): runner を登録していない状態で、fork からの試しの PR を出し、承認待ちで止まるのを見て閉じる。
5. PAT を作り `/etc/os32-ci/pat` に置く (ユーザー。値は画面にもログにも出さない)。
6. unit を入れる: `os32-ci-net.service` を確かめる → 記録にある offline の登録を消す → 像から使い捨てのコンテナ → JIT の値を作って ID を記録し、標準入力で渡す → `run.sh --jitconfig` → 終わったらコンテナを消す → 繰り返し。
7. N11 (終わった後の残りの 4 通り) と、JIT の runner が自分を更新しないことの確かめ (§2-2)。止め方 (unit の停止・登録の消し方・PAT の失効の順) を 5 行で残す。

## 6. 受入

分数の目標 (checks の 1 本が 20 分以下など) は目安で、基準は同じ SHA の対の比較。

| 項目 | 基準 | 確かめ方 |
|---|---|---|
| 所要 | 列を分けて書く: (i) **走る時間** — 段 3 の同じ SHA・同じシャードの対 (1 対ずつ、ほかに runner を使う物が無い時。キャッシュが当たる回と外れる回を別に) で、自前の中央値が標準より短く、5 分以上遅い対が無い。(ii) **待ちを含めた時間** — 段 6 の本番の回 (10 回以上) の「待ち + 走る時間」が、同じ SHA の手本 (標準) より短い。段 5b 以降の手本は標準だけなので、並べること自体は自前の待ちを生まない。(iii) build 全体 (全シャードの最長) が標準だけの回より遅くならない。**main の回のうち `wt/` の成功を再利用した回 (数秒) は数に入れず**、実際に検査した回だけで比べる。届かなければ U5 | 段 3・6 の表。1 本だけ向けても全体が縮まない回 (別のシャードが最長) は内訳を書く |
| 行列 | 重なる push の実演: 試しの枝 2 本を 1 分以内に push し、後の枝の待ちを測る。後の枝の build 全体が標準だけの回より遅くなるなら、1 本の構成は取り込みが重なる時間帯に向かない — その数字を付けてユーザーに戻す | 段 6 |
| 検査の範囲 | 手本が build.yml と同じ入口・同じ登録・同じシャードの割り方を使うことを差分で確かめた上で (反論 2 の条件)、`runner_compare.py` が同じ SHA の 2 つの回の成果物を読み、(1) 検査の名前の集合が同じ、(2) 終了コードが同じ、(3) 変異の数が検査ごとに同じ、(4) SKIP の行が増えていない、(5) 登録にあって結果の行が無い検査 (回っていない) が無い、を全部満たす。計時の TSV だけで (3)(4) が読めなければ、手本は比較の期間だけ検査ごとのログ (`CHECK_LOG_DIR`) を両方の側から上げる | 段 3・6・7 の全部の対。道具には host の試験と変異 (G9) |
| 道具が無い時 | 道具を 1 つ抜いた像で、検査を始めずに終了コード 2 で止まる (G6) | 段 3 で 1 回 |
| 赤が見える | 自前の上でわざと 1 本落とした回で、残りが回り、表と成果物に出て、ジョブが失敗で終わる (G2・G3・G12)。時間切れの実演 (段 1・2) とは別に行う | 段 6 |
| 振り分け | 本番の式の文字列の検査と変異 7 本が赤になること (タグ `refs/tags/v…` が標準になることはここで確かめる、反論 1 の条件)。式の実物の表 (段 5・6) | 段 5・6 |
| 生成物 | G8・G11 (生成物が同じこと) の対象は配布物を作る artifacts で、標準の固定のまま変えないので、この設計では比較しない | 段 5 の試験 (artifacts の固定) |
| 外部の PR で動かない | (a) 設定の画面の値。(b) **runner を登録する前に**、別のアカウントの fork から、自前のラベルを指す `echo` だけの workflow を足した PR を出し、承認待ちで止まるのを見て、承認せずに閉じる。閉じてから登録する。(c) こちらの workflow が `pull_request` で標準に乗ることは、同じリポジトリの枝からの PR (信頼の範囲) で別に確かめる (段 9 の前。build.yml は `pull_request` を持たないので段 5〜8 では起きない) | 段 0b |
| ホストの権限・網 | §2-1b の確かめの表、§2-1a の N1〜N11、§2-1 の主体の確かめが全部期待どおり | 段 0a・0b |
| 戻せる | §4-1 の本体の道 (取り消し → 回し直し) で、**同じ run・同じ SHA** の標準の success が取れる。2 番目の道 (別の枝) は build が起動した時・しなかった時の両方を記録。かかった分数 | 段 5 (変数を入れる前) と段 6 で 1 回ずつ |
| 生成 AI | §3 の止め方 (15 分まで待つ → 取り消して回し直す) で、ComfyUI を起こす前に `lxc list` が空になる | 段 6 で 1 回 |

## 7. 決定済み (ユーザー決定 2026-11-19、全部 r1 の推奨どおり) と残り

| # | 決まったこと |
|---|---|
| U1 | substream は使わないまま。ci-server の機械だけを混在の形で使う。関門が GitHub の CI の結論であることは変えない (ROLES §6 の一覧に書く) |
| U2 | 使い捨ての登録の PAT をホストの `ci-runner` だけが読める所に置く (このリポジトリだけ・90 日) |
| U3 | fork の PR の承認の設定を変え、試しの PR で確かめる。外部の PR を受ける時は VM に替えてから |
| U4 | 段のあいだは 1 本。生成 AI を起こす時はユーザーが伝え、PM が振り分けを止める (§3) |
| U5 | 受入に届かなければ runner を外し、時間切れと切り分けの記録だけ残す |
| U6 | snap の lxd を入れる。r1 の「lxd グループが root 相当になる点を承知の上で」は PM 決定で改めた: `ci-runner` は lxd グループに入れず、制限付きの project だけ (§2-1b) |

新しい未決は無い。実測で決める値: 向けるシャードの番号・CPU の番号の並び・メモリの上限 (12GiB で足りるか) は段 3、unit の権限の 2 つの形のどちらになるかは段 0a。

## 反論

第 2 回に対する新しい反論は無い (指摘は全部採った)。第 1 回の 2 件は第 2 回で妥当と認められ、付いた条件を §6 に入れた:

- タグ (`v*`) の実物の push はしない — 本番の式の静的な検査と、イベント・ref の条件の変異が赤になることを受入に残す (§6「振り分け」)。
- G4 だけの実行の比較はしない — 手本が同じ登録・同じシャードの入口を使うことを差分で確かめ、R3 の実行の集合の機械の比較は続ける (§6「検査の範囲」)。

確かめていない前提 (実物で外れたら設計に戻る所): 回し直しが `runs-on` の式を評価し直すこと (段 5)、利用者用の口の project に §2-1b の制限を掛けられること (段 0a、代わりの形あり)、LXD の表と自前の表の並びで drop が効くこと (段 0b N1・N4〜N6)、JIT の runner の自動更新 (段 0b)。

## 確定 (2026-11-19、sol の第 3 回の条件付きの承認、往復 3 回で確定)
実装の段の完了条件に写す (満たせない時は自前への振り分けを有効にせず、ユーザーに上げる):
- 段 0b: 承認の免除と書き込みの主体を別々に照合する。採った権限の構成で、経路の変更中 (LAN・VPN の経路が変わる最中) も含めた実効の遮断を確かめる (維持できなければ稼働中のコンテナを隔離・停止)。N1 は鎖の順序でなく実効の verdict とカウンタで判定、N5 の br0 自身の番地は N4 の input で数える、N6 は IPv6 のパケットが実際にフックへ届く経路で、N8 は LAN からの転送とホスト自身が始める通信を分ける。JIT の runner の自動更新は、更新の対象になる版で観測する。
- 段 5: 復旧の実演の前に手本を標準側だけにし (段 5b を先に)、どの job が待ったかを記録。ref の大小文字の境界と、取り消しの完了の後の全体の回し直しで、同じ run・同じ SHA・全シャードが標準の runner で success になることを照合する。

## 確定の後の追記: メモリの上限を 20GiB に (2026-11-19、ユーザー決定)
- **ユーザーの指示** (ci-server のセッションで直接、2026-11-19 20:41 頃): 「上限をあげて comfyui と競合しない前提で設定」。§3 の `limits.memory=12GiB` を **20GiB** に上げた (project os32-ci と default の profile、swap は false のまま)。CPU (8 スレッド)・ディスク (30GiB)・同時 1 本は変えない。
- **きっかけ**: 本番の最初のジョブ (2026-11-19 20:26、build のシャード 4) で memory.peak が 10797 MiB (12GiB の 88%)。段 3 の手本の回の山は 8775 MiB。OOM は 0。
- **余り**: ホストは計 30976 MiB。ComfyUI が待機 (RSS 約 1.7GiB) の時の空きは約 26〜28GiB で、20GiB なら 8GiB 前後残る。ComfyUI がモデルを持つ (約 21GiB) のと同時には成り立たないので、**生成を動かす時は変数 `OS32_SELFHOST_SHARDS` を `[]` にして loop を止める** 運用が前提。
- **連動して直す物** (root の台本 host-setup-10.sh、ユーザーが実行): loop の「起こさない」条件を空き 14336 MiB 未満 → 22528 MiB 未満 (20GiB + 2GiB) に、os32-ci-cycle の記録の limit を実物の memory.max から読む。§3 の「2 本にする条件」は 12GiB を前提にした値なので、2 本にする時は改めて決める。
