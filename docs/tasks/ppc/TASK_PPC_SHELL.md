# TASK_PPC_SHELL — PowerPC 移植: シェルが動くまで (LAN の端末、RAM の root、開発用の小さな CLI → USER アプリ → 製品のシェル)

> 上位: [TASK_PPC_KBOOT](TASK_PPC_KBOOT.md)

> 状態: **実装中 (2026-11-12)** — 中間の到達点 S-A は main。最終の到達点 S-B は全段実装・独立レビュー済みで、**QEMU 7450・7455 で受入に到達** (LAN 端末から /bin/hello.bin ok → exit=7、tools/ppc/sb_run.py)。S-B の段は全部 main (61276d39、2026-11-12)。次は W6 (PC-98 の FD 起動から共通 RAM root、本線) と W7 (sh.bin)。設計は [PPC_SHELL_DESIGN](../../decisions/ppc/PPC_SHELL_DESIGN.md) (S-A) と [PPC_SHELL_EXEC_DESIGN](../../decisions/ppc/PPC_SHELL_EXEC_DESIGN.md) (S-B、実装で決めたことは末尾の「確定の後の追記 1」)。

## 0. ユーザー決定 (2026-11-11)

- 目標: 「シェルが動くまでの計画と詳細設計を作りましょう」。
- PM の 4 問への回答:
  1. 受入は 2 段 — 中間 S-A = カーネル側の CLI がプロンプトを出し内蔵コマンドを 1 行実行して戻る、最終 S-B = 外部の USER アプリを起動し終了して戻る。
  2. 入出力は LAN が主 (ホストの道具から LAN 越しの双方向の端末)、ESCC の serial は QEMU の補助。画面 (FB) とキーボードは後の段。
  3. root はメモリ上の読み取り専用 image (起動像に同梱)。「これは最終的にPC98のFDブートでもこの方式にしたい。これはメインのPMに伝えて」— image の形式・RAM の block device・mount の口は CPU・機種に依らない共通の部品にする (本線の PM に伝達済み)。
  4. 最初の版は soft-float・AltiVec なし、module は静的 link、コマンドは最小で PC-98 固有は外す。
- 追加: 起動の後は起動媒体を読まない (FD・CD を抜いても動く)。書き込みが要る — メモリ上の書き込み用の領域 (/tmp の小さな ramfs、再起動で消える) を S-A に入れ、残す物は書ける媒体を後から mount する (PowerPC は Host Services の LAN 版、PC-98 は既存の FD/HDD/Host Services)。
- S-A の書き込みは `echo text > /tmp/f` の 1 形 (「> ファイル」以外の redirect・パイプなし) と cat の読み戻し。
- S-B の後、既存の OS32 のシェルを USER アプリ sh.bin として PowerPC 向けに組む。「小さなCLIの経路は最終的に通らないように。ただし、他のアーキテクチャ開発時に同じ開発経路を取れるようにはしておきたい。」— 製品の起動は root の mount の後に kernel が sh.bin を直接起動する。小さな CLI は make 変数 KBOOT_SHELL=1 のときだけ組む開発用の経路で、kernel/cli/ は CPU・機種に依らない。

## 1. 詳細設計 (確定) の要約

全文は [PPC_SHELL_DESIGN](../../decisions/ppc/PPC_SHELL_DESIGN.md)。最終の到達点 S-B (USER の ABI・exec の facade) は [PPC_SHELL_EXEC_DESIGN](../../decisions/ppc/PPC_SHELL_EXEC_DESIGN.md)。

- LAN の端末: PLN1 に端末の型 4 つ (OPEN・IN・OUT・STATE)。出力は「届く分だけ + 欠落数」、入力の欠落はその行を実行しない。所有は lan_terminal.c、送信予算は LOG と OUT で計 4、STATE は枠の外。
- root: ISO9660 の読み取り専用 image を起動像に同梱 (tools/mkroot_image.py)、共通の口 include/boot_root.h と RAM の block device drivers/block/ram_ro.c (cd0 として登録)。名前の表は platform/powermac/root_boot.c が mount の前に登録。/tmp に新しい小さな ramfs (fs/ramfs/)。
- 開発用の小さな CLI: kernel/cli/resident_cli.c (help・echo・cat と `> ファイル`)、端末と poll は resident_cli_ops の口で受ける。
- 段: W0 (宣言) → W1 (部品) → W2 (接続) → W3 (PPC の起動に結線・閉包リンク・S-A の通し受入) → W4 (S-B の詳細設計) → W5 (S-B) → W6 (PC-98 の FD 起動から共通 RAM root、本線) → W7 (sh.bin、製品の起動) → W8 (書ける媒体の後 mount) → W9 (移植の手順書 PORT_FLOW に立ち上げの順)。
- 共通の場所の部品 (include/・fs/・drivers/block/・init/・kernel/) に触る段は、出す前に本線の PM に知らせる。include/memmap.h と include/config.h は変えない。

## 2. 段の状態

| 段 | 状態 |
|---|---|
| 棚卸し (sol) | 済 (2026-11-11、~/os32-tmp/run/ppc-shell/SURVEY.md) |
| 計画と S-A の詳細設計の往復 | 済 (2026-11-11、r1 → r2 → final) |
| W0 (宣言)・C2 (fs/vfs_fd.c の 1 行) | main (7e255783)。fs/vfs.h の VFS_DEV_RAM は x86 の生成物が変わる (CD module の契約値の入力) ので、iso9660_mount に rdN を受けさせる x86 の変わる段へ移した |
| W1 (A06+A07 LAN の端末、A08 ホストの端末モード、A10 RAM の装置 rd0、A11 root の image を作る道具、A12 開発用の小さな CLI、A27 /tmp の ramfs) | 実装・独立レビュー済 (2026-11-11、統合の枝 wt/ppc-shint2)。x86 のビルドには入らない。PPC の像は lan_wire.c (端末の型を通す) の分だけ変わる |
| W2〜W3 (S-A) | 実装・独立レビュー済 (2026-11-11、作業の枝 wt/ppc-shw3、X1 の後に取り込み)。**中間の到達点 S-A に QEMU で到達** (7450・7455、CD を eject した後に LAN の端末から echo・診断の CLOCK・cat /etc/motd・echo a > /tmp/f・cat /tmp/f、tools/ppc/shell_run.py)。開発用の像は KBOOT_SHELL=1、今の像は LAN の端末だけ増える |
| X1 (★ fs/vfs.h の VFS_DEV_RAM、iso9660_mount が rdN を受ける) | 実装・独立レビュー済 (wt/ppc-shint3)、本線で HAL v2 の I1 と一緒に NP21/W の受入待ち |
| W4 (S-B の詳細設計の往復) | 済 (2026-11-11、PPC_SHELL_EXEC_DESIGN)。exec の facade の段 (F) は HAL v2 の I1 が main に入った後の main を基点 |
| W5 (S-B) | 全段 (facade F01〜F21・生成器 F10/F14/F15・B01〜B36、B21 はやめた) 実装・独立レビュー済 (2026-11-12)。**QEMU で受入に到達** (sb_run.py、7450・7455、LAN 込み)。B36 で docs/09_exec.md に PowerPC の USER の ABI |
| W7 (S-C、既存のシェルを USER の sh.bin として起動) | 詳細設計は確定 (2026-11-12、[PPC_SHELL_SC_DESIGN](../../decisions/ppc/PPC_SHELL_SC_DESIGN.md))。製品の経路は第 3 の像 KBOOT_SH=1、既定の診断の像と KBOOT_SHELL=1 の像は不変。段 N01 (USER の入れ子の先行確認) から |
| W6・W8・W9 | 未着手 (W6 は共通の部品の段と PC-98 の FD 側の段に分け、後者は本線が起こす) |
