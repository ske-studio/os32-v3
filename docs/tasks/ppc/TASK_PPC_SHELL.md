# TASK_PPC_SHELL — PowerPC 移植: シェルが動くまで (LAN の端末、RAM の root、開発用の小さな CLI → USER アプリ → 製品のシェル)

> 上位: [TASK_PPC_KBOOT](TASK_PPC_KBOOT.md)

> 状態: **設計中 (2026-11-11)** — 計画と中間の到達点 S-A の詳細設計は確定 ([PPC_SHELL_DESIGN](../../decisions/ppc/PPC_SHELL_DESIGN.md))、実装待ち。最終の到達点 S-B (USER ABI・syscall・exec・OS32X の BE の読み) は別の往復で詳細設計する。前提は LAN ([TASK_PPC_LAN](TASK_PPC_LAN.md)、main)。

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

全文は [PPC_SHELL_DESIGN](../../decisions/ppc/PPC_SHELL_DESIGN.md)。

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
| W0〜W3 (S-A) | 未着手 |
| W4〜W9 | 未着手 |
