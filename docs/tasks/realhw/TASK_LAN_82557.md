# TASK_LAN_82557 — 内蔵 LAN (Intel 82557、PCI) で Host Services を動かす

> 状態: **実装中** — L-A・L-D 着地、R1〜R4 は実機で取得。次は L-B (EEPROM 読み = R5、未着手)。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-fable-5-1`、2026-09-22) / それまでの状態: **L-A (PCI 列挙、KAPI v58) と L-D (Linux の橋) を着地 (2026-09-22)。**R1〜R4 は実機で取得 (2026-09-23、§6)**: 82557 = `0:11.0`、Interrupt Line **3** / PIN A、I/O BAR **0x6000** (BIOS 割当済み)、Command 0x0147 (IO/MEM/BM 有効)、PCMC = 82441FX (メカニズム #1 で可)。次は L-B (EEPROM 読み = R5)**。決裁: §3 の 1 は (b) 推奨案、2 は Ubuntu ノート、3 はスイッチ直結 (ユーザー、2026-09-22)。
> ユーザー指示 2026-09-22: 「シリアル転送が実用的な速度になったので、次は内蔵 LAN によるホストサービスの稼働を目指す」

正典: [`PLAN.md`](PLAN.md) §5 (82557 を狙う理由)・§6 (PCI の土台)、[`../../archive/v3/PLAN.md`](../../archive/v3/PLAN.md) §1 (順序) ・§3 (ドライバの動的読み込み)、
リンク層と Host Services は [`../network/LINK_PLAN.md`](../network/LINK_PLAN.md) / [`../network/HOST_SERVICES_PLAN.md`](../network/HOST_SERVICES_PLAN.md)。
資料: `docs/hw/undocumented/io_pci.md` (PC-98 の PCI)、**`docs/hw/intel/8255x_open_source_sdm.pdf`** (Intel 8255x Open Source Software Developer Manual、
341 ページ、2026-09-22 に Intel の公開 URL から取得。`docs/hw/` は著作権物のミラーで gitignore)。テキスト版 `8255x_open_source_sdm.txt` (9,133 行、`pdftotext -layout`) も同じ場所 — grep で引く。要点の所在: SCB = §6.3.2 (34 ページ)、EEPROM = §5/§6.3.4、Configure = 62 ページ、RFD = 100 ページ、PCI Interrupt Line = §4.1.15。

## 0. 到達点

実機 PC-9821Ra266 のオンボード 82557 で、**既存のリンク層 (raw Ethernet、EtherType 0x88B5) と Host Services
(クリップボード・印刷・HTTP・時刻) がそのまま動く**こと。エミュレータ側の LGY-98 経路は残す (回帰用)。

## 1. 分かっていること

| 事実 | 出典 |
|---|---|
| NP21/W は PCI を再現しない (`0CF8h` の実装が無い)。**PCI も 82557 も実機でしか動かせない** | PLAN §5/§6 |
| 実機との会話はシリアル 115200 で 3.2 KB/s、`tools/rshell_serial.py` (2026-09-22) | TASK_SERIAL_VFAST |
| PC-98 の PCI: コンフィギュレーションメカニズム #1 (`0CF8h` DWORD でアドレス、`0CFCh` でデータ)。マーキュリー (82434LX) だけ #2 が要る。PCMC = デバイス 0、PCI-C バスブリッヂ = デバイス 1、スロット #0〜2 = デバイス 8〜10 | `io_pci.md` 40〜75 行 |
| PIRQ0〜3 → 8259 の割り当ては C バスブリッヂの PCI コンフィギュレーションレジスタで、通常 PnP BIOS が起動時に設定 → **デバイスの Interrupt Line レジスタを読めば IRQ が分かる** | 同 315〜320 行 |
| ハードは規格どおりの PCI (PC/AT 用ボードが挿さる) | 同 |
| リンク層は NIC の上に載る。`drivers/lgy98.c` が `link_init(mac)` を呼び、IRQ スタブは `irq_stub_nic_3/5/6` | `drivers/lgy98.c` 122〜137 行 |
| ホスト側 `tools/host_agent.py` は **TCP の FrameStream** (NP21/W の NP2NETSOCK が繋ぐ) しか話さない。実機では LAN の生フレームを FrameStream に橋渡しする道具が要る | `host_agent.py` `open_stream()` |
| WSL2 (ミラーモード) は `eth3` (物理、f0:68:e3:fa:99:06) を見るが AF_PACKET は root 要 (sudoers に python は無い)。Windows 側には **Npcap 導入済み** (`System32\Npcap`)、scapy は未導入 (uv で入る) | 2026-09-22 実測 |
| カーネル本体の大きさと残り予算は [`docs/02_memory.md`](../../02_memory.md) §2-1 の生成ブロックが正典 (策定時 2026-09-22 の写しは 442.6KB / 468KB、残り 25.4KB。2026-09-30 は 583.0KB / 596KB、残り 13.0KB — KHEAP の切り直しで予算が 596KB になった)。予算は `MEM_KERNEL_IMAGE_MAX` = 帯 − KHEAP − KAPI − ガード − SHM。増やすなら SHM を 16KB 単位で削るか KHEAP を減らす | `docs/02_memory.md` §2-1、`include/memmap.h` (`MEM_KERNEL_IMAGE_MAX`) |
| v3 の順序は C11 → メモリマップ再配置 → **ドライバ動的読み込み → PCI → 82557** (「動かさない」)。理由は予算 (PCI 数 KB + 82557 十数 KB、USB は 100KB 超) | `../v3/PLAN.md` §1/§3 |

## 2. 段取り (案)

| 段 | やること | 大きさ (見積) | 検証 |
|---|---|---|---|
| **L-A** | **PCI の列挙**: `drivers/pci.c` (メカニズム #1 の config r/w、bus 0 の全デバイスを走査、vendor/device/class/BAR/Interrupt Line)、シェル `lspci`、`pci_find(vendor, device)`。判定は純粋関数に切り出してホスト試験 | 2〜3KB | **実機**: `lspci` で 8086:1229 (82557) と BAR (I/O 窓)、IRQ が出る |
| **L-B** | **82557 ドライバ (最小)**: PCI から I/O BAR と IRQ、SCB 経由で reset / EEPROM から MAC / Configure / IA setup / CU (TX: 1 コマンドずつ) / RU (RX: RFD 連結、簡略モード)、割り込みは RU の Frame Received + CU 完了。参照は Intel の SDM (§6 SCB、§7 RFD) と Linux e100 の作法 | 10〜15KB | **実機**: MAC が読める → 自己送信 (ループバック) → ホストへ HELLO |
| **L-C** | **NIC 境界の一般化**: `lgy98.c` ↔ `net/link.c` の結びつきを関数表 (`net/nic.h`: tx / mac / irq / poll) にして 82557 を 2 つ目の NIC に。起動時は PCI に 82557 が居ればそれ、無ければ LGY-98 (設定は手動でよい、v3 §3) | 1〜2KB | NP21/W (LGY-98 の回帰 L0〜L3) + 実機 |
| **L-D** | **ホストの橋** `tools/lan_bridge.py` (**Ubuntu 側 Python、標準ライブラリのみ** — AF_PACKET。scapy も Npcap も要らない): NIC で EtherType 0x88B5 を拾い、`host_agent.py --listen` の FrameStream へ流し、逆も返す。**実装済み** (ホスト試験 `make check-lan-bridge-host`、記録 [`tools/tests/lan_bridge_tdd.md`](../../../tools/tests/lan_bridge_tdd.md)) | ホストのみ | 実機 ↔ ホスト |
| **L-D2** | **Ubuntu 向けのホスト側バックエンド**: `host_agent.py` のクリップボード (`clip.exe`/PowerShell) と印刷 (win32print) は Windows 前提。Linux では `xclip`/`wl-copy` と `lp` (CUPS) に差し替える (無ければ 503、既存の方針どおり) | ホストのみ | Ubuntu 機で `hclip` / `lpr` |
| **L-E** | **Host Services の疎通**: 実機で `link_selftest` (L0〜L3 相当) → クリップボード / 印刷 / `wget` / TIME | — | 実機 |

L-A と L-D は独立 (並行できる)。**L-B の前に結線の土台** ([`../v3/TASK_HAL_WIRING.md`](../v3/TASK_HAL_WIRING.md): 共有 IRQ の動的登録、`dma_alloc`、PCI の結線表) を入れる。L-B は L-A の上、L-C は L-B と同時、L-E は全部の上。

### 2-1. L-D の使い方 (Ubuntu ノート)

AF_PACKET の raw ソケットには **root か `CAP_NET_RAW`** が要る。どちらかを選ぶ:

```sh
sudo python3 tools/lan_bridge.py --iface enp3s0 ...
sudo setcap cap_net_raw+ep "$(readlink -f "$(which python3)")"   # 1 回だけ
```

`setcap` は**その `python3` で動く全スクリプト**に権限を与えるので、共用機では
root で回すか専用の `python3` を置く。

起動順は **`host_agent.py` が先** (橋は Agent へ繋ぎに行く側が既定):

```sh
MAC=$(cat /sys/class/net/enp3s0/address)
python3 tools/host_agent.py --listen 127.0.0.1:8026 --mac "$MAC"   # 1 枚目の端末
sudo python3 tools/lan_bridge.py --iface enp3s0 --agent-mac "$MAC" # 2 枚目
```

**`--mac` と NIC の MAC は必ず揃える。** 橋は Ethernet ヘッダを書き換えない
(透過) ので、ずれると実機からの返信が NIC の MAC 宛にならず、非 promiscuous の
NIC に落とされて**片道だけ通る**。揃っていなければ橋が起動時に警告する
(`--agent-mac` を渡したとき)。どうしても別 MAC で回すなら `--promisc`。

記録は `--pcap FILE` (両方向、linktype 1)、統計は終了時と `kill -USR1 <pid>`。
`--listen` を使えば橋が待つ側にもなれる (`host_agent.py --connect` と組む)。

**実 NIC での確認はまだ取っていない** ([V4])。ホスト試験
(`make check-lan-bridge-host`) が踏むのは中継・枠付け・フィルタまでで、
AF_PACKET の bind と `sendto` は実機の回で確かめる (§5-2)。

## 3. ユーザー決裁が要る点

1. **予算と v3 の順序** — **決まった: (b)** (ユーザー、2026-09-22)。L-A (2〜3KB) は決裁時の残り 25KB に収まる (当時の写し。現在の残りは §1 の正典)。L-B (10〜15KB) を静的リンクすると予算をほぼ使い切る。
   v3 §1 は「ドライバ動的読み込み → PCI → 82557」の順を「動かさない」としている。選択肢:
   (a) 順序どおり: 先に v3 の 1〜3 (C11 / 再配置 / 動的読み込み) を片付ける (大きい、LAN は先送り)。
   (b) **PM の推奨**: L-A を先に (小さく、実機でしか検証できない土台を早く通す)、L-B は予算を測りながら静的で入れ、
       SHM を 16KB 削って余裕を作る (`kernel/shm.h` の `SHM_BLOCK_COUNT` と同時)。動的読み込みは 82557 が動いてから
       「外に出す最初のドライバ」として v3 §3 に戻す。
2. **ホスト側の橋の置き場** — **決まった (ユーザー、2026-09-22)**: 実機のホストは **Ubuntu ノート (Intel i3 3000 系)**。
   この Windows/WSL 機は実機を手で操作する場面だけのホスト。→ 橋は **Linux の AF_PACKET** で書く (`tools/lan_bridge.py`、root か
   `CAP_NET_RAW`)。`host_agent.py` (Python 標準ライブラリ) も同じ Ubuntu 機で動かす。`rshell_serial.py` は pyserial なので
   Ubuntu でも `--port /dev/ttyUSB0` でそのまま。Windows/Npcap 版は作らない。
3. **配線** — **決まった**: Ubuntu 機と実機はローカルのスイッチで直結 (同じ L2)。
4. **82557 で確かめてから** コンボカード (1394US2G-PCI) を判断する (PLAN §6 のまま)。

## 4. 実機でしか分からないこと (最初に測る)

- `lspci` の出力 (82557 のデバイス番号・BAR・Interrupt Line)。PC-98 の PIRQ→IRQ が何番に落ちているか。
- 82557 の I/O 窓が PC-98 の I/O 空間 (16 ビット) のどこに置かれているか (BIOS が割り当て済みか、0 なら自分で割り当てが要る)。
- バスマスタ DMA で読む RFD/CB の物理番地: OS32 のカーネル帯 (1MB 超) を 82557 は 32 ビットでアクセスできるので
  `0439h` の 1MB 制限 (FDC の DMA) は関係ない — ただし **ページング下で物理番地を渡す** (カーネル帯は恒等写像)。

## 5. 実機でしか分からないこと / 手元で済むこと (ユーザー指示 2026-09-22: 切り分けておく)

### 5-1. 手元 (ホスト試験・NP21/W) で済むこと — 実機を待たずに作って固める

| 段 | 手元で確かめられること | 手段 |
|---|---|---|
| L-A PCI | `0CF8h` のアドレス語の組み立て (bus/dev/fn/reg、bit31)、BAR の種別と大きさの復号 (I/O / メモリ / 32-64 ビット、サイズ = `~(mask) + 1`)、クラス・ベンダ表の引き、列挙の走査順とマルチファンクション判定 (Header Type bit7)、`lspci` の整形 | **純粋関数に切り出してホスト試験** (変異つき)。NP21/W では `0CF8h` が無いので `lspci` は「PCI 無し」を正しく報告することだけ見る |
| L-B 82557 | SCB コマンド語・CB/RFD の直列化 (LE、C 構造体の padding に依存しない)、EEPROM の読み出しビット列 (opcode 6、アドレス幅の自動判定)、RFD リングの前後処理 (EL/S ビット、RU の再開判定)、Configure の 22 バイト | 純粋関数 + ホスト試験。**動作はエミュレータで一切見られない** |
| L-C NIC 境界 | `net/nic.h` の関数表に LGY-98 を載せ替えて **L0〜L3 の回帰が NP21/W で通る**こと | NP21/W (既存の `link_selftest` / L1〜L3) |
| L-D 橋 | AF_PACKET ↔ FrameStream の中継 (フレームの往復、EtherType のフィルタ、MAC の照合)、FrameStream の枠付け | **済 (2026-09-22)**: `make check-lan-bridge-host`。橋の `--fake-nic` (NIC の差し替え口) に贋 OS32 を繋ぎ、対向は **実物の `host_agent.py`** を `--unix` で子プロセス起動。root も `veth` も要らない。記録 [`tools/tests/lan_bridge_tdd.md`](../../../tools/tests/lan_bridge_tdd.md) |
| L-E Host Services | プロトコルそのもの | 既に NP21/W で受入済み (LINK_PLAN §5-1)。実機では **経路が変わるだけ** |

### 5-2. 実機でしか分からないこと — 最初の 1 回で全部まとめて取る

シリアル (115200) で `lspci` と生ダンプを取るだけの回。ドライバは無くてよい。

| # | 取るもの | 何に効くか |
|---|---|---|
| R1 | `lspci` の全行 (bus 0〜、vendor:device、class、Header Type、BAR0〜5 の生値、Interrupt Line/Pin) | 82557 が **8086:1229** で見えるか、デバイス番号 (オンボードなので 8〜10 のスロット外の番号のはず)、**I/O BAR に BIOS が番地を割り当て済みか (0 なら自分で割り当てが要る)**、メモリ BAR の位置 |
| R2 | 82557 の config 256 バイトの生ダンプ (`pcidump b d f`) | Command レジスタ (I/O Enable / Bus Master が立っているか)、Latency Timer、Subsystem ID (機種固有の実装)、Cap ポインタ |
| R3 | PCI-C バスブリッヂ (デバイス 1) の config ダンプ | **PIRQ0〜3 → 8259 IRQ の割り当て** (`io_pci.md` は「PnP BIOS が設定する」としか言わない。レジスタ位置は資料に無いので Interrupt Line の値と突き合わせる) |
| R4 | PCMC (デバイス 0) の vendor:device | チップセット世代 (メカニズム #1 が使えるかの裏取り。マーキュリー 82434LX なら #2) |
| R5 | 82557 の EEPROM 先頭 (MAC 6 バイト + サイズ語) を I/O BAR 経由で読む (L-B の最小片) | MAC が取れる = I/O 窓と EEPROM 制御が正しい。**L-B の残りはこれが通ってから** |
| R6 | 割り込みが本当に来るか (Interrupt Line の IRQ に対して CU 完了 1 回) | PC-98 の PIRQ 配線は資料どおりでも、Interrupt Line の値が正しく設定されていない機種があり得る。来なければポーリングで進める |
| R7 | バスマスタで書いた RFD が **物理 1MB 超の番地** に届くか (ページング下で恒等写像のカーネル帯) | FDC の `0439h` とは別系統 (PCI は 32 ビット)。念のため |

**R1〜R4 は L-A だけで取れる。R5〜R7 は L-B の最小片 (EEPROM 読みと 1 コマンド) が要る。** 最初の実機の回で R1〜R4 を取り、
その結果 (I/O 番地・IRQ) を定数ではなく **PCI から読んだ値で使う**形に L-B を書く。

### 5-3. 手元で作っておくが実機で答え合わせするもの

- Configure コマンドの 22 バイト (Linux e100 の既定値を写す) — **リンクが上がるかは実機の PHY 次第** (82557 は外付け PHY、MII 経由。PHY の ID も R2 相当で読む)。
- 10/100 の自動判別 — 82557 自体は PHY 任せ。スイッチとのネゴシエーションは実機。
- 実効速度 — L-E で `link_l2_stream` 相当を実機で測る。

## 6. 進捗

| 日付 | 段 | 内容 |
|---|---|---|
| 2026-09-22 | L-A | `drivers/pci.c` / `pci_decode.c` (メカニズム #1、bus 0 + ブリッジ配下、32 件の静的表)、`lspci` / `pcidump` (KAPI **v58**: `pci_count` / `pci_get` / `pci_cfg_read32`)、`inpd/outpd` は `platform/pc98/platform_io.h`。判定は 2 値 (`0x80000000` と `0x80FFFFFC` の読み戻し) — 実機で `absent` なら 1 値に緩める。ホスト試験 `check-pci-decode-host` (9 ケース、変異 7 本)。**カーネル 446.9KB / 468KB (残り 21.1KB)**。NP21/W: `[pci] 0 devices`、`lspci: no PCI (mechanism #1 not present)`、kselftest 104/104 |
| 2026-09-22 | L-D | `tools/lan_bridge.py` (AF_PACKET ↔ FrameStream、標準ライブラリのみ、MAC は透過、`--agent-mac` 照合)。ホスト試験 `check-lan-bridge-host` (9 ケース、変異 5 本、実物の host_agent と L0 往復)。実 NIC は未確認 |
| 2026-09-22 | 検証 | `make clean` → `make all` → `make external` → `make check` exit 0 (98a0785) |
| 2026-09-23 | **R1〜R4 (実機)** | Ra266、Build Sep 23 10:20 (API v61)、Ubuntu ノートのシリアル越し (`tools/rshell_serial.py`)。**`lspci` (R1)**: `0:0.0 8086:1237` Host bridge (**82441FX PMC → R4: メカニズム #1 で正しい**) / `0:6.0 1033:002c` NEC bridge class 06.80 (**C バスブリッヂ = デバイス 6**、資料の「デバイス 1」ではない) / `0:7.0 1033:0009` NEC display class 03.80 (PEGC 側) / `0:8.0 1023:9660` Trident (irq 255 = 未割当、bar0〜2 mem 0x20000000/0x20400000/0x20800000) / **`0:11.0 8086:1229` Intel 82557 class 02.00 irq 3 pin A bar0 mem 0x20410000 bar1 io 0x6000 bar2 mem 0x20500000** / `0:12.0 1102:0007` Creative CA0106 (SB Live! 24-bit、PCI スロットの増設カード) class 04.01 irq 6 pin A bar0 io 0x6020。**R2 (82557 の config)**: `00: 86 80 29 12 47 01 80 02 02 00 00 02 00 60 00 00` → Command 0x0147 (IO+MEM+BusMaster、PERR 応答)、Status 0x0280、rev 02、Latency 0x60 / `10: 08 00 41 20 01 60 00 00 00 00 50 20` → BAR0 mem prefetchable 0x20410000、BAR1 io 0x6001、BAR2 mem 0x20500000 / `2c: 33 10 00 80` → Subsystem **1033:8000 (NEC)** / `34: 00` Cap 無し / `3c: 03 01 08 38` → **Line 3、Pin A**、MinGnt 8、MaxLat 0x38。**R3 (C バスブリッヂ 0:6.0)**: `40: 10 00 f6 00 fa 00 fb 00 fe 00 fe 00 00 01 ff 03` / `50: 00 01 02 0f 04 05 86 07 88 09 8a 0b 8c 0d 0e 8f` (0x50〜0x5f は IRQ 0〜15 の表に見える: 値 = IRQ 番号、bit7 が 6/8/10/12/15 に立つ。PIRQ→IRQ の対応はここから読めるかを L-B で解読) / `60: 06 80 80 03 50 3e 07 18 00 00 00 0c` / `70: 0c c0 00 00 00 00 00 00 b2 d9 0f 00`。**PCMC (R4)** `50: 30 82 00 8f 80 dd ff 01 1a 10 00 00 00 01 00 11` / `60: 04 04 08 08 08 08 08 08 80` (DRB) / `70: 08 1f 0a`。**L-B への帰結**: I/O 窓は BIOS が 0x6000 に割り当て済みで自前の割当は不要、IRQ は master 3 (動的 IRQ の対象、`irq_register(3)`)、共有相手は無い (IRQ 3 は他に無い)。Trident と PCM (CS4231 は C バス側、PCI には出ない) は無関係 |

**R1〜R4 は 2026-09-23 に取得済み (上の表)**。2 値判定はそのまま通った (6 装置)。次の実機の回は **R5 (EEPROM から MAC)** — L-B の最小片が要る。
