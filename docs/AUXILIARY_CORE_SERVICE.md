# Auxiliary Core Service 設計草案

> 状態: **草案 (2026-09-28)** — 設計思想の草案で、ロードマップ・実装契約ではない。v3 本案をまとめる段で位置を決める。

## 目的

OS32 は「1アプリへ資源を集中する」設計思想を維持し、一般的なSMP OSのようなマルチコア・マルチスレッドスケジューリングを主目的としない。

一方、近年の x86 / ARM SoC では複数CPUコアが一般的であり、OS32が1コアのみを使用すると大きな計算資源が未使用になる。

そこで余剰CPUコアを、汎用タスクを実行するCPUではなく、GPU / DSP / codec 等に相当する **固定機能アクセラレータ** として利用する。

仮称を **Auxiliary Core Service (ACS)** とする。

## 基本原則

- OS32の主実行系は原則として1コアで動作する。
- 汎用SMPスケジューラを必須にしない。
- 補助コアには起動時またはサービス初期化時に役割を固定する。
- 補助コアはコマンドキューを待機し、割り当てられた処理のみを実行する。
- OS32アプリケーションからCPUトポロジを直接意識させない。
- 補助コアが存在しない環境では主コアによるソフトウェア処理へ縮退できる。
- ISA固有APIにしない。x86 AP、ARMマルチコア等を同一モデルで扱う。
- ハードウェアGPU/DSPが存在する場合も、上位HALから見た機能契約は可能な限り共通化する。

## 概念構成

```text
Application
    |
OS32 HAL / Service API
    |
    +----------------------+----------------------+
    |                      |                      |
Primary CPU          Auxiliary Core 1      Auxiliary Core 2
OS32 + App           Graphics Worker       Graphics Worker
                                               
                           Auxiliary Core 3
                           Audio / DSP Worker
```

主コアだけが通常のOS32実行環境を持つ。補助コアは完全なOS実行環境を要求せず、専用ワーカーループとして動作できる。

```c
for (;;) {
    command = auxiliary_queue_wait();
    auxiliary_execute(command);
}
```

## Graphics Worker

最初の用途としてソフトウェア3Dラスタライザを想定する。

OS32 3D HALが提供する機能を、ハードウェアGPUではなく補助CPUコアで実装可能にする。

例:

- triangle rasterization
- texture sampling
- Z test / Z buffer
- alpha blending
- framebuffer clear
- primitive transform の一部
- framebuffer composition

複数コアを利用する場合は、画面タイル、scanline、primitive batch 等の単位で処理を分割する。

重要なのは、アプリケーションから見ればハードウェアGPU実装とCPU Rasterizer実装の差を見せないことである。

```text
                 OS32 3D HAL
                      |
        +-------------+-------------+
        |                           |
Hardware 3D Driver           CPU Rasterizer
                                    |
                          +---------+---------+
                          |         |         |
                        Core 1    Core 2    Core 3
```

これにより、新しいGPUの複雑なネイティブ3Dドライバが存在しない環境でも、十分なCPU性能とFramebuffer出力があれば3D HALを提供できる。

## Audio / DSP Worker

同じ方式を音声処理にも適用できる。

候補:

- software FM synthesis
- PCM mixing
- resampling
- format conversion
- codec処理

OS32のソフトウェアミキサーや fmgen 等を補助コアへ移すことで、主コアのアプリケーション実行時間を確保できる。

## x86

x86 SMP環境ではBSPをOS32主コアとし、APをACSワーカーとして利用する構成を基本候補とする。

例:

```text
Core 0 / BSP : OS32 + Application
Core 1 / AP  : Graphics Worker
Core 2 / AP  : Graphics Worker
Core 3 / AP  : Audio/DSP Worker
```

APに通常プロセスをスケジュールする必要はない。

このため「SMP対応」と「Auxiliary Core対応」を明確に区別する。

## ARM

ACSはx86固有機能にしない。

マルチコアARM SoCでも、

```text
Primary ARM core   : OS32 / guest execution
Secondary core(s)  : graphics / audio / codec / host service
```

という同じモデルを使用する。

PS VitaのようなマルチコアARM機、ARM SBC、モバイルSoC、将来のOS64ホスト環境などでも同じ抽象を利用できることを設計目標とする。

big.LITTLE等の非対称構成についても、将来的には低性能コアをI/O/DSP、高性能コアを主処理またはRasterizerへ割り当てる余地がある。ただし初期実装では動的負荷分散を要求しない。

## 縮退モデル

ACSは必須機能にしない。

```text
Single Core
  OS32 + software processing

Dual Core
  Core 0: OS32
  Core 1: accelerator

4 Core
  Core 0: OS32
  Core 1-3: accelerator workers

Hardware accelerator available
  OS32 HAL -> native GPU/DSP driver
```

同じアプリケーションが古い単一コアPCから新しいマルチコアx86/ARMまで動作することを優先する。

## Host Serviceとの関係

ACSとHost Serviceは役割を分離する。

処理配置の上位原則は [LEGACY_LIVING_PRESERVATION.md](LEGACY_LIVING_PRESERVATION.md) を参照する。ACSはHost Serviceの代替ではなく、実機内部で余っている計算資源を固定機能として活用するための仕組みである。AI、現代codec、TLS等、実機で実行する体験上の意味が薄い高負荷・時代依存処理はHost Serviceへ委譲する。

- **ACS**: 同一マシン上の余剰CPUコアを固定機能アクセラレータとして利用する。
- **Host Service**: OS32外部またはホスト環境が提供するサービスを利用する。

ただし上位HALからは共通バックエンドとして扱える設計が望ましい。

例:

```text
OS32 3D HAL
    |
    +-- native GPU
    +-- Auxiliary Core software rasterizer
    +-- Host Service renderer
    +-- primary-core software rasterizer
```

これにより実機、仮想機、エミュレータ、x86、ARMの差をアプリケーションから分離する。

## 非目標

初期段階では以下を目標としない。

- 一般用途のSMPスケジューラ
- 任意プロセスの複数コア分散
- POSIX threads相当の汎用マルチスレッドモデル
- 動的な高度負荷分散
- コア間NUMA最適化
- GPU APIを現代GPUの機能に合わせて拡張すること

余剰コアは「追加CPU」ではなく「OS32に接続された計算デバイス」として扱う。

## 実装段階案

1. CPUトポロジ検出と補助コア起動
2. 最小コマンドキュー / mailbox
3. 1補助コアでGraphics Workerを実証
4. 複数Graphics Workerへの分割
5. Audio/DSP Worker
6. HALバックエンドとして統合
7. ARM実装で同一モデルを検証

## 設計上の意義

この方式では、OS32の単純な実行モデルを維持したまま、世代が新しいハードウェアの余剰計算資源を利用できる。

特に「マルチコアCPUを汎用SMP資源として扱う」のではなく、「必要な機能へ専用化できる複数の計算エンジン」として扱う点をOS32の設計方針とする。

これにより、古い単一コアPCではそのまま動作し、新しいx86やARMでは余剰コアがGPU/DSP等の性能拡張装置として自然に参加できる。
