# PPC_NOTES — PowerPC 移植の注意事項 (実機・QEMU・root・フォント)

> 状態: **現行 (2026-11-14)** — PowerPC に固有の注意事項はこの 1 枚に集め、共通の文書 (docs/05_drivers.md・08_build.md など) には書かない (ユーザー指示 2026-11-13)。PowerPC で機能を作る前に、他のアーキテクチャや本線と統合して作れるかを判断する: [POLICY_DEV](../../POLICY_DEV.md) §1「別のアーキテクチャで機能を作るときは、統合して作れるかを先に判断する」。実装と取り込みの決まりは [PPC_IMPL_RULES](PPC_IMPL_RULES.md)、事実の資料は [docs/ppc](../../ppc/README.md)。実機の受入の結果は票 TASK_PPC_REALHW §2。

## 1. RAM の root とフォント

- PowerPC の RAM の root (埋め込み ISO、ISO level 1 の 8.3 名) は `build/targets/ppc-powermac.mk` が `ROOT_MANIFEST=build/ppc-ram-root.json` で中身を足す。共通の `build/ram-root.mk` は呼び手が指定した時だけ manifest を使う。PC-98 の root は変えない。
- 収録: `/fonts/shino16.kcg` (東雲 16 の 16px EBDT から作る KCG1、LZ4 の flags=1 で 187,980B 前後、展開後 295,940B)・`/fonts/shino16.txt` (東雲のライセンス)・`/sys/unicode.bin` (漢字の道 unicode_to_jis が読む)。パスは `sys/include/config.h` の `SYS_FB_FONT`・`SYS_FB_FONT_LICENSE`・`SYS_UNICODE_BIN`。
- 字形の源は 2026-11-13 のユーザー決定「KCG の口 + 東雲 16 から作る .kcgfont」。ROM の無い機種の KCG の本体 `sys/kern/console/font_source_kcg.c` が KcgModuleApi と同じ口で返す。PC-98 は今までどおり CG ROM と `/boot/kcg.mod`、旧 `.kcgfont` の配布と `kcg_load_font` の NOSYS は変えない。
- KCG1: 16B の LE ヘッダ (`KCG1`・展開後サイズ・flags)、94×94×32B の漢字・fetched、256×16B の ANK・fetched。flags は 0 (非圧縮) と 1 (本体全体が 1 つの LZ4 block) だけを受け、他は拒否する ([PPC_KCG_LZ4_DESIGN](PPC_KCG_LZ4_DESIGN.md))。製品の生成は `python3 tools/gen_font16.py --ebdt <JF-Dot-Shinonome16.ttf> <out> --lz4` (`build/font-fetch.mk`、Python の lz4 が要る)。派生物は git に入れない。
- 作業木でフォントが無いときは `tools/preflight.sh --fix-fonts` (主の作業木から SHA-256 を照合してコピー)。ネットワークの無い所で `make all` が Inconsolata・東雲の取得で落ちるのはこれで避ける。

## 2. G4 実機 (PowerMac3,5、OF 4.2.1f2)

- 起動の行は `boot enet:192.168.2.12,os32-sh.elf,192.168.2.201,192.168.2.12`。この OF は `;` も区切りに数え、4 つ目がゲートウェイ、5 つ目が bootp-retries (`;mask,;gw` は通らない)。
- 引数を付けても OF は先にサーバへユニキャストの DHCPDISCOVER (送り元 0.0.0.0) と BSDP (LIST/SELECT) を出す。Linux の dnsmasq は martian として捨てるので、ノートでは AF_PACKET の返事役 (G4 の MAC だけに答える) を使う。
- NVRAM の起動名は `os32-sh.elf` に固定する。更新時はノートで前版を `os32-sh.<中身>-<日付>.elf` として保管し、`/srv/tftp/os32-sh.elf` を新版に置き換える。両方を読み取り専用にする。
- G4 の電源を入れる前に、ノートで常駐の BSDP 応答サービス `os32-g4-bootp` が稼働していることを確かめる。
- OF の画面は 8bpp・640x480・linebytes 768 (Apple の CLUT、0 白・0xff 黒)。load-base は 0x800000。
- `/memory` の reg は空の DIMM を size 0 で並べる (飛ばす)。OF は読み込んだ像の直後の 32KiB を available に戻さない (64KiB までの隙間を許す)。RAM 1.5GiB、TB 33.29MHz、MPIC 64 本、PHY は BCM5411 (ID 0x0020/0x6071)。
- リンク確立の直後に送ったフレームは届かない。lan_service は RUNNING の後 2 秒は送らない。
- 切り離し前の停止は OF の画面に `FAIL:KB:<理由>` を出す。シリアル (ESCC) は筐体の外に出ていないので、観測は FB の段の印・カメラ・LAN。

## 3. QEMU (mac99)

- OpenBIOS の画面は既定で 800x600x32。FB の文字端末は初段 INDEX8 だけなので `-g 800x600x8` で動かす。
- FB の受入は 旧手順（422c11a0ec91 の記録） (S-B/S-C/S-D も同じ環境変数)。`-g 800x600x8 -display none` を選ぶ。`-nographic` は OF の stdout をシリアルに変え、display の採取を行えないため FB の観測には使わない。USB の product 試験は `screen.ppm` も残す。環境変数を省くと従来のシリアル経路を使う。
- この作業環境の OpenBIOS 1.1 (2026-06-24 build) は display の `color@` で例外を出し、catch セルを返さない。method 呼出しでは catch の初期値を失敗にし、未返却を成功と取り違えない。`color@` が失敗したときだけ、切り離し前に `color!` (r g b index) で 0=黒・255=白を書き、前景255・背景0、`fb_color_rc=4` (WRITTEN) を記録する。書込みも失敗したら端末を付けない。読めた場合は CLUT を変えず、明るい方を前景にする (OK=1、同じ明るさは EQUAL=3 で拒否)。
- USB の受入は `-icount shift=auto` (TCG では OHCI のフレームの時計が TB から遅れる)。
- LAN・USB の受入は時間に敏感で、ホストの負荷が高い (load 10 前後) と時間切れや打鍵の重複で落ちることがある。負荷の下がった時に回し直して確かめる。

QEMU の sungem は、受信リングが満杯の間に来た電文を溜め、RXDMA_KICK を書いても配り直さない (-trace sungem_rx_* で観測、2026-11-15)。そのため、受信の有効化の前に 27 本以上溜まると受信が止まる。製品では回避しない (ホストが送り続けないことで防ぐ、docs/design/tools/host_notify.md「送りの規則」の沈黙)。

`-icount` の下で待ちが NAP すると、QEMU の仮想の時計とゲストの TB が、実時間から瞬間的にずれる (2026-11-16 観測)。
- 平均の tick の進みは変わらない。
- send-key の hold-time 100 ms が、実時間で 3〜967 ms になった (眠らない時は 87 ms〜)。
- 実時間で 100 ms 押しても、ゲストには 500 ms を超えて見えて、繰り返しが出た。
- そのため USB の打鍵の受入は、押す長さをゲストの時計に合わせる。ゲストの押下 (長押しは最初の REPEAT) を見てから離す (usb_run.py (旧版 402a96233090) の key)。
- NAP で前景が DEC と同時に起きるので、受入が足す観測の枠 (~USBSTREAM) の書き込みに、IRQ の側の USB の行が割り込みやすい。枠は IRQ を止めて一続きに書く。
- 製品の振る舞いは変えない。

待ちで 7450 を眠らせる試し (2026-11-15、使い捨ての木。この形のままでは製品に入れない):
- 形: arch_cpu_idle で HID0 を NAP にして、EE=1・POW=1 の mtmsr で眠る。差分は ~/os32-tmp/run/ppc-c-idle/c-idle-experiment.patch (リポジトリ外)。
- QEMU では眠りが効く。64KiB の /host の cp の間、時間の 9 割を眠る。DEC (1 kHz) の割り込みで起き、1 回の眠りは最長約 4 ms (1 tick の 10 ms 未満)。起きた後の tick と TB は壁時計と合う。
- QEMU は割り込みの入口で MSR[POW] を落とし、SRR1 に POW を残さない。実機の 7450 で SRR1 に POW が残るか (残れば戻りでまた眠るので入口で消す処理が要る) は未確認。
- 今の作り (GEM の割り込みは INTMASK=~0 で止め、受信は tick の poll) では遅くなる。64KiB の cp が 24.8 s → 32.1〜32.2 s。電文が来ても、次の DEC か tick まで前景が進まないためと見ている (未確認)。眠りを使うなら、GEM の受信の割り込みと組にする設計が要る。ユーザー決定 (2026-11-16「実機で測ってからがだめ」「そう言う意味ではcpuは休ませないと」): 実機の数字を待たずに、次の形 4 を入れる。
- 形 4: 前景の待ちで poll と link_tick を進め (1 ms に 1 回まで、共通 sys/net/nic.c、本線の段 lo-fgtick)、仕事が無ければ NAP する (段 ppc-cpu-nap)。
- 使い捨ての木で測った 64KiB の cp: 前景で poll だけなら 24.8 s、poll + NAP は 27.5 s、poll + link_tick (眠らない) は 29.7 s、poll + link_tick + NAP は 21.7 s で、眠りは 8 割。
- 受信の割り込みを開ける設計は要らない。G4 の実機では、/host の cp と SRR1 の POW を確かめる。

## 4. 時計

- W4 実装前の現状では USER の syscall は EE=0 で走り tick_count が止まるので、機種の時計 (hostfs の now・USB の ms) は timebase から取る。DEC の処理を前景から呼ばない。W4 適合後は通常 syscall 本体を FG1 とし、timer IRQ が進行する (ARCH_COMPAT の AC-CTX-1/6)。適合はまだ未。

## 5. FB 端末の起動と出力

- SH/SHELL 像だけ共通 console・fbcon・gfx core/v2/sync・OF backend・resident KCG をリンクする。root → `kcg_file_load` (guard 内の resident lease は読込後に close) → Unicode-JIS 表の常駐読込 → HAL → console target → attach (top=64) → OF の色 → TEXT 準備 → clear/cursor → USB/入力 → sh の順。失敗時は `FB off step=... rc=...` を診断に残し stream で続ける。
- 固定 192 KiB のヒープでは 295,940B の字体キャッシュを持てない。SH/SHELL は最初のヒープ利用より前に台帳の BOOT owner で 1 MiB の連続 RAM を確保しヒープとする。固定 KAPI/SHM の配置は動かさず、確保失敗時は既存ヒープへ戻す (フォント失敗後も stream で起動)。診断像は既存ヒープのまま。
- USER の fd1・文字出力・printf は共通 console の mirror から `console_stream_write` へ送り、LAN terminal と ESCC に各 byte を 1 回送る。既存の有限キューと poll を保つ。起動・ドライバの診断は同じ formatter を別名で組み、`pm_diag_write` から LAN LOG/ESCC と共通 console の画面へ送る。この画面出力では terminal mirror を抑え、LOG を terminal に再投入しない。
- sh の clear は `tvram_clear` KAPI の PowerPC endpoint から共通 console の画面消去・原点復帰を呼び、`ESC[2J ESC[H` は stream だけへ送る。共通 console は ESC 列を解釈しない。mirror は [console の契約](../../design/kernel/console/console.md) を参照。x86 の CUI/GUI の描画門は同じ。取り込み前の NP21/W の CUI ver・shell 出力・CONSOLE_DUAL の受入は本線 PM が行う。
- USB の診断 (打鍵・enumeration・mouse・release を含む) は既存 LOG/ESCC に送り、端末画面へは出さない。同期 formatter の間だけ `pm_log_stream_only` を保存・設定・復元する。USER の echo/kprintf/fd1 はこれまでどおり共通 console と mirror へ送る。
- 画素の受入は 旧手順（422c11a0ec91 の記録） (`ppc-fb` check)。NIC 無しの mac99、USB で sh を開始し、固定した UTF-8 echo の `/fb.sh` を source する。PPM・固定期待行列・像/フォント/Unicode 表の SHA-256・stream を `$TMPDIR/fb-run-*/` に残す。fbcon のカーソルは下 2 走査線の反転なので、oracle は `cursor_top=14` を使う。LAN 切断前後の比較は別途 LAN が使える環境で行う。
- 共通 console への移行後、FB が無い場合も console_get_cursor_x/y は論理カーソル座標を返す (旧 kboot_console の -1 とは異なる)。

## 6. PowerPC の試験と CI

- **CI の check-fast には、PowerPC の toolchain・QEMU・LAN の socket が無い。**
  - PPC の cross gcc・qemu-system-ppc・socket を使う試験は、無い環境ではその半分を `SKIP <理由>` と表示して 0 を返す。host の側の試験と変異は、必ず回す。
  - CI で PowerPC を必ず組むのは、check.yml の `ppc-compile` の job。そこでは `OS32_REQUIRE_PPC=1` が立つので、toolchain が無ければ失敗にする。
  - 手本は `test_link_platform.py (旧版 c4bc40be8d05)`・`compile_ci.py (旧版 3d07969b3073)`・`ppc-sc.py (旧版 bdd8b904e457)`。
- **新しい試験は、取り込む前に `PPC_CROSS_DIR=/nonexistent` でも 1 回回す。** CI と同じ形で、SKIP と rc=0 になることを確かめる。2026-11-14 に、P1a の link-platform-host と P1f の gem-adapter-host が無条件に cross gcc を呼び、CI で落ちた。
- **実装者の sandbox では socket が使えない。**
  - LAN と link の QEMU の受入 (link_run・sc_run の LAN・net-link-host の本物の agent) は、PM が手元で回す。
  - 試験を、環境 (socket の有無) によって別の手順に切り替わる作りにしない (2026-11-14 の ppc-fb)。
