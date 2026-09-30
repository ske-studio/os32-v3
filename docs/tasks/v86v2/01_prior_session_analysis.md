# 01. 前回セッション残存ログの再解析 (Phase 0-1b)

> 実施日: 2026-08-08
> 対象: 2026-05-15〜05-17 に `archive/feat-vdm` で行われた V86 セッションの残存成果物
> 位置付け: 新規テストを一切走らせずに、前回「計画のみ・未実行」で終わった検証項目を片付ける

---

## 1. 結論サマリ

| # | 発見 | 確度 |
|---|------|------|
| 1 | **INT 1Bh のディスク読み取りは正常に動いていた**（CF=0, AH=00 で完走） | ✅ 確定 |
| 2 | **ローダはシリンダ 0→8 を読んだ後、先頭から読み直す無限リトライに入っていた** | ✅ 確定 |
| 3 | **BDA が起動中に一切更新されていない**（`post_ipl` が `init` とバイト単位で完全一致） | ✅ 確定 |
| 4 | 前日 (05-16) の `pre_dos` では BDA が 9 バイト更新されていた → **05-16 → 05-17 で退行** | ✅ 確定 |
| 5 | 最終的にゲストは **BIOS ROM (FD80) のステータスポーリングループ**に落ちていた | ✅ 確定 |
| 6 | **カーネル起動時に毎回、決定論的なカーネルモード #PF が発生していた** (`addr=0x800166FD`) | ✅ 確定 |
| 7 | その #PF は V86_STATUS §10.1 が「BIOS ROM 実行が原因」の根拠にした行だが、その結論は翌日撤回された。**つまり未解明のまま終わっている** | ✅ 確定 |
| 8 | DOS5 IO.SYS が参照するとされた BDA フィールド (`0481h` MEMSIZ 他) は実際にゼロだった | ⚠️ 整合するが因果は未証明 |

---

## 2. 解析した成果物

### 2.1 `/mnt/c/os32/debug/`（HostDrv）

| ファイル | 日時 | 内容 |
|---|---|---|
| `v86_events.log` | 05-17 19:44 | 409 レコード（16B 固定長リングバッファ） |
| `v86_diag.log` | 05-17 19:44 | 372B。SESSION ヘッダのみで**途中終了** |
| `v86_bda_init.bin` | 05-17 19:44 | セッション開始時の BDA (512B) |
| `v86_bda_post_ipl.bin` | 05-17 19:44 | IPL 実行後の BDA |
| `v86_bda_pre_dos.bin` | 05-16 17:35 | **別セッション**。DOS 起動直前 |
| `v86_bda_exit.bin` | 05-15 08:31 | **別セッション**。終了時 |
| `v86_screen.png` | 05-16 16:17 | 真っ黒 |

### 2.2 `/mnt/c/os32/os32_serial_log.txt`

63,994 行。**1 行目が `> v86 -d /host/dos5_1.fdi`** で、V86 セッションのシリアル出力から始まる。
以降は再起動を挟んでゲーム（スゴロク RPG）のログが追記されている。

### 2.3 使用ツール（`archive/feat-vdm` から取得）

```bash
git show archive/feat-vdm:tools/v86_event_decode.py   > v86_event_decode.py
git show archive/feat-vdm:tools/v86_bda_diff.py       > v86_bda_diff.py
git show archive/feat-vdm:tools/v86_bda_annotate.py   > v86_bda_annotate.py
```

---

## 3. INT 1Bh は動いていた — ただしローダはループしていた

シリアルログの実トレース:

```
[V86] 1st GP: CS:IP=1FC0:015A DS=0000 SS:SP=0000:0286 op=CD 1B
[V86] INT 1Bh AH=D6 AL=90 CL=00 CH=03 DH=00 DL=06 BX=0400 ES=0000 BP=0600
[V86] INT1B done AH=00 CF=0 rc=+0                 ← 成功
[V86] INT 1Bh AH=D6 AL=90 CL=00 CH=03 DH=01 DL=04 BX=1400 ES=1994 BP=0060
[V86] INT 1Bh AH=D6 AL=90 CL=01 CH=03 DH=00 DL=01 BX=2000 ES=1994 BP=0000
   …  CL=01,02,03,04,05,06,07 を head 0/1 で読み進む …
[V86] INT 1Bh AH=D6 AL=90 CL=08 CH=03 DH=00 DL=01 BX=1C00 ES=1994 BP=0000
[V86] INT 1Bh AH=D6 AL=90 CL=00 CH=03 DH=00 DL=06 BX=0400 ES=0000 BP=0600   ← ★ 先頭に戻る
```

- レジスタ規約は **`CL`=シリンダ / `CH`=セクタ長コード(03=1024B) / `DH`=ヘッド / `DL`=セクタ**。
  過去に誤っていた CHS マッピング（`freedos98_boot_debug_report.md` 参照）は修正済みで正しく動いている。
- 全て `CF=0 / AH=00` で成功しており、**ディスク I/O 層は健全**。
- にもかかわらず**同じ範囲を読み直す**。ローダが目的のものを見つけられずリトライしている。

同時に出力されている per-call スナップショット:

```
[S#0] DE=01 UA=00 0530=00 FN=....../...... IV8=FD80:AF06 WK=0000000000000000
```

**`WK`（BDA ワークエリア）が最初から最後まで全ゼロ**。これが次節の裏付けになる。

---

## 4. 【核心】BDA が一切更新されていない — 05-16 からの退行

### 4.1 最終セッション内（05-17）

```
$ python3 v86_bda_diff.py v86_bda_init.bin v86_bda_post_ipl.bin
  No differences between init and post_ipl
--- Total: 0 byte changes across 1 transitions ---
```

IPL が INT 1Bh を 17 回以上発行して完走したにもかかわらず、**BDA は 1 バイトも変化していない**。

### 4.2 前日（05-16）との比較

```
$ python3 v86_bda_diff.py v86_bda_post_ipl.bin v86_bda_pre_dos.bin
  Differences: post_ipl -> pre_dos  (9 bytes changed)
  046Ch  00 -> 17   EQUIP_FLAG  ; 機器構成フラグ
  055Eh  00 -> 01   (FDC 状態 — V86_USERLAND_MIGRATION.md §2.2 が「BDA 更新対象」と明記している番地)
  0564h  00 -> 24
  0565h  00 -> 03
  0567h  00 -> 03
  0568h  00 -> 01
  0569h  00 -> 05
  056Ah  00 -> 03
  056Bh  00 -> 03
```

前日は起動中に BDA が 9 バイト書き込まれていた。最終セッションでは**ゼロ**。

> **注意**: `pre_dos` と `post_ipl` は別セッション・別スナップショット地点であり、
> 厳密な同条件比較ではない。ただし「BDA が更新される経路が存在した」ことは確実で、
> 最終セッションでそれが働いていないことも確実である。

**時系列との対応**: 05-16 → 05-17 の間に入った V86 の最終コミットは
`1e505fe feat(v86): NP21/W準拠HLE全面強化 — DMA全ch仮想化/PIC OCW3 SMM+ローテーション/FDD切替/INT08h改善`。
**このコミットが BDA 更新経路を壊した可能性が高い**（仮説。要検証）。

ローダが「読めているのに進めない」のは、**FDC/ディスクの完了状態を BDA 経由で受け取れていないから**、
という説明と整合する。

### 4.3 初期 BDA の中身（参照用）

```
0413h  0280  MEM_SIZE    640 KB
0480h    03  CPU_FLAG
0484h    03  CPU_TYPE    (03 = i386以上)
0501h    A4  BIOS_FLAG
053Ch    12  CRT_STS
0524h  0502  KB_HEAD }   HEAD == TAIL → キーバッファ空
0526h  0502  KB_TAIL }
055Ch  0001  DISK_EQUIP
0584h    90  BOOT_DEV    (DA/UA = 0x90 = 2HD FDD#1 — INT 1Bh の AL=90 と一致)
05AEh    A0  CONV_MEM    160 × 4KB = 640KB
05CCh-05CFh  D7 1A 80 FD → FD80:1AD7  (ROM ベクタ)
05F8h-05FBh  AF 1A 80 FD → FD80:1AAF  (ROM ベクタ)
```

### 4.4 検証項目 A1 の結論

`docs/tasks/v86/09_msdos_roadmap.md` が DOS5 IO.SYS のハング原因として疑っていた
`0455h` / `0457h` / **`0481h MEMSIZ`** / `0483h` / `0598h` / `05A0h` は、
**実際にすべてゼロのまま**であることを確認した（非ゼロ一覧に現れない）。

ただしこれは「DOS5 がそれを読んでハングした」ことの証明ではない。
**仮説と矛盾しない、という段階**にとどまる。

---

## 5. 最終状態 — BIOS ROM のポーリングループ

`v86_events.log` 409 レコード（GP 373 / INT 36）の後半は、以下の完全な反復:

```
0665:1991  9C  PUSHF
0665:1992  FA  CLI
FD80:0169  E6  OUT imm8, AL
FD80:016D  E4  IN  AL, imm8
FD80:0187  E6  OUT imm8, AL
FD80:0279  FB  STI
FD80:0286  EC  IN  AL, DX     ┐
FD80:02C3  EC  IN  AL, DX     │ ステータスポーリング
FD80:02D0  EE  OUT DX, AL     │ 読み値: AL = 80h → 08h → D0h
FD80:02D8  EC  IN  AL, DX     │
FD80:02E3  EC  IN  AL, DX     ┘
FD80:02C0  CF  IRET
→ 0665:1991 へ戻る（ログ末尾まで同一パターン）
```

- **メモリ破壊ではなく決定論的なループ**。再現性があるので追跡可能。
- 読み値 `80h / 08h / D0h` は µPD765A の MSR らしき値。
  §4 の「BDA が更新されない」と同根の可能性がある（FDC の完了通知が届いていない）。

---

## 6. 【最重要】未解明の決定論的カーネル #PF

シリアルログ中に **30 回以上**、以下がカーネル起動のたびに出力されている。

```
[V86-ASSERT] count=0 esp0_err=0 stk_low=0 regs_err=0 stk_min=0x00007F14 gp#=0x00000001
[V86-PF] addr=0x800166FD err=0x00000002 eip=0x00000000 CS=0x00000008 IP=0x00000000
[V86-ASSERT] count=0 esp0_err=0 stk_low=0 regs_err=0 stk_min=0x00007F14 gp#=0x0000002D
Remote shell active (ESC to exit)          ← 直後に rshell 起動 = カーネル起動シーケンス中
```

### 事実

- **`CS=0x0008` = カーネル CS** → V86 ゲストではなく**カーネル自身**が #PF を起こしている
- `err=0x02` = supervisor / write / not-present
- `addr=0x800166FD` ≈ 2GB。ページングは 16MB しかマップしていない（`PAGING_MAP_SIZE`）ので当然未マップ
- **カーネル起動時の V86 自己テスト**（`731f2be feat: カーネル起動時にV86動作検証テストを実行`）で発生している
- カーネルは**生存して続行**している（rshell が起動する）
- 直前の `[V86-ASSERT]` は `esp0_err=0 / stk_low=0 / regs_err=0` — **自前のアサーションは全て通っている**

### なぜ重要か

この行は `docs/V86_STATUS.md` §10.1 が「トリプルフォルトの根本原因は BIOS ROM 実行」と
結論づけた際の決定的証拠として引用されたものである。
しかしその結論は翌日 `8057a20`（NP21/W ソース調査により「ROM は直接実行が正しい」と判明）で
**全面撤回された**。撤回に伴い、**この #PF 自体の説明も宙に浮いたまま終わっている**。

### 再挑戦での価値

- **毎回のカーネル起動で再現する**＝理想的な最小再現ケース
- ディスクイメージも DOS も不要
- Phase 0-0（フォルト地点で凍結する仕組み）が入れば、
  `emu_read_mem` でカーネルスタックを遡り、**0x800166FD を計算したコードを特定できる**

`0x800166FD` は最上位ビットが立った値であり、正常なカーネルポインタ（`0x0011xxxx`〜`0x0016xxxx`）
とはかけ離れている。ポインタ計算のどこかで符号拡張・ビット混入が起きている可能性があるが、
**現時点では推測であり、証拠はない**。Phase 1 で実測する。

---

## 7. 再実装で使える実測パラメータ（前回の稼働時の値）

```
カーネルロードアドレス        0x110000
#GP ハンドラ (IDT[13])        0x001100C7  sel=0x08 flags=0x8E PRESENT
TSS ESP0                      0x00169290
カーネルスタック              0x001612A0 - 0x001692A0
v86_context の実体            0x00145020  (BSS。static 化済み)
IDT                           base=0x00148DC0 limit=0x7FF
バッキング RAM 物理アドレス   0x500000  (pgalloc 動的確保)
A20 OFF 時のマッピング        VA 0x100000 -> PA 0x500000
IPL ロード先                  1FC0:0000  (1024 バイト)
IVT[00-07]                    すべて 0xFD80xxxx (BIOS ROM)
IVT[08]                       0xFD80AF06
IVT[18]                       0xFD800275
IVT[1B]                       0x003C0000  (ダミー → HLE で処理)
PDE/PTE                       PWU (Present / Writable / User) を付与
```

---

## 8. 次のアクション

| 優先 | 内容 | 対応 Phase |
|---|---|---|
| 1 | **`addr=0x800166FD` の #PF を実際に捕まえて原因を特定する** — 毎起動で再現するので最良の入口 | 0-0 → 1 |
| 2 | BDA 更新経路の退行を確認する（`1e505fe` の差分を読む） | 0-2 の入力 |
| 3 | FDC 完了通知（MSR / BDA `055Eh`）の経路を、素の NP21/W の挙動と突き合わせる | 0-1 |
| 4 | Ys 系は DOS を通らないため、上記 2・3 の影響を受けない可能性がある。**第一目標は Ys なので、DOS 系の深追いはしない** | — |

---

## 9. 参照

- `archive/feat-vdm:docs/V86_STATUS.md` §10.1（撤回された「ROM 実行が原因」説）、§12（§BUG-CTX）
- `archive/feat-vdm:docs/tasks/v86/V86_HANDOVER.md` §3（`#UD (EIP=0x0F)`）
- `archive/feat-vdm:docs/tasks/v86/09_msdos_roadmap.md` §9.2（DOS5 IPL ハング）
- `archive/feat-vdm:docs/tasks/v86/11_gaps_and_verification.md` A1〜A6（本文書で A1 に決着）
- `git show dff9ebf~1:docs/tasks/v86/_archive/freedos98_boot_debug_report.md`（CHS マッピング誤りの記録）
