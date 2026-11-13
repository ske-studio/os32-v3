# PPC_NOTES — PowerPC 移植の注意事項 (実機・QEMU・root・フォント)

> 状態: **現行 (2026-11-13)** — PowerPC に固有の注意事項はこの 1 枚に集め、共通の文書 (docs/05_drivers.md・08_build.md など) には書かない (ユーザー指示 2026-11-13)。実装と取り込みの決まりは [PPC_IMPL_RULES](PPC_IMPL_RULES.md)、事実の資料は [docs/ppc](../../ppc/README.md)。実機の受入の結果は票 TASK_PPC_REALHW §2。

## 1. RAM の root とフォント

- PowerPC の RAM の root (埋め込み ISO、ISO level 1 の 8.3 名) は `build/ppc-kboot.mk` が `ROOT_MANIFEST=build/ppc-ram-root.json` で中身を足す。共通の `build/ram-root.mk` は呼び手が指定した時だけ manifest を使う。PC-98 の root は変えない。
- 収録: `/fonts/shino16.kcg` (東雲 16 の 16px EBDT から作る KCG1、非圧縮 295,956B)・`/fonts/shino16.txt` (東雲のライセンス)・`/sys/unicode.bin` (漢字の道 unicode_to_jis が読む)。パスは `include/config.h` の `SYS_FB_FONT`・`SYS_FB_FONT_LICENSE`・`SYS_UNICODE_BIN`。
- 字形の源は 2026-11-13 のユーザー決定「KCG の口 + 東雲 16 から作る .kcgfont」。ROM の無い機種の KCG の本体 `drivers/video/kcg/kcg_file.c` が KcgModuleApi と同じ口で返す。PC-98 は今までどおり CG ROM と `/boot/kcg.mod`、旧 `.kcgfont` の配布と `kcg_load_font` の NOSYS は変えない。
- KCG1: 16B の LE ヘッダ (`KCG1`・展開後サイズ・flags)、94×94×32B の漢字・fetched、256×16B の ANK・fetched。読み込み側は flags≠0 を拒否する。生成は `python3 tools/gen_font16.py --ebdt <JF-Dot-Shinonome16.ttf> <out>`、派生物は git に入れない。
- 作業木でフォントが無いときは `tools/preflight.sh --fix-fonts` (主の作業木から SHA-256 を照合してコピー)。ネットワークの無い所で `make all` が Inconsolata・東雲の取得で落ちるのはこれで避ける。

## 2. G4 実機 (PowerMac3,5、OF 4.2.1f2)

- 起動の行は `boot enet:192.168.2.12,os32-sh.elf,192.168.2.201,192.168.2.12`。この OF は `;` も区切りに数え、4 つ目がゲートウェイ、5 つ目が bootp-retries (`;mask,;gw` は通らない)。
- 引数を付けても OF は先にサーバへユニキャストの DHCPDISCOVER (送り元 0.0.0.0) と BSDP (LIST/SELECT) を出す。Linux の dnsmasq は martian として捨てるので、ノートでは AF_PACKET の返事役 (G4 の MAC だけに答える) を使う。返事役の期限切れに注意。
- OF の画面は 8bpp・640x480・linebytes 768 (Apple の CLUT、0 白・0xff 黒)。load-base は 0x800000。
- `/memory` の reg は空の DIMM を size 0 で並べる (飛ばす)。OF は読み込んだ像の直後の 32KiB を available に戻さない (64KiB までの隙間を許す)。RAM 1.5GiB、TB 33.29MHz、MPIC 64 本、PHY は BCM5411 (ID 0x0020/0x6071)。
- リンク確立の直後に送ったフレームは届かない。lan_service は RUNNING の後 2 秒は送らない。
- 切り離し前の停止は OF の画面に `FAIL:KB:<理由>` を出す。シリアル (ESCC) は筐体の外に出ていないので、観測は FB の段の印・カメラ・LAN。

## 3. QEMU (mac99)

- OpenBIOS の画面は既定で 800x600x32。FB の文字端末は初段 INDEX8 だけなので `-g 800x600x8` で動かす。
- FB の受入は `PPC_FB=1 python3 -B tools/ppc/usb_run.py` (S-B/S-C/S-D も同じ環境変数)。`-g 800x600x8 -display none` を選ぶ。`-nographic` は OF の stdout をシリアルに変え、display の採取を行えないため FB の観測には使わない。USB の product 試験は `screen.ppm` も残す。環境変数を省くと従来のシリアル経路を使う。
- この作業環境の OpenBIOS 1.1 (2026-06-24 build) は display の `color@` で例外を出し、catch セルを返さない。method 呼出しでは catch の初期値を失敗にし、未返却を成功と取り違えない。`color@` が失敗したときだけ、切り離し前に `color!` (r g b index) で 0=黒・255=白を書き、前景255・背景0、`fb_color_rc=4` (WRITTEN) を記録する。書込みも失敗したら端末を付けない。読めた場合は CLUT を変えず、明るい方を前景にする (OK=1、同じ明るさは EQUAL=3 で拒否)。
- USB の受入は `-icount shift=auto` (TCG では OHCI のフレームの時計が TB から遅れる)。
- LAN・USB の受入は時間に敏感で、ホストの負荷が高い (load 10 前後) と時間切れや打鍵の重複で落ちることがある。負荷の下がった時に回し直して確かめる。

## 4. 時計

- USER の syscall は EE=0 で走り tick_count が止まるので、機種の時計 (hostfs の now・USB の ms) は timebase から取る。DEC の処理を前景から呼ばない。

## 5. FB 端末の起動と出力

- SH/SHELL 像だけ共通 console・fbcon・gfx core/v2/sync・OF backend・resident KCG をリンクする。root → `kcg_file_load` (guard 内の resident lease は読込後に close) → Unicode-JIS 表の常駐読込 → HAL → console target → attach (top=64) → OF の色 → TEXT 準備 → clear/cursor → USB/入力 → sh の順。失敗時は `FB off step=... rc=...` を診断に残し stream で続ける。
- 固定 192 KiB のヒープでは 295,940B の字体キャッシュを持てない。SH/SHELL は最初のヒープ利用より前に台帳の BOOT owner で 1 MiB の連続 RAM を確保しヒープとする。固定 KAPI/SHM の配置は動かさず、確保失敗時は既存ヒープへ戻す (フォント失敗後も stream で起動)。診断像は既存ヒープのまま。
- USER の fd1・文字出力・printf は共通 console の mirror から `console_stream_write` へ送り、LAN terminal と ESCC に各 byte を 1 回送る。既存の有限キューと poll を保つ。起動・ドライバの診断は同じ formatter を別名で組み、`pm_diag_write` から LAN LOG/ESCC と共通 console の画面へ送る。この画面出力では terminal mirror を抑え、LOG を terminal に再投入しない。
- sh の clear は `tvram_clear` KAPI の PowerPC endpoint から共通 console の画面消去・原点復帰を呼び、`ESC[2J ESC[H` は stream だけへ送る。共通 console は ESC 列を解釈しない。mirror は [console の契約](../../design/kernel/console/console.md) を参照。x86 の CUI/GUI の描画門は同じ。取り込み前の NP21/W の CUI ver・shell 出力・CONSOLE_DUAL の受入は本線 PM が行う。
- USB の診断 (打鍵・enumeration・mouse・release を含む) は既存 LOG/ESCC に送り、端末画面へは出さない。同期 formatter の間だけ `pm_log_stream_only` を保存・設定・復元する。USER の echo/kprintf/fd1 はこれまでどおり共通 console と mirror へ送る。
- 画素の受入は `python3 -B tools/ppc/fb_run.py --mutate` (`ppc-fb` check)。NIC 無しの mac99、USB で sh を開始し、固定した UTF-8 echo の `/fb.sh` を source する。PPM・固定期待行列・像/フォント/Unicode 表の SHA-256・stream を `$TMPDIR/fb-run-*/` に残す。fbcon のカーソルは下 2 走査線の反転なので、oracle は `cursor_top=14` を使う。LAN 切断前後の比較は別途 LAN が使える環境で行う。
- 共通 console への移行後、FB が無い場合も console_get_cursor_x/y は論理カーソル座標を返す (旧 kboot_console の -1 とは異なる)。
