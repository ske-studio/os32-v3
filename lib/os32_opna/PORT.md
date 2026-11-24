# OPNA 移植台帳

移植元は NP21/W revision `b49b96a8305b4a2676ea3f6d233c7a7a2a5ab0ea`。
`upstream/` は sound/fmgen の比較用原本。readme も同所から採録し、
fmgen_readme_np2.txt だけは LICENSES/fmgen から採録した。

## 原本の SHA-256

```text
45022797065ff63aa856d224758231eae0d10fa332896590355b5dba722f7be7  fmgen_readme.txt
50f16ce53c3e5e0928716efccfceab250d06add45c6a7f387ce90fdcd1130801  fmgen_readme_kai.txt
d3b7a9cb3dce47cee20a2380820d9a590b52c7ad41230a86d5df8ed734ea3d53  fmgen_readme_np2.txt
7cbec22e348a309a3178233844a1511dbf922a7ab19910ccfae720014671147c  fmgen_readme_np21w.txt
c81dd6db82e1dc67307b066dd1c98361a9d79036ed6d71ae75fc262126abaa11  upstream/fmgen_fmgen.cpp
5fd704216a41d62bce5d1a8dbabeced23e4c974b78c7b5649969f344538e71da  upstream/fmgen_fmgen.h
d475ca831a6143f79f201341fbfeea005f1b607bb51d3b87648855b08427be00  upstream/fmgen_fmgeninl.h
53b96fe05f1a25c1e1ab2d753ca44c72f15364bfda5eb2aa9a07c53ce839dfb0  upstream/fmgen_fmtimer.cpp
a46d1a3b0ca948aaec064076eaeb5f2a4554165f7b68ea56eecf232c22e9a180  upstream/fmgen_fmtimer.h
d8306faa35f713c526ef8c691647b5ac3b0f22bd0749aaf5891faca63c90da78  upstream/fmgen_opna.cpp
2635ab6fd59ff72ae3cb06fa8ef497e41d8e5c70f03aaebc721bf16a3650e9fc  upstream/fmgen_opna.h
315ce077872f016cabfd4bc9da05e445b0cffa1d1cefc905f7f8e52c11c6f8ef  upstream/fmgen_psg.cpp
fd5c165e4f240de65e2f58fb6994f1d5441772b3b572dd8a1b6a106ff568f2ee  upstream/fmgen_psg.h
```

## 移植先の対応表（段 A 時点の予定）

下表はクラスごとの移植単位。各関数を実装した段で Rust の関数名と
整数幅・丸め・回り込みの差分を追記する。保存・復元、FileIO、OPN/OPNB
専用実装は対象外。opna.h の入口は src/lib.rs に置く。

| 元の関数 | Rust 移植先予定 |
|---|---|
| `Chip` — Chip, SetAML, SetPML | `src/state.rs` |
| `Chip` — SetRatio, MakeTable | `src/tables.rs` |
| `Operator` — MakeTable | `src/tables.rs` |
| `Operator` — Operator, Reset, SetDPBN, LogToLin, EGCalc, EGStep, PGCalc, PGCalcL, Calc, CalcL, CalcN, CalcFB, CalcFBL, Prepare, ShiftPhase, SetFNum, EGUpdate, SetEGRate, ResetFB, KeyOn, KeyOff, IsOn, SetDT, SetDT2, SetMULTI, SetTL, SetAR, SetDR, SetSR, SetSL, SetRR, SetKS, SetSSGEC, SetAMON, Mute, SetMS | `src/operator.rs` |
| `Channel4` — Channel4, Reset, Prepare, SetFNum, SetKCKF, KeyControl, SetAlgorithm, Calc, CalcL, CalcN, CalcLN, SetType, SetFB, SetMS, Mute, Refresh, SetChip | `src/channel.rs` |
| `Channel4` — MakeTable | `src/tables.rs` |
| `Timer` — SetTimerControl, SetTimerA, SetTimerB, Count, GetNextEvent, Reset | 済 `src/timer.rs`（write/count/next_event/reset） |
| `Timer` — SetTimerBase | `src/tables.rs` |
| `OPNBase` — OPNBase, SetParameter, Reset, SetPrescaler, Init, TimerA | `src/registers.rs` |
| `OPNBase` — SetVolumeFM | `src/tables.rs` |
| `OPNABase` — OPNABase, ~OPNABase, Init, Reset, SetRate, SetChannelMask, SetReg, SetStatus, ResetStatus, UpdateStatus, WriteRAM, ReadRAM, ReadRAMN, ReadStatusEx, BuildLFOTable, LFO | `src/registers.rs` |
| `OPNABase` — MakeTable2 | `src/tables.rs` |
| `OPNABase` — SetADPCMBReg, DecodeADPCMBSample, ResetCPUADPCM, WriteCPUADPCM, ReadCPUADPCMN, DecodeADPCMB, ADPCMBMix | 済 `src/adpcm.rs`（write/next_sample/decode/advance/mix） |
| `OPNA` — GetReg, OPNA, ~OPNA, Init, Reset, SetRate, SetReg | `src/registers.rs` |
| `OPNABase` — FMMix, MixSubSL, MixSubS, Mix6 | `src/channel.rs` |
| `OPNA` — RhythmMix, SetVolumeRhythmTotal, SetVolumeRhythm | 済 `src/rhythm.rs`（mix/volume、音量は全体・各声に適用） |
| `OPNA` — SetVolumeADPCM | 済 `src/adpcm.rs`（固定 0 dB の 65536 × level >> 12。段 B の ADPCM_GAIN は level=0 適用後の値） |
| `OPNA` — Mix | `src/lib.rs` |
| `PSG` — PSG, ~PSG, Reset, MakeNoiseTable, SetChannelMask, MakeEnvelopTable, SetReg, StoreSample, Mix | `src/psg.rs` |
| `PSG` — SetClock, SetVolume | `src/tables.rs` |

OPNA::LoadRhythmSample の FileIO とメモリ確保は移植対象外。C 側が読み込んだ波形を opna_rhythm で借用する。

## 浮動小数点の台帳

| 元の箇所 | ホストで焼く鍵・処置 |
|---|---|
| fmgen_fmtimer.cpp:137 SetTimerBase（有効な #if 1） | clock/2、prescaler p=0/1/2 のFM分周6/3/2、さらに/12、timer_step |
| fmgen_psg.cpp:46-48 SetClock | clock/2、p別SSG分周4/2/1、44100Hz、tone/env/noise各shift |
| fmgen_psg.cpp:89-91 SetVolume | 0dB、全振幅段；clock非依存 |
| fmgen_fmgen.cpp:193-218 MakeTable | type・LFO深さ・位相の全組合せ；clock非依存 |
| fmgen_fmgen.cpp:262-266 SetRatio | p=0/1/2で得る全ratio、dt2=0..3とmultiple全値 |
| fmgen_fmgen.cpp:355,371-375 Operator表 | TL/sine/logの全index；clock非依存 |
| fmgen_fmgen.cpp:831 Channel4::MakeTable | kftable全indexを焼く。OPM専用でも共有初期化からの到達を除去確認するまで省かない |
| fmgen_opna.cpp:156 SetVolumeFM | 0dBの整数gain |
| fmgen_opna.cpp:490 MakeTable2 | TL全index；clock非依存 |
| fmgen_opna.cpp:540 adplbase | 入力7987200/2、44100Hz。p非依存（SetRateの式どおり） |
| fmgen_opna.cpp:1708 SetVolumeADPCM | 0dBの整数gain |
| fmgen_opna.cpp:1846,2105 | OPNB専用で製品から除外。oracleに含む場合はホストのみ |

pは0/1/2、レジスタ2Dh/2Eh/2Fhによる選択。すべての表にキー・長さ・hash・元式の丸めを保存し、ゲストでの再計算を不要にする。clippyに加えfloat型/定数と最終命令の検査も行う。

## 段 B の整数表と状態

元 C++ をホストで実行する方式を採用した。元の libm 呼出しと整数変換の順序を
保持し、独立した近似式の丸め差を避ける。生成は
`python3 tools/gen_opna_tables.py`。独立した NP21/W の sound/fmgen ディレクトリを
`--source` に渡せる。`--check --mutate` は Rust 配列を実行時に列挙して
元の C++ の値と全バイト比較し、SINE の先頭値の 1 bit 反転も検出する。
tables.manifest に整数幅、要素数、little-endian の SHA-256 を記す。
配列順は元の C++ の行優先順。P0/P1/P2 は 7987200 Hz・44100 Hz の分周設定。
生成器・C++・libm は Cargo の入力に含まれず、Rust 側には整数だけを格納する。

state.rs の Adpcm は NP21/W の CPU ストリームに必要な 65536 byte FIFO を持つ。
これは 256 KiB の借用 RAM と独立しており、固定状態だけで設計の概算 16–32 KiB
を超える。FIFO の境界と BRDY の挙動を維持するため元の容量を採用した。

段 C の ADPCM RAM 転送・遅延読出し・拡張 status は adpcm.rs の
transfer/read/status_ex に移植済。timer.rs の count は A/B の期限をビットで返し、
csm_due は A 期限時点の CSM を判定する。段 D が channel 3 の key off/on を接続する。
停止時の next_event は内部 ABI に従い u32::MAX。元の Count の正の周期までの
反復加算は同値な剰余で計算する。reset は OPNA::Reset が直接変更する状態を戻し、
OPNABase::SetReg（非 virtual）を経由して再設定されないリズム音量・ADPCM レジスタを保持する。
リズムの rate × 1024 は u32、PCM と gain の積は i32 の wrapping、符号付き右シフト。
`MUTATE=1 make check-opna-parts` が PCM・status・RAM・timer の列を原本と比較する。
