# sys/dev/block/netcd.c — ネットワーク越しの CD (ホストの ISO 像を cd0 として読む)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `sys/dev/block/netcd.c`・`sys/dev/block/netcd.h`・`sys/dev/net/e82557/lan_module.h` (`netcd_register` の欄)・`sys/platform/pc98/kernel_boot_memory.inc`・`sys/include/config.h` (`SYS_NETCD_CFG`)・`sys/dev/ata/cd_module.c`・`sys/hal/block/dev.h` (`dev_register_cd`・`media_check`)・`tools/fd_netcd_config.py`・`build/packages.yaml` (`fd.only`)・`userland/system/install.c` (`fd_only`)

## 何をするか

- ホストの `lanfs_host.py --image install=/path/os32.iso` が公開した像を、alias (`install`) で開き、読み取り専用の Device `cd0` (512B セクタ) として見せる。
- `cdinst` は変えない。`/cd0` に iso9660 を載せれば、今までの CD からのインストールがそのまま動く。
- 物理の CD (ATAPI) は `dev_register_cd(dev, -1)` で最初の空き `cdN` を取る。netcd が先に `cd0` を取るので、ATAPI と並ぶ時は ATAPI が `cd1`。NETCD を指定しなければ今までどおり ATAPI が `cd0` で、既存の cd0 を横取りしない。
- ホストの実機器 (光学ドライブなど) を見せたい時は、同じ image の口を出すラッパーをホスト側に作る (OS32 の側は変えない)。

## 起動の手順と設定

1. ホストで `tools/fd_netcd_config.py <入力 FD 像> --output <出力 FD 像> --netcd install --peer <MAC>`。入力の像は変えず、出力の像の `/etc/netcd.cfg` に `NETCD=install`・`SFS_LAN=1`・`SFS_LAN_PEER=<mac>` を書いて読み戻して確かめる (2HD と 1.44MB)。
2. ホストで `lanfs_host.py` (`--image`、要れば `--allow-write`) と `host_agent.py` (`lan_bridge.py` を含む) を起動する。
3. 出力の FD で起動する。POST_ROOT (`kernel_boot_memory`) で、`NETCD` があれば: メールボックスの確保 → PCI の資源 → lan.mod の読み込み・NIC の bind (`kernel_boot_lan`、`kernel_boot_hostsvc` が常駐の /host も始める) → `lan_module_netcd_register(alias)` で cd0 を登録 → `cd_module_load()` (ATAPI は次の空き番号) → 子の mount。PCI と LAN の段は二重に行わない。NETCD が無ければ LAN の段は従来の後ろの位置。
4. `mount /cd0 cd0 iso9660` → 従来の `cdinst`。

- `/etc/netcd.cfg` は FD にだけ置く (`build/packages.yaml` の `fd.only` と `install.c` の `fd_only` の両方)。`/etc/system.cfg` に書くと install が HDD へ写し、HDD 起動でも netcd が cd0 を取ってしまう。
- 登録の時にホストと通信しない (`netcd_register` は cd0 を予約するだけ。既にあれば `EXIST`)。起動の後から付け替える口は無い。
- alias は 1〜63B。

## 状態 (`Device.priv` の私有の文脈、netcd が単独で持つ)

`struct NetCd` (handle・媒体の世代 `media_gen`・最後の使用・`alive_seen`・size・mtime)、alias の写し、`struct SfsBlock` (sid・handle・同一性)。

## 読みと媒体の確かめ

- `netcd_read` と `netcd_media_check` は、操作の入口で 1000ms (`NETCD_DEADLINE_TICKS`) の期限を 1 回決め、ensure・BOPEN・BREAD に同じ値を渡す。読みは 2 セクタずつに割るが期限は 1 つ。
- 次の場合は BOPEN し直して同一性 (size + mtime) を比べる: まだ開いていない / 30 秒使っていない (`NETCD_IDLE_TICKS`) / seq が 0xF000 を超えた / ALIVE の観測の通番が `alive_seen` と違う / /host の側が sid を張り替えた / ホストが `STALE` を返した。待ちは task の中だけ (IRQ0 で通信しない、IRQ から lan.mod へ呼び戻さない)。
- `alive_seen` には確かめる前に取った観測の通番を入れる。確かめている間に来た新しい ALIVE は次のアクセスに残る。
- 同じ像なら媒体の世代を保つ。違えば `media_gen++` してその操作は `STALE`。ISO の側が FD を永久に失効させ、キャッシュを全部捨て、次のパスの操作で同じ `/cd0` を載せ直す ([fs/isofs/iso9660_media](../../fs/isofs/iso9660_media.md))。
- BREAD の直前ごとに確かめるので、seq の上限をまたいで sid が替わっても古い handle を送らない。
- 書き込みは `ROFS`。

## 確かめ

- `test_hostsvc_netcd.py (旧版 c2b0e620ea28)`・`test_hostsvc_media.py` (像の差し替え・同じ像は世代を保つ・ATAPI の回帰・netcd=cd0 と並ぶ ATAPI=cd1)・`test_hostsvc_boot.py` (起動の順)・`test_hostsvc_fd.py`・`test_fd_netcd_config.py` (FD の設定の書き込みと読み戻し)・`test_packages.py` (`/etc/netcd.cfg` を HDD に写さない)。
