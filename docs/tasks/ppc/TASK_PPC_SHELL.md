# TASK_PPC_SHELL — PowerPC の製品シェルの起動と保存先

> 2026-11-20 の見直しで x86 の形に改めた ([DESIGN_X86_AUDIT](../../decisions/ppc/DESIGN_X86_AUDIT.md))。

> 上位: [TASK_PPC_KBOOT](TASK_PPC_KBOOT.md)

> 状態: **実装中 (2026-11-20)** — W7 の USER sh 起動は main 1a136abc8、2d102e306 は stdio 試験の修正。製品シェルの統合は PPC_SHELL_ALIGN_X86_DESIGN に従う。

## 0. 製品の起動と媒体

W7 で USER アプリとしての sh.bin の起動を確認した。製品では 起動ループが外部シェルを owner 1 のシェル枠にロードする。終了時に資源を回収し、gui_take_next_shell で次のシェルを選んで同じ枠で起動する。
PPC_SHELL_ALIGN_X86_DESIGN の段 A・B・C・E は main 未収録。共通起動ループ D1〜D3 は main 844839af0。常駐 CLI と旧 kboot の 5 像は、x86 に寄せる作業の最後に外す。

## 1. W8 と W9

インストール媒体は読み取り専用の RAM root、製品の root は HDD の ext2 とする。 W8 の保存先 /host は共通 VFS と link の SFS CALL に接続し、再起動後の読み戻しを確認する。
W9 は子の終了値、シェル終了時の回収、次のシェルの起動を製品のループから確認する。

初期の設計は [PPC_SHELL_DESIGN](../../decisions/ppc/PPC_SHELL_DESIGN.md)、USER ABI は [PPC_SHELL_EXEC_DESIGN](../../decisions/ppc/PPC_SHELL_EXEC_DESIGN.md)、W8 の媒体の検証は [PPC_SHELL_W8_DESIGN](../../decisions/ppc/PPC_SHELL_W8_DESIGN.md) に記録した。

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
| W7 (S-C、既存のシェルを USER の sh.bin として起動) | 済 (QEMU、sc_run.py、7450・7455 の LAN)。設計は [PPC_SHELL_SC_DESIGN](../../decisions/ppc/PPC_SHELL_SC_DESIGN.md) |
| W6 (PC-98 の FD 起動から共通の RAM root) | 計画確定 (2026-11-12、[PPC_RAM_ROOT_W6_PLAN](../../decisions/ppc/PPC_RAM_ROOT_W6_PLAN.md))。**ユーザー決定 (2026-11-12): 案 B** (kernel は ISO の中に 1 回だけ)。共通の部品の段 C1〜C4 は PowerPC 側が先に出す、PC-98 の FD 側の段は本線が起こす |
| W8 (保存先) | 初期の S-D は /host の保存を QEMU で確認済み。HDD の ext2 root からの製品起動・保存は未受入。共通 VFS/link 上の /host を製品シェルで受け入れる。 |
| W9 (製品起動) | [ARCH_BRINGUP_ORDER](../../decisions/ppc/ARCH_BRINGUP_ORDER.md) に従い、owner 1 の起動・回収・再起動を確認する。 |
| FB 文字端末 (G4 の画面に sh、共通の部品) | **QEMU で受入、main e74a0cf1 (2026-11-13)**。設計 [PPC_FBCON_DESIGN](../../decisions/ppc/PPC_FBCON_DESIGN.md)。段 A (x86 CUI の回帰の試験)・B (resident のフォント・KCG1 の本体)・C (全画面の採取)・D (gfx_console・OF_FB=6)・E (of_fb の後端・provider・color@/color!)・F (共通の fbcon)・G (東雲 16 の KCG1・RAM root)・H (PPC の像への結線・shell_putchar の mirror)・J (QEMU mac99 `-g 800x600x8` の画素の受入 `ppc-fb`、7450・7455) 。x86 の生成物は段 D の分だけ変わる (本線 PM 承認 11-13)。H の shell_putchar の写しは PowerPC だけ (CONSOLE_PUTCHAR_MIRROR、x86 の console.o は main と同じ)。G4 実機 (R9) は未。台帳の PPC-USB-LAN-FLAKE は 2026-11-13 に閉じた: 台本 tools/ppc/usb_run.py が `echo discarded` の全字の echo を待たずに LAN OPEN していた競合 (ゲストは各字 1 回ずつ配達、取り消しの後に届いた字が次の行に残った)、OPEN の前に全字の echo を待つ形に直した。`usb_run.py --lan --mutate` を PPC_FB=0・1 とも 3 回続けて GREEN、待ちを外すと 2 回とも `eecho`・`decho` で RED (証拠 `~/os32-tmp/run/usb-flake/`) |
| フォントの LZ4 (KCG1 flags=1) | **QEMU で受入、main 0f115f53 (2026-11-14)**。製品フォント: flags=1・187,980B・SHA-256 `36eb1719dd6c0c18824c5746c69bcff27ac1f93d04a72c6ab8907d529dff9451`、生成: Python lz4 4.0.2+dfsg。設計 [PPC_KCG_LZ4_DESIGN](../../decisions/ppc/PPC_KCG_LZ4_DESIGN.md)。console_route は段 I へ送った (本線 PM 決定 11-13) |
