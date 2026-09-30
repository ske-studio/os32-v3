# コーダー1 タスク: M1 リング3 土台の実装

> 発行: PM (2026-09-03) / 状態: **完了記録 (2026-09-03)**
> 担当: コーダー1 (Opus 5)
> 前提: M0b ([TASK_coder1_M0b_privileged.md](TASK_coder1_M0b_privileged.md)) 完了
>       = `check_privileged.py --strict` が exit 0
> 設計: [M1_RING3.md](M1_RING3.md) (これが正典。本書は進め方と gate)
> 契約: [CONTRACTS.md](CONTRACTS.md) C1/C2/C7 を厳守

## ゴール

1 つのユーザプログラムを CPL=3 で起動し、正常終了でシェルに戻す。かつ
不正メモリアクセスがカーネルを巻き込まず #PF で止まる。KAPI 配線 (トランポリン)
は M2 なので、M1 の検証は KAPI をほぼ使わない最小プログラムで行う
(M1_RING3 §6)。

## 排他と禁止 (CONTRACTS C7)

- **あなたの排他ファイル**: `kernel/gdt.c` `kernel/paging.c` `kernel/pgalloc.c`
  `kernel/idt.c` `exec/exec.c` `sdk/gen_kapi.py` `kernel/*ring3*` (新規)
- **触ってはいけない**: `userland/rust/libos32gui/**` (コーダー2 排他)、
  `tools/check_privileged.py` `userland/tests/faultprobe/**` (PM)
- 定数 (セレクタ値・番地) は CONTRACTS C1/C2 のとおり。**勝手に変えない**。
  変更が要るなら PM に相談 (他レーンのビルドが黙って壊れる)

## 実装順と各段の gate

各段は前段の検証 OK 後に進む。検証は別セッションの検証モデルが
`os32-cycle` で実施し `RESULT` を返す。**あなたは実機を叩かない** (実装のみ)。
Opus 5 なので M1a-M1c はまとめて実装してよいが、**M1b の PD 共有だけは
単独で gate する** (最大の事故要因)。

### M1a — GDT 拡張
- `kernel/gdt.c`: `GDT_ENTRIES` 4→6、idx4=`USER_CS`(0x23,access 0xFA)、
  idx5=`USER_DS`(0x2B,access 0xF2)。TSS は idx3 固定 (動かさない)
- セレクタ定数を公開 (idt.h か新規ヘッダ。`KERNEL_CS`/`KERNEL_DS` に倣う)
- gate: `os32-cycle deploy` で従来どおり起動・`ver` 応答 (CPL=0 に影響なし)

### M1b — PD 複製 pgalloc API (最難所, 単独 gate)
- `kernel/pgalloc.c` / `paging.c`: 新 PD を 1 枚作り、**カーネル帯域
  (0x100000-0x3FFFFF) と MEM_SHM_BASE の PDE を共有**、`0x400000` 帯だけ
  アプリ固有 PT に差し替える API (CONTRACTS C2)
- `v86_mem.c` の PDE 張り替え・巻き戻しの手法を参照 (M1_RING3 §3.3)
- gate (**V1 最優先**): CPL=0 のまま CR3 を新 PD に載せてもカーネルが動き
  続ける。全 PD でカーネル帯域が同一物理を指す。ここを誤ると M1c 以降全滅

### M1c — 0x400000 帯を USER マップ + テストプログラム配置
- アプリ PD の `0x400000` 帯とユーザスタックを `RW+USER` に。VRAM 0xA8000 も
  `RW+USER` (C2)
- CPL=3 検証用の最小プログラム (VRAM に 1 文字書いて終了) を用意

### M1d — CPL=3 往復
- `exec/exec.c`: `iret` で CPL=3 に降りる (M1_RING3 §4.1)。
  **`v86_entry.asm` の iretd フレーム構築を流用** (SS/ESP/EFLAGS/CS/EIP)
- `sys_exit` 相当を CPL=3 から呼べる `int` として先行実装 (M2 の前倒し最小版)
- gate (V2/V3): CPL=3 から VRAM に書ける・カーネル帯域には書けない。
  CPL=3 実行中の PIT/KBD 割り込みがカーネルに正しく入る

### M1e — フォールトで #PF、カーネル生存
- #PF ハンドラが CPL=3 からのフォールトでアプリを kill してシェルに戻す。
  **`fault_kill_count` (u32) を増やす** (CONTRACTS C6。PM の faultprobe と
  os32-cycle fault-test がこれを読む)
- gate (**V4 = M1 の存在意義**): `userland/tests/faultprobe.bin`
  (PM 作成済み) の case 1/2/3 でアプリだけ死に、カーネル生存・シェル復帰。
  `os32-cycle fault-test` は PM がこの段で追加する

## 完了条件 (M1)

- CPL=3 の最小プログラムが起動→VRAM 描画→正常終了でシェル復帰
- faultprobe case 1/2/3 が「アプリ死・カーネル生存」(V4)
- V86/DOS 起動が壊れていない (V5, vdm-v2 で回帰確認)
- `make check` exit 0、`make clean && make all` 通過

## PM への連絡

- CONTRACTS 変更が要るとき / 排他外ファイルへの配線が要るとき
- M1e 着手時 (PM が `os32-cycle fault-test` と `fault_kill_count` 読取を用意)
- 各段完了時 (PM がレビュー → 検証モデルに実行指示 → 次段 gate)
