# OS32 v2.1 リリースノート

> 状態: **確定** (2026-09-29、タグ `v2.1`)。実機 PC-9821Ra266 で CHECKLIST_2026-09-26 の手順 1〜7 (判定の記録は os32 リポジトリの `docs/archive/realhw_v21/CHECKLIST_2026-09-26.md`) を満たし、
> Ra266 の PEGC 640x480 の修正 ([TASK_PEGC_RA266_TIMING](archive/realhw_v21/TASK_PEGC_RA266_TIMING.md)、[ROADMAP](ROADMAP.md) §1.5) を**実機の画面を見ずに確かめられる条件**で受け入れた (ユーザー決定 2026-09-29、v3 を遅らせないため)。
> 前の版: `v2.0` (2026-09-03、KernelAPI v39)。この版: KernelAPI **v68**。

v2.1 は **v3 へ分岐する前の区切り**。v2.0 (リング 3 ネイティブ) の上に GUI シェルを載せ、**実機 PC-9821Ra266 で FD 起動・CD からの
HDD インストール・HDD 起動まで**通した版。

## 1. 実機で動くようになったもの

| 分野 | 内容 |
|---|---|
| 起動 | FD (2HD 1232KB / 1.44MB の生イメージ) で起動、PIT はクロックを判定して分周 (2.4576MHz 系で 23% 速かったのを直した)、キーボード 8251 のコマンド語 (0x16)、ビープの止め方 (0035h を全体で書かない) |
| FD | シーク・回転の最悪値から時間上限、FRY、1MB 超への DMA (0439h)、トラック読みと 8 セクタの LRU |
| HDD | 標準の PC-98 区画表で OS32 の区画を作る `cdinst` / `install` (段 1・2)、他 OS の区画や壊れた表を消して入れる **ERASE**、HDD の IPL とローダ、イメージの CRC 検査 (VK32) と `ver` の `Commit` / `Image CRC` |
| CD | ATAPI の READ(10) を 16 セクタずつ、iso9660 のパス・ディレクトリのキャッシュと先読み (cdinst が約 10 分かかっていた件)、マスター/スレーブの両方から媒体のある装置を選ぶ、待ち上限を秒単位に (スピンアップでリセットしない、SRST 31 秒)、死んだバスはすぐ失敗 |
| シリアル | 115200bps までの整数分周、SerialFS (シリアル越しの `/host`) と `hsync --root` — **FD 起動から HDD を更新**できる。2026-09-29 に **HDD 起動のまま** 135b6b5 → 44bd0fe を更新 (カーネル 59 秒、`/sys` 63 秒、全体 21 分) |
| PCI | 列挙と `lspci -v` (82557 LAN、チューナーの識別) |
| 診断 | 起動ログ `/var/log/boot.log` (前回分は `.1`)、`kbdstat -w` (キーの make/break を 1 行ずつ)、`v86 -g` (実機の BIOS が 480 ラインで書く GDC の値を記録) |

## 2. GUI シェル (1.1〜1.3)

gshell (WM)、libos32gui.shlib、PEGC / Cirrus のバックエンド、ファイラー、エディタの GUI 版、FEP、設定レジストリ (settings.db)。
v2.1 で足したもの: **キーボードだけで GUI を操作する** (Windows 98 と同じ割り当て — CTRL+ESC、GRPH+TAB、GRPH+f･4、GRPH+SPACE の窓メニュー、
SHIFT+f･10、カナ ON のマウスキー。PC-98 の GRPH = Alt)。GUI 1.4 の残りのうち **About** (`ver` と同じ内容を表示、Start → Programs の `about.bin`) と **R2 計測** (PEGC 640x480・NP21/W の Cirrus 640x480 で gui_gate v11/v12g1/v12g4 が通る) は 2026-09-29 に入った。
ドラッグ枠の線が他の窓に残る件・前面を替えたとき旧前面のタイトルがアクティブ色のまま残る件も直した (gshell が損傷を申告していなかった。重なり順はアプリ → 枠 → モーダル → タスクバー → メニュー → FEP → カーソルに統一、Codex 5 回で Approve)。

## 2-1. PEGC 640x480 (Ra266 実機)

実機の ROM (INT 18h AH=30h) が 480 ラインへ入るとき・戻るときの OUT 列を `v86 -g` で全部記録し (2026-09-29)、`pegc_apply_timing` をその順序と値に合わせた。
NP21/W では出なかった違い: GDC の RESET と表示の停止をしていない、グラフィック GDC の CSRFORM・SCROLL の長さ (3FFh)、テキスト GDC の 480 ライン設定 (SYNC・PITCH 80・CSRFORM・SCROLL)、6Eh とテキスト CRTC、09A8h を毎回書いて bit7 を落としていた。GDC クロック 5MHz は 6Ah 83h・85h の両方、`gdc_send` は FIFO を有界に待つ。

**受け入れ (実機の画面は目で見ていない)**:
- (A) 実物の `backend_pegc.c` の OUT 列が実機 ROM の記録 (入り・戻り各 94 行) と一致する (ホスト試験、変異 76 本 RED)。意図的な違いは理由付きの除外表。**A0h DFh / A2h 28h の組 (4 行) は未解明の差分として除外** — 実機で崩れが残るなら最初に疑う。
- (B) NP21/W の回帰: PEGC・9801・Cirrus で gui_gate が通る。
- (C) 実機 Ra266 で `pegcchk 5` (シリアル): `enter 09a8=81 … clk=3 ext=1 800l=1 fifo_to=0 vs_to=0` / `exit 09a8=81 … clk=0 ext=0 800l=0 fifo_to=0 vs_to=0`、640x480 でテスト画を描いて CUI へ戻る。続く `v86 -g` で ROM のモード取得が元の AX=310d BX=0100 のまま。
- **残る確認**: 実機で GUI (gshell) を PEGC 640x480 で**目で見る**こと (v3 と並行、実機の前に居るとき)。

## 3. カーネル層で直した不具合 (主なもの)

- **GUI アプリが起動直後に消えていた** (2026-09-23〜26): KAPI の出力ポインタの検査が、アプリの syscall の中で走る WM (gshell) 自身の
  ポインタを弾いてアプリを kill していた。WM の文脈の深さで判定し、アプリが登録したバッファ (fd_redirect) は由来で必ず検査する。
- `ime_set_render` と `gui_register` にアプリが関数表を渡すと、カーネルがそれを CPL=0 で呼び続けた — 常駐側だけに限る。WM の中で起きた障害を `ring3_wm_fault_count` と `(in WM)` で見分ける。
- **Cirrus の窓の判定を RAM の上端ではなく物理地図で行う** (16MB を超える RAM で Cirrus が選ばれなかった)。リニア窓を 4GB 上位の **v3 のデバイス窓の帯 `[0xFE000000, 0xFF000000)`** へ移し、帯の先頭 4MB の PT を静的に持つ。Cirrus は NP21/W との互換のためだけで、auto では NP21/W の上に居るときだけボードの ID を読む。
- VFS の FD の失効と ime_dict の開き直し、KAPI のデータ欄の固定 (v63)、SHM 帯とカーネルスタックの重なり、ext2 の書き込みの諸点。
- カナ・CAPS はロックキー (押し込んで make・外して break) として扱う。

## 4. 配布物

- **MINIMAL を「起動・HDD への導入・回復・残りの取得」に絞った** — フォントと一般コマンドは NORMAL へ、hsync を MINIMAL へ。
  FD 起動で赤い `FONT..NG` が出るのは正常 (テキスト画面は本体の CG、GUI は漢字 ROM で描く)。
- 2HD の起動 FD の空き 359KB (1.44MB は 568KB)。空きが 64KB を切ったら `make check` が落ちる。

## 5. 開発の道具

- `make check` の 3 段: `check-fast` (約 33 秒)・`check-changed` (変えた所だけ変異試験)・`check` (約 2〜3 分、見直し前は約 10 分)。
- NP21/W ai-debug フォーク: `/api/quit`・`/api/instance`・`/api/cd`・`/api/fdd` (有界な待ち、ブレーク中は 409)、`tools/np21w_ctl.py` (停止・起動・媒体の出し入れ)。上流 0.86 rev105 を取り込んだ。
- `gui_gate.py` は台本の前に rshell を閉じ、GUI に入れたか (`scrn_ymax`/`grph_disp`、Cirrus は `wab_relay`/`wab_height`) を合否に入れる — CUI のまま `RESULT: OK` になっていた (R2 の 640x400 の正体)。
- CI (GitHub Actions) で本体をビルド、実機用の成果物は `tools/ci_fetch.sh`。

## 6. 分かっている制限 (v3 以降)

- 実機 Ra266 の PEGC 640x480 の GUI は目視未確認 (§2-1 の受け入れは画面を見ない条件)。

- 実機 Ra266 の内蔵アクセラレータ (PCI の Trident 1023:9660) のドライバは設計票だけ ([TASK_TRIDENT_DRIVER](tasks/realhw/TASK_TRIDENT_DRIVER.md)、Codex Approve)。

- `libos32ui` (microUI) のアプリにはマウスキーが届かない。
- CPL=3 のアプリのコールバック (`sys_ls`) は CPL=0 で同期的に呼ばれる (障害隔離のモデルで、敵対アプリの封じ込めは目標にしていない)。
