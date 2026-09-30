# lan_bridge — RED → GREEN の記録

票: [`docs/tasks/realhw/TASK_LAN_82557.md`](../../docs/tasks/realhw/TASK_LAN_82557.md) §2 **L-D** / §5-1
対象: `tools/lan_bridge.py` (物理 NIC の AF_PACKET ↔ `tools/host_agent.py` の FrameStream)
試験: `tools/tests/test_lan_bridge.py`
実行: `make check-lan-bridge-host` (`check-par` の列)

## 何のための橋か

実機 PC-9821Ra266 とホスト (Ubuntu ノート) をローカルのスイッチで直結したとき、
OS32 リンクプロトコル (raw Ethernet, EtherType 0x88B5) のフレームを
`host_agent.py` まで運ぶ。NP21/W ではこの役を ai-debug のソケットバックエンド
(NP2NETSOCK) がしていたので、**Agent 側は 1 行も変えずに実機へ差し替えられる**
ようにするのがこの橋の役目 — つまり枠付けを NP2NETSOCK と同じにすること。

```
実機 --[スイッチ]-- NIC --[AF_PACKET]-- lan_bridge.py --[TCP/UNIX]-- host_agent.py
```

票 §2 の L-D は当初「Windows 側 Python + scapy/Npcap」と書いていたが、
ユーザー決裁 (2026-09-22) でホストが Ubuntu ノートになったので **AF_PACKET +
Python 標準ライブラリのみ**に変えた (scapy も Npcap も要らない)。

## FrameStream の枠付け (読み)

`tools/host_agent.py` の `FrameStream` (186〜213 行) と `open_stream` (1278 行):

```python
def send(self, frame):
    self.sock.sendall(struct.pack(">I", len(frame)) + frame)
```

- **4 バイトのビッグエンディアン長 + raw Ethernet フレーム**。長さは Ethernet
  ヘッダ (14B) を含むフレーム全体で、長さ語そのものは含まない。
- 受信側は `0 < n <= 65536` でなければ接続を壊れたものとして切る。
- ストリームなので 1 回の `recv` に複数フレームが入りうるし、途中で切れてもいる。
  `recv_frames()` は溜めて、揃ったものだけ返す。
- **QEMU の socket バックエンド / NP2NETSOCK と同じ枠**。だから NP21/W では
  エミュレータが、実機ではこの橋が、同じ形で Agent に繋がる。
- トランスポートは TCP (`--listen` / `--connect`) と UNIX (`--unix`) の 2 つ。
  待つ側・繋ぐ側はどちらにもなれる。

`host_agent.py` は**変えていない**。橋は同じ枠付けの写しを自前に持つ
(`lan_bridge.py` の `FrameStream`)。写しにしたのは橋を Agent から独立に動かす
ため。ずれは試験 `framing_matches_host_agent` が**両方の実物を socketpair で
突き合わせて**止める (片方だけ直しても緑にならない)。

## MAC が一致していないと片道になる — 透過を選んだ結果

橋は **Ethernet ヘッダを一切書き換えない (透過)**。書き換える案もあったが、
橋がヘッダを触りはじめると pcap と実際に線を流れたものが食い違い、実機の障害
調査で足を引っ張る。代わりに次を受け持つ:

- Agent は HELLO (SYN-ACK / ESTABLISHED) で `--mac` の値を送信元として名乗り、
  OS32 は以後その MAC 宛にフレームを送る。
- 橋が書き換えない以上、`host_agent.py --mac` が **NIC の MAC と一致していないと**
  実機からの返信の宛先が NIC の MAC でなくなり、非 promiscuous の NIC が落とす。
  **OS32 → ホストだけ通ってホスト → OS32 が死ぬ**、という分かりにくい片道になる。
- そこで `--agent-mac` に Agent へ渡したのと同じ値を教えてもらい、起動時に
  NIC の MAC と照合して違えば警告する。渡されなければ「`--mac <NIC の MAC>` を
  渡すこと」を必ず 1 行出す。逃げ道として `--promisc` を用意したが既定にしない
  (取り違えを黙らせてしまうため)。

## RED

1. `test_lan_bridge.py` を先に書いた。`tools/lan_bridge.py` が無いので
   `--fake-nic` に繋ぐ相手が起動せず、全ケースが RED。

2. 実装後の否定側 (`--mutate`)。**変異は写しの上で行う** (一時ディレクトリへ
   `lan_bridge_mutN.py` を書き、試験はそれを起動する) ので実物のソースは
   1 バイトも動かない = `check-par` で並列に回せる。最後に実物が変わっていない
   ことを表明する。

   | # | 壊し方 | 結果 |
   |---|---|---|
   | 1 | EtherType のフィルタを外す (0x88B5 以外も Agent へ流す) | RED (1 件) |
   | 2 | 枠付けの長さ前置きを 1 バイトずらす | RED (5 件) |
   | 3 | 逆向き (agent → NIC) を落とす | RED (4 件) |
   | 4 | 長さ前置きのバイト順を LE にする | RED (5 件) |
   | 5 | SIGUSR1 のハンドラから直に `print` する | RED (1 件) |

3. 試験を書いている途中で**実装の欠陥が 2 件**出た (これも RED)。

   | | 症状 | 直し方 |
   |---|---|---|
   | (a) | NIC 側の口が閉じたときの `ConnectionError` を「host_agent が切れた」と表示して **Agent へ繋ぎ直しに行っていた**。NIC は繋ぎ直しても戻らないので、再接続を回しつづけて NIC の異常が運用者に見えない | `NicGone` を分け、NIC の消失は統計を出して終わる |
   | (b) | **`kill -USR1` で橋が落ちる。** シグナルハンドラから直に `print` していたので、主流がちょうど `print` の中だと `RuntimeError: reentrant call inside <_io.BufferedWriter>` が**割り込まれた側の print から**上がって死ぬ。並列で 16 本回して踏んだ (1 本) — 単体で 20 回以上回しても出なかった | ハンドラは**旗を立てるだけ** (`request_report`)。主ループが `select` の期限で起きたときに出す。変異 5 がこれを固定する |

## GREEN

```
SUMMARY 9/9 PASS
MUTATION 1 RED (1 件): EtherType のフィルタを外す (0x88B5 以外も Agent へ流す)
MUTATION 2 RED (5 件): 枠付けの長さ前置きを 1 バイトずらす
MUTATION 3 RED (4 件): 逆向き (agent -> NIC) を落とす
MUTATION 4 RED (5 件): 長さ前置きのバイト順を LE にする
MUTATION 5 RED (1 件): SIGUSR1 のハンドラから直に print する (再入で橋が落ちる)
```

並列 16 本 × 2 周でも 9/9 (この試験は `check-par` の列に入るので、負荷の下で
落ちないことまで見ておく)。

## 何を見ているか

足場は **root も実 NIC も要らない**。橋の `--fake-nic PATH` (NIC の差し替え口) に
贋 OS32 を UNIX ソケットで繋ぎ、反対側には **実物の `host_agent.py`** を
`--unix` で子プロセス起動する (`tools/tests/test_net_link.py` と同じ作法)。

```
贋 OS32 --[UNIX]-- lan_bridge.py (子プロセス) --[UNIX]-- host_agent.py (実物・子プロセス)
```

贋 OS32 は**フレームも枠付けも自前で組む**。`lan_bridge.py` の直列化を借りると
「橋と試験が同じだけ間違っている」を見逃す。

| ケース | 見るもの |
|---|---|
| `framing_matches_host_agent` | 橋の `FrameStream` と `host_agent.py` の `FrameStream` を socketpair の両端に置いて**互いの枠を解けるか**。さらに生のバイト列が `pack(">I", n) + frame` ちょうどか |
| `l0_hello_roundtrip` | L0 相当 — 3 way HELLO (SYN → SYN-ACK → CONFIRM → ESTABLISHED) と REQUEST `PING` → RESPONSE 200 が**両方向**橋を通る。sess が採番され、統計が往復を数えている |
| `foreign_ethertype_is_dropped` | EtherType を 0x0800 に変えただけの HELLO SYN が **Agent まで届かない** (届けば SYN-ACK が返ってしまう)。その後も橋は生きていて HELLO が通る |
| `short_frame_is_dropped` | Ethernet ヘッダ (14B) にも満たないフレームで添字エラーを起こさず、`drop_short` に数える |
| `stats_on_sigusr1_and_exit` | 統計が **SIGUSR1 でも終了時でも**出る (運用中に覗ける) |
| `sigusr1_while_busy_keeps_the_bridge_alive` | **忙しいときの SIGUSR1 連打で橋が落ちない** (上の RED (b))。400 フレーム流しながら 400 回撃ち込み、嵐の後も中継が生きていることと、出力に `Traceback` / `reentrant` が無いことを見る |
| `pcap_records_both_directions` | `--pcap` が linktype 1 (Ethernet) の pcap に**両方向**を残す (HELLO 往復で 4 レコード以上) |
| `mac_mismatch_warns` | `--agent-mac` が NIC の MAC と違えば `WARNING`、同じなら出さない |
| `af_packet_needs_root` | 実 NIC (`--iface lo`) を非 root で開いたとき、終了コード 2 と **`CAP_NET_RAW` / `setcap` を名指す**文言で止まる (root で回したときは SKIP) |

## ここで緑でも実機合格ではない ([V4])

**AF_PACKET の経路そのものはこの試験では一度も通っていない。** `--fake-nic` は
UNIX ソケットで、`NicPort` (raw ソケットの生成・bind・`sendto`・promiscuous 参加・
`/sys/class/net/*/address` からの MAC 読み) は `af_packet_needs_root` の
「権限が無い」経路しか踏んでいない。実 NIC で確かめるのは PM / ユーザー:

- `--iface` の bind と `htons(ethertype)` のフィルタが本当に 0x88B5 だけ拾うか
- `sendto(frame, (iface, ethertype))` が線に出るか (60B 未満の padding 込み)
- 実機との MAC の噛み合い (上の「片道になる」)、スイッチ越しの到達
- 実効スループットと取りこぼし (`drop_*` の統計)

## 試験を書いていて踏んだ罠

- **`print` の本文と改行の間にシグナルが割り込む。** 橋を SIGINT で畳んでいた
  ころ、`print("...", flush=True)` の本文を書いた直後・改行の前に
  KeyboardInterrupt が入って、統計行が前の行の尻にくっついた
  (`host_agent が切れた: peer closedlan_bridge stats (exit): ...`)。
  8 回に 1 回ほど落ちる。**シグナルで畳むのをやめて** NIC の口を閉じれば橋が
  自分で終わるようにし (`NicGone`)、統計の拾い方も行頭一致から
  行中の検索に変えた。
- **`print` の再入は単体では踏めない。** 並列 16 本で初めて出た。`check-par` の列に
  入れる試験は、入れる前に負荷をかけて回しておく。
- `host_agent.py` は REQUEST に **ACK を先に返してから** RESPONSE を返す。
  1 フレーム目を RESPONSE だと決めつけると落ちる。
- NIC への送信が失敗しても橋は止めない (`tx_error` に数えて次へ)。線が落ちただけで
  Agent との接続まで畳むと、復旧のたびに HELLO からやり直しになる。ただし口が
  閉じた (`ConnectionError`) ときだけは `NicGone` で終わる。
