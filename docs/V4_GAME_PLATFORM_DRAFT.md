# OS32 v4 草案 — Portable Game OS / Multi-Architecture / OS32 Fabric

*草案策定: 2026-09-07*

> 本書は v4 世代を見据えた長期設計のたたき台である。現行 PC-98 実装を直ちに置換する計画ではなく、
> OS32 を「PC-98 専用 OS」から、ゲーム用途を中心に複数の異種ハードウェアでソース資産を共有できる
> オープンなゲーム OS / プラットフォームへ発展させる際の設計原則を記録する。
>
> v1〜v3 の実装・互換性・安定化を優先し、本書の内容は段階的に導入する。

---

## 1. 目的

OS32 v4 の長期目標は、特定 CPU や特定ゲーム機のハードウェア構成を標準化することではない。
標準化するのは **アプリケーションから見た API、データ型、実行モデル、デバイス能力の意味論** とする。

目標は次の通り。

1. 同一のゲーム / アプリケーションソースを、異なる OS32 ターゲット向けに再コンパイルして利用できること。
2. PC-98 固有機能を失わず、portable API と platform-specific API を明確に分離すること。
3. CPU / GPU / 音源 / 入力 /ストレージの違いを HAL / platform 層で吸収すること。
4. ゲームエンジンは薄い Core と選択可能な Module に分け、用途差は Template / Sample で補うこと。
5. OS32 搭載機同士が協調し、他ノードの GPU・音源・入力・ストレージ・計算能力をデバイスとして利用できること。
6. 協調通信は汎用ネットワークとは分離し、必要最小限の capability-only protocol として安全側に設計すること。

---

## 2. 基本思想

### 2.1 ハードウェアではなくソフトウェア意味論を標準化する

MSX が共通ハードウェア仕様によってソフト互換性を実現したのに対し、OS32 はより上位の層を標準化する。

```text
                  OS32 Portable API
                         |
       +-----------------+-----------------+
       |                 |                 |
     i386              SH-4             RISC-V
       |                 |                 |
    PC-98 / Xbox      Dreamcast      ESP32-P4 / MSX3
```

CPU、GPU、RAM 容量、ストレージ方式は一致していなくてよい。
portable API が同じ意味で動作することを互換性の中心とする。

### 2.2 互換性レベル

OS32 の移植先ごとに、互換性を次の3段階で区別する。

| Level | 名称 | 意味 |
|---|---|---|
| 1 | Source Compatibility | 同じ C / Rust ソースを再コンパイルして動作できる |
| 2 | API / ABI Compatibility | KAPI 構造・型・呼び出し契約まで共通 |
| 3 | Binary Compatibility | 既存ターゲット向けバイナリが無変換で動く |

v4 で必須とするのは **Level 1**。Level 2 は可能な範囲で維持する。
Level 3 は同一アーキテクチャ等、合理的な場合のみ対象とし、全 platform 共通要件にはしない。

---

## 3. Portable API と Platform API

### 3.1 Portable API

通常のゲーム / アプリケーションは portable API のみで実装できることを目標とする。

例:

```c
#include <os32/os32.h>

/* portable */
os32_file_open(...);
os32_mem_alloc(...);
os32_time_ticks(...);
os32_gfx_present(...);
os32_input_poll(...);
os32_audio_play(...);
```

対象候補:

- file / VFS
- heap / memory allocation
- time / timer
- graphics
- audio
- input
- network (利用可能 platform のみ。capability で検出)
- save
- SQLite / DB
- GUI
- game runtime

### 3.2 Platform-specific API

機種固有の魅力を消すための抽象化は行わない。
固有機能を使うアプリは明示的な platform header を利用する。

```c
#include <os32/platform/pc98.h>
#include <os32/platform/msx.h>
#include <os32/platform/dreamcast.h>
```

例:

- PC-98: V86、BIOS互換機能、KCG、PEGC 固有操作
- MSX: VDP / PSG / slot 固有操作
- Dreamcast: PVR 固有機能
- Xbox: NV2A 固有機能

V86 のように PC-98 でのみ意味を持つ機能を、他 platform に無理に実装しない。

---

## 4. ソースツリーの長期構造

将来的には CPU と machine/platform を分離する。

```text
arch/
  i386/
  sh4/
  riscv32/
  riscv64/
  ppc/

platform/
  pc98/
  xbox/
  dreamcast/
  esp32p4/
  msx3/
  ...

kernel/
fs/
kapi/
gfx/
exec/
...
```

`arch/` は CPU 依存の機構を扱う。

- exception / interrupt entry
- privilege transition
- syscall entry
- context / register handling
- MMU / PMP / address-space primitive
- atomic / barrier

`platform/` は機械固有の装置を扱う。

- boot
- timer source
- storage controller
- input devices
- display controller / GPU backend
- audio
- network adapter
- firmware / BIOS integration

現在の PC-98 実装を reference implementation とし、段階的に `arch/i386` と `platform/pc98` の境界へ整理する。

---

## 5. OS32 実装 Profile

全ターゲットに同じ kernel 機能を要求しない。

### 5.1 OS32 Native

OS32 kernel を直接動作させる正式 platform。

候補:

- PC-98 / i386
- Original Xbox / i386
- Dreamcast / SH-4
- ESP32-P4 / RISC-V
- MSX3 / RISC-V (最終仕様確定後)

### 5.2 OS32 Hosted

既存 firmware / homebrew SDK 上で OS32 Portable API を提供する環境。

候補:

- PSP
- Wii
- PS2
- その他、bare-metal 化より既存 homebrew 基盤を利用する方が合理的な機種

### 5.3 OS32 Game Runtime / Compact

RAM / address space 等の制約が強い機種向けに、ゲーム用途の portable API のサブセットを提供する。

候補:

- N64
- GBA
- MSX2 / MSX2+ / turboR / MSX2++

旧機種に 32-bit flat memory model を無理に模倣せず、対象 API を明示する。

---

## 6. ゲームエンジン設計

ゲームエンジンは「何でもできる巨大エンジン」ではなく、小さい部品を組み合わせる構造とする。

```text
Game Engine Core
  - game loop / time
  - input
  - graphics primitive / sprite
  - audio
  - resource
  - scene / state
  - event / message

Optional Modules
  - tilemap
  - animation
  - collision
  - turn
  - battle
  - inventory
  - AI
  - RPG
  - board
  - etc.

Templates / Samples
  - action2d
  - jrpg
  - board-game
  - turn-battle
  - adventure
```

原則:

- Core = mechanism
- Template / game = policy
- 2種類以上の異なるゲームでそのまま再利用できないルールは Core に入れない
- 「将来使えそう」という理由だけで抽象化しない
- 2つ目の実使用例が要求するまで過剰な一般化を避ける

例: 手番の進行・round counter は engine/module、7 turn = 1 week や勝利条件は template/game 側。

---

## 7. Rust 導入方針

全面書き換えは行わず、上位の純粋ロジックから段階的に導入する。

```text
Game / Template             <- Rust 化しやすい
Scene / Event / Turn / AI
Battle / Inventory / Board
Resource / Map
GFX / Input / Audio / DB
KAPI / arch / drivers       <- 当面 C / asm を優先
```

基本方針:

- 公開境界は C ABI を優先し、既存 C アプリを維持する
- Rust 実装は `no_std` を前提に検討する
- まず `turn` など OS 非依存の小さい状態機械で PoC する
- C テストを変更せず Rust 実装へ差し替えられることを初期評価基準とする
- 実測項目: code size、RAM、呼び出しコスト、toolchain の安定性

「上位から下位へ」Rust の適用範囲を広げ、境界が問題になった部分だけ下層へ降りる。

---

## 8. OS32 Fabric — 異種ハードウェア協調

v4 の特徴候補として、複数の OS32 対応機を一つのシステムとして協調させる仕組みを定義する。

```text
                 OS32 Fabric

        PC-98 -------- Raspberry Pi
          |                 |
       keyboard            GPU / HDMI
       FM sound            audio
          |                 |
          +---- ESP32-P4 ---+
                    |
                 LCD / touch
```

古い機械を新しい機械で置き換えるのではなく、古い機械固有の機能を残したまま不足能力を他ノードが補う。

### 8.1 Remote computer ではなく Remote device として扱う

アプリケーションからはローカル / リモートを極力意識させない。

```text
Application
    |
   KAPI
    |
Device abstraction
    |---------------- local driver
    |
    +---------------- remote proxy
                         |
                      OS32 Link
                         |
                    remote OS32 node
                         |
                       driver
```

リモートノードが提供できる capability の例:

- GFX
- AUDIO
- INPUT
- BLOCK / STORAGE
- NETWORK GATEWAY
- COMPUTE
- CLOCK

### 8.2 Raspberry Pi 外部 GPU を最初の PoC とする

最初の実証候補:

```text
PC-98 / OS32
   |
USB or dedicated Ethernet-like physical link
   |
Raspberry Pi
   |
HDMI / high-resolution output
```

OS32 起動前や legacy 画面には raw framebuffer transport を利用可能とし、
OS32 アプリでは帯域節約のため command transport を優先する。

例:

- sprite draw
- tilemap draw
- text draw
- fill / blit
- buffer upload
- present

Full-HD framebuffer を毎 frame 転送する方式に固定しない。

### 8.3 Compute node

将来的には描画以外にも限定された計算 job を別ノードへ委譲できる。

例:

- image decode / scale
- audio decode
- compression
- path finding
- AI
- database helper

ただし任意コードの remote execution を Fabric の基本機能にはしない。

---

## 9. OS32 Link — 閉域デバイスネットワーク

OS32 Fabric の transport / protocol は、原則として TCP/IP を前提としない。

目的は「ネットワーク接続された汎用コンピューター群」ではなく、
**ケーブルの向こうにある capability-limited device 群**として扱うことである。

### 9.1 基本原則

- IP address 不要
- TCP / UDP 不要
- DNS 不要
- routing 不要
- HTTP / Web service 不要
- remote shell 不要
- arbitrary remote exec 不要
- capability に定義された opcode のみを受理
- 外部 Internet との bridge は明示的 Gateway node のみに限定

物理 transport は複数実装を許可する。

- USB
- dedicated Ethernet PHY / frame transport
- serial
- その他 peer-to-peer transport

Ethernet 物理層を使っても TCP/IP を載せる必要はない。

### 9.2 プロトコル最小化

候補 header:

```text
version
node_id
device_id
opcode
payload_length
sequence
flags
checksum / integrity tag
```

候補 primitive:

```text
DISCOVER
CAPS
OPEN
CLOSE
READ
WRITE
IOCTL
EVENT
BUFFER_UPLOAD
BUFFER_DOWNLOAD
```

文字列ベースの shell protocol を基本仕様にしない。
可変長 payload は最大長・alignment・型・range を厳格に規定する。

### 9.3 Capability policy

例:

```text
Raspberry Pi GPU node
  allow: GFX, AUDIO
  deny : FILESYSTEM, EXEC, MEMORY, NETWORK

ESP32 input node
  allow: INPUT_EVENT
  deny : GFX, FILE, EXEC
```

能力は node 単位・device 単位に限定し、未宣言機能を利用できないようにする。

### 9.4 任意メモリアクセスを許さない

remote address を直接指定する DMA 的 API は基本仕様にしない。

NG:

```text
WRITE_MEMORY 0x12345678 ...
```

OK:

```text
BUFFER_UPLOAD handle=17 ...
```

OS が handle と所有権を検証し、許可された buffer にのみアクセスさせる。

### 9.5 信頼モデル

TCP/IP を使わないだけで安全が保証されるわけではない。
悪意あるノード、壊れた firmware、外部ネットワークに侵害された bridge node を想定する。

長期候補:

- node identity
- challenge-response authentication
- per-node public key
- trusted node list
- capability authorization
- message integrity / replay protection

最重要原則:

> Remote computer を信用しない。Remote device として扱う。

### 9.6 Gateway の隔離

Internet 接続が必要な場合は専用 Gateway node に集約する。

```text
OS32 Fabric
     |
  Gateway
     |
 TCP/IP
     |
 Internet
```

Fabric の他ノードは TCP/IP stack や Internet capability を持つ必要がない。
これにより汎用ネットワーク由来の攻撃面を最小化する。

---

## 10. 想定ターゲットと位置付け

本表はコミットメントではなく、設計検証用の候補である。

| Platform | Arch | Profile候補 | 主な意味 |
|---|---|---|---|
| PC-98 | i386 | Native / Reference | 現行実装、固有機能を維持 |
| Original Xbox | i386 | Native | 同一archで platform 分離を検証しやすい |
| Dreamcast | SH-4 | Native | 非x86 port の検証候補 |
| ESP32-P4 | RISC-V | Native / Embedded | 小型HMI、Fabric nodeにも適する |
| MSX3 | RISC-V等 | Native | 最終仕様確定後の候補 |
| PSP | MIPS | Hosted | source compatibility の実証 |
| Wii | PowerPC | Hosted / Native検討 | homebrew 基盤が成熟 |
| PS2 | MIPS | Hosted | EE/IOP は platform layer で吸収 |
| N64 | MIPS | Game Runtime | RAM制約の強い profile |
| GBA | ARM7TDMI | Game Runtime | kernel より Game SDK target |
| MSX2系 | Z80/R800 | Compact / Runtime | 16-bit address space を無理に32bit化しない |

---

## 11. v4 までに守るべき設計原則

現時点から可能な範囲で次を意識し、将来の port を不必要に困難にしない。

1. PC-98 固有処理を portable 層へ新規流入させない。
2. gfx / input / audio / storage は backend / HAL 境界を維持する。
3. KAPI の意味論と実装方式 (`int 0x80` 等) を分離して考える。
4. 固定物理アドレス・i386 ABI を portable API の契約にしない。
5. game engine module に個別ゲームの policy を混ぜない。
6. platform-only 機能は capability または専用 header で明示する。
7. remote device protocol に任意コード実行・任意メモリ操作を安易に追加しない。
8. セキュリティ上不要な network capability を各 node に与えない。

---

## 12. 段階的ロードマップ案

### Phase V4-0 — 境界の文書化

- Portable API / Platform API の棚卸し
- i386-specific / PC98-specific の分類
- KAPI の architecture-independent contract を抽出
- game engine の Core / Module / Template 境界を定義

### Phase V4-1 — 上位 portability

- game engine の一部を portable 化
- Rust PoC (`turn` / `event` 等)
- host test を増やし platform 非依存ロジックを実機なしで検証

### Phase V4-2 — OS32 Link PoC

- transport-neutral frame format
- node discovery / CAPS
- GFX remote device
- Raspberry Pi 外部 GPU PoC
- raw framebuffer + command mode の比較

### Phase V4-3 — 第二 platform

候補の一つを選び、source compatibility を検証する。
初期候補は同一 i386 arch の Original Xbox、または小型 RISC-V の ESP32-P4。

### Phase V4-4 — Fabric

- device registry
- capability routing
- input / audio node
- buffer ownership
- node authentication
- optional isolated network gateway

---

## 13. 非目標

v4 の思想を明確にするため、次は基本目標としない。

- 全 platform の binary compatibility
- 全機種で PC-98 V86 / BIOS を再現すること
- hardware abstraction のために各機種固有能力を隠すこと
- すべての game rule を engine Core に取り込むこと
- Fabric node への任意 remote shell / arbitrary code execution
- 全ノードへの TCP/IP stack 搭載
- 旧8bit機で無理に32bit flat memory modelを模倣すること

---

## 14. v4 の一文での定義

> **OS32 v4 は、異なる世代・異なるCPU・異なるゲームハード上でソース資産を共有し、必要なら各機械の固有能力を保ったまま、複数のノードを一つのゲームコンピューターとして協調させるためのオープンなゲーム OS / platform を目指す。**

PC-98 はその reference implementation であり、置き換える対象ではない。
新しい機械が古い機械を淘汰するのではなく、双方を同一の software ecosystem と OS32 Fabric の中で利用できることを長期的な価値とする。
