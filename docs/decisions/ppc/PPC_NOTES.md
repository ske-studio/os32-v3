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
- USB の受入は `-icount shift=auto` (TCG では OHCI のフレームの時計が TB から遅れる)。
- LAN・USB の受入は時間に敏感で、ホストの負荷が高い (load 10 前後) と時間切れや打鍵の重複で落ちることがある。負荷の下がった時に回し直して確かめる。

## 4. 時計

- USER の syscall は EE=0 で走り tick_count が止まるので、機種の時計 (hostfs の now・USB の ms) は timebase から取る。DEC の処理を前景から呼ばない。
