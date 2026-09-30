# 10. HDD イメージからの DOS ブート — 完了 (2026-08-12)

VDM を「インストール済みプログラムのテスト環境」にするための HDD ブート対応。
計画・実測・設計・制限一覧の正本。

**結果**: `v86 -b /host/dos5hd.nhd` で MS-DOS 5.00A が NHD 単体から `A>` まで
起動し、EXE 実行・ファイル書き込み・再起動を跨いだ永続化・FDD 併載
(`v86 -b <nhd> <fdi>` で B: ドライブ) まで動作。ブート全 154+ INT 1Bh
コールが失敗ゼロ。FDD 回帰 (dos5.fdi / Ys.D88 / v86 -t / -d) 全通過。

関連: [08_dos5.md](08_dos5.md) (FDD ブートまでの経緯) / [05_disk_bios_plan.md](05_disk_bios_plan.md) (FDD INT 1Bh)

## 1. スコープ

- INT 1Bh の SASI/IDE 規約 (DA/UA 0x80) を実装し、NHD/HDI イメージから DOS をブートする
- WRITE (AH=05h) を HDD/FDD 両方に実装する
- DA/UA→スロット表で HDD + FDD の 2 ドライブ同時マウント
- KAPI に `v86_boot2(boot_img, second_img)` を追加 (append-only)

## 2. Phase 0 実測 (2026-08-11)

### 2-1. 手順

1. fork に `/api/sasitrace` を追加 (`sasibios_operate()` の入口レジスタ+戻り AH を
   リングバッファ 4096 件に記録。`np21w-src/src/win9x/aidebug/aidebug_sasitrace.c`)
2. 素の NP21/W (IDE, `AIDEBIOS=true`) で空 NHD (40MB: C=600 H=8 S=17 SS=512) を
   IDE#1 に接続し、MS-DOS 5.00A FDD (`dos5.fdi`) からブート
3. FORMAT.EXE → 固定ディスク → 初期化 → 領域確保 (39MB, 先頭シリンダ 0001,
   システム転送する) → パーティション「アクティブ / BOOT 可」
4. `copy a:\*.* c:\dos` (30 ファイル)、AUTOEXEC.BAT / CONFIG.SYS 作成
5. FDD を抜いて NHD 単体ブート → `A>` 到達を確認
6. ブート全体の INT 1Bh 列と BDA を採取

イメージ: `C:\os32\dos5hd.nhd` (作業用) / `C:\os32\dos5hdmaster.nhd` (マスター、
セッションで壊れたらここから復旧)。プロファイル INI: `C:\os32\dos5hd.ini`
(HDD1FILE=dos5hd.nhd, FDD は引数で渡す)。

生データ: [data/trace_boot.json](data/trace_boot.json) (ブート全 154 呼び出し) /
[data/trace_write.json](data/trace_write.json) / [data/bda_hddboot.json](data/bda_hddboot.json)

### 2-2. ブート → A> の INT 1Bh 統計 (全 154 呼び出し)

| AH | AL | 回数 | 意味 |
|----|----|----|------|
| 06 | 00 | 126 | READ **LBA モード** (支配的) |
| 06 | 80 | 4 | READ CHS モード (ROM/IPL 期のみ) |
| 84 | 80/00 | 14 | SENSE 拡張 (ジオメトリ問い合わせ) |
| 14 | 80 | 3 | SENSE 変種 (bit4。ジオメトリ返却なし、ret=0x0F だけ見る) |
| 83/03 | 00 | 3 | INIT |
| 8E | 80/00 | 4 | 未サポートコマンド。**ret=0x40 (CF=1) が正、DOS は無視して続行** |
| 05 | - | 0 | WRITE はブート中ゼロ |

### 2-3. 呼び出し列の要点

```
#0-4   ROM: INIT(83/03), 8E(→40), SENSE84(→0F)
#5-7   ROM: READ CHS cyl=0 head=0 sect=0..2 (sect は 0 起算!) → 1c00:0000
#11    SENSE84 の戻り: BX=0200(512B) CX=0258(600cyl) DH=08 DL=11(17spt)
#12-30 DOS パーティションスキャン: READ LBA 0/1 を SENSE を挟んで反復
#31    READ CHS cyl=1 sect=0 BX=0400 → 1f80:0000 (パーティションブートセクタ)
#33-34 IO.SYS ロード: READ LBA 154 (BX=0200) → READ LBA 346 BX=F800 (62KB 一括!)
#36-   MSDOS.SYS/COMMAND.COM/DOS ファイル: すべて LBA モード、BX=0400 (1KB クラスタ)
       ときどき BX=BC00 (47KB) の大転送
```

書き込み (`echo x > a:\wtest.txt`) は **AH=05 LBA モード BX=0400** ×5
(ディレクトリ LBA154 / FAT LBA138,146 / データ LBA3162)。

### 2-4. HDD ブート完了時の BDA (実測)

| アドレス | 値 | 意味 / VDM での扱い |
|---|---|---|
| 0x0584 DISK_BOOT | **0x80** | ブート DA/UA。VDM も 0x80 を書く |
| 0x055C DISK_EQUIP | **0x0103** | bit8=SASI#1 + bit0,1=2HD FDD 2 台 (メディア無しでも立つ)。VDM は 0x0100 (FDD 併載時 0x0101) |
| 0x0482 EQUIPS | **0x00** | SASI 用ビットマップは IDE 経路では未使用。**触らない (計画どおり)** |
| 0x0460-0x047F | **全ゼロ** | SASI パラメータ域も未使用。**触らない (計画どおり)** |
| 0x0480 | 0x83 | bit7=IDE BIOS。VDM は落としたまま試す (IDE ポートは拒否のため) |
| 0x0501 | 0xA4 | 下位 3bit=4 → 640KB (VDM と同じ) |
| 0x0401 / 0x0594 | 0x70 / 0x0001 | 実機は拡張メモリあり。VDM は 0 のまま (HIMEM は自分で降りる) |
| 0x05BA-BB | 01 01 | IDE 接続ビットマップ。実機 BDA から引き継がれる |

### 2-5. 実装への確定事項 (sxsibios.c 1:1 + 実測で裏取り)

1. **LBA モードが本命**: AL bit7=0 → `pos = (DL<<16)|CX`。DOS 5 の通常 I/O は全部これ
2. CHS モード (AL bit7=1) は ROM/IPL 期のみ: CX=16bit シリンダ, DH=ヘッド,
   **DL=0 起算セクタ**
3. BX はバイト数。**0xF800 (62KB) の実績あり**。0 は 0x10000 (64KB)。
   転送は pos++ の線形でシリンダ跨ぎ自由、64KB 境界エラーなし
4. SENSE AH=84h は BX=セクタ長/CX=シリンダ/DH=ヘッド/DL=SPT を返す。
   AH=14h 等の変種はジオメトリを書かず ret だけ返す
   (nibble=4 はどれも sasibios_sense に落ちるが、レジスタ設定は AH==0x84 のときだけ)
5. 戻り AH: SPT=33 の SASI 標準ジオメトリ以外は **0x0F (CF=0)**。NHD (SPT=17) は常に 0x0F
6. CF は ret >= 0x20 のときだけ立てる (0x0F は成功扱い)
7. 未サポート nibble (0/2/8-C/E) は 0x40。**AH=8E が実際に飛んでくる**が失敗で正しい
8. INIT (nibble=3) は成功を返す (BDA 0x55C の SASI ビットを立てる)
9. IPL は **LBA 0 から 1024 バイト**を 1FC0:0000 へ (`bios1b.c boot_hd()`)
10. WRITE (AH=05) も LBA モードで飛んでくる。FAT 更新は BX=0400 単位

## 3. fork 側の変更 (Phase 0-b)

- `src/win9x/aidebug/aidebug_sasitrace.c/.h` — リングバッファ + 読み出し
- `src/bios/sxsibios.c` — `sasibios_operate()` をラップして記録
- `src/win9x/aidebug/aidebug_api.cpp` — `GET /api/sasitrace?from=N&max=M` /
  `?clear=1`
- `src/win9x/aidebug/aidebug_keys.cpp` — **PC-98 配列のキーマップ修正**:
  `\` (YEN 0x0D) と `_` (SHIFT+RO 0x33) を追加、`*` を SHIFT+COLON (0x27) に、
  `+` を SHIFT+SEMICOLON (0x26) に修正。これで `copy a:\*.* c:\dos` が
  `/api/key` の text= でそのまま打てる

## 4. 運用ルール

- **マスター/コピー運用**: `dos5hdmaster.nhd` がマスター。VDM セッションの
  打ち切り (5 分上限 / ホットキー) は DOS のバッファフラッシュを待たないため
  FAT 破損があり得る。テストは必ずコピーに対して行う
- NP21/W 側で dos5hd.nhd をブートするとき: `np21x64w.exe C:\os32\dos5hd.ini`
  (FDD を挿すときは第 2 引数に .fdi)。本番 INI (`np21x64w.ini`) は触らない

## 5. 実装 (2026-08-12 完了)

### 5-1. 変更点

| ファイル | 内容 |
|---|---|
| `drivers/loop_dev.c/.h` | `LOOP_FMT_NHD` + `attach_nhd()` (マジック照合 + 乗算オーバーフロー対策)、`loop_dev_read_lba()/write_lba()` (境界チェック内蔵、D88 非対応)、`loop_dev_is_writable()`、アタッチを O_RDWR (失敗時 O_RDONLY フォールバック + 書き込み不可フラグ) |
| `kernel/v86_bios.h/.c` | ドライブ表 `v86_drives[2]` (ブート=slot0 / 2 台目=slot1)、`daua_to_slot()` (**AL=0x00 は 0x80 の別名**)、`bios_int1b()` を devtype で `bios_fdd_int1b()` / `bios_sasi_int1b()` に分岐、BDA を FDD/HDD で分岐 (0x584=ブート DA/UA、0x55C=装備ビットマップ)、FDD 側は `cur_fdd_slot` でスロット可変に |
| `kernel/v86_bios.c` (SASI) | `sasi_pos()` (CHS 0 起算 / LBA (DL<<16)\|CX)、READ/WRITE (BX=0→64KB、pos++ 線形、毎セクタ guest_range_ok)、SENSE 84h (ジオメトリをレジスタで返す、非 SASI ジオメトリは 0x0F CF=0)、INIT (BDA 0x55C 更新)、他 nibble は 0x40。CF は ret>=0x20 のみ |
| `kernel/v86_bios.c` (FDD) | `disk_write()` — AH=05h。bios1b.c case 0x05 準拠 (bit4 シーク / 0x70 プロテクト / 0x20 64KB 境界 / 0xC0 転送しきれず / 端数は RMW)。FDI/RAW のみ、D88 は 0x40 |
| `kernel/v86.c` | `v86_boot2(boot, second)` 新設 (v86_boot は薄いラッパ)。HDD ブートは **LBA 0 から 1024B** を 1FC0:0000 へ |
| `tools/kapi.json` | version 39、`v86_boot2` を append |
| `programs/cmds/v86.c` | `v86 -b <image> [second]` |

### 5-2. 検証記録

- VDM disklog とブート実測列が一致 (LBA READ 支配、48KB/62KB 大転送、
  WRITE はディレクトリ/FAT/データの 1KB 単位) — 全コール res=00
- 書き込み: ゲストで `echo > vdmtest.txt` → ホスト側 NHD ファイルに
  バイト反映 (grep で確認) → 再ブート後も残存。FDD (dos5.fdi) も同様
- `v86 -d` は 256B/セクタ前提のテストなので 1024B の FDI は仕様上
  0x8005 で弾かれる (回帰ではない)。Ys.D88 で OK
- セッション終了後の OS32 は健全 (`ver` 応答)

## 6. 制限とオーバーヘッド (実測 2026-08-12)

### 動くもの
- リアルモード・XMS/EMS 不要・コンベンショナル 640KB 内のプログラム
- INT 21h ファイル I/O 読み書き (HDD/FDD とも)
- HIMEM/EMM386 入り CONFIG.SYS (自分で「使用できません」と降りて下位ロード)
- DOS の TIME/DATE — **RTC (カレンダポート 0x20 パススルー) ベースなので
  ずれない** (実測: ホスト 63 秒経過で DOS 62 秒。PIT ch0 未仮想化の影響は
  INT 08h ベースの常駐物にだけ出る)

### 動かないもの
- XMS/EMS 必須のプログラム (A20=0xF2 拒否・拡張メモリ申告 0)
- IDE (0x640 系) / FDC (0x90 系) ポート直叩きツール (BDA の IDE ビットも
  落としてある)
- 5 分超のセッション (V86_TICK_LIMIT)
- INT 08h タイマ割り込み頼みの常駐物 (初期 IMR で IRQ0 マスク)
- D88 への書き込み (FDI/RAW/NHD/HDI のみ)

### 性能
- DOS `COPY` 968KB (30 ファイル、HDD→HDD 読み書き): **約 13 秒 ≒ 75KB/s**。
  経路は INT 1Bh HLE → loop_dev → VFS → hostdrv ハイパーコール (1 転送=BX
  バイトずつ vfs_read/write)。実用上はブート 5 秒・EXE 起動即応で、
  テスト環境として十分
- ブート → `A>`: 実測 5 秒未満 (素の NP21/W と体感同等)
- V86 モニタ自体のオーバーヘッドは従来実測 6-13% から変化なし
  (I/O ポリシーに変更なし)

### 運用
- **マスター/コピー運用**: セッション打ち切りは DOS のバッファ
  フラッシュを待たない。`dos5hdmaster.nhd` から `dos5hd.nhd` を作り直せる
- テスト対象プログラムの持ち込み: NP21/W 側で NHD をブートして COPY するか、
  FDD イメージを `v86 -b <nhd> <fdi>` で B: として渡す
