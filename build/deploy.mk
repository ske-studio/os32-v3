# ============================================================================
#  deploy.mk — デプロイターゲット
# ============================================================================

# deploy: HostDrv方式 — ビルド成果物をC:\os32にコピー (sudo不要, 再起動不要)
# ゲストOSは /host 経由で直接アクセス可能
deploy: $(BUILD_OUT)/vmkernel.lz4 programs unicode_bin
	@echo "=== HostDrv Deploy ==="
	$(HOSTDRV_DEPLOY) sync
	$(PRUNE_STALE) hostdrv $(PRUNE_FLAG)

# deploy-kernel: vmkernel.lz4 と全ビルド成果物をNHDのext2に配置
#   (NP21/W再起動が必要)。HostDrv を先に同期するので、HostDrv 側が古いまま
#   ext2 を上書きして中身を失う事故を防げる。
#   ブートローダー自体を変更した場合は deploy-boot を別途実行すること。
deploy-kernel: $(BUILD_OUT)/vmkernel.lz4
	@echo "=== Sync to HostDrv before NHD deploy ==="
	$(HOSTDRV_DEPLOY) sync
	$(NHD_DEPLOY) sync-from-hostdrv
	$(PRUNE_STALE) both $(PRUNE_FLAG)
	$(NHD_DEPLOY) deploy

# deploy-boot: ブートローダーのみNHDブート領域 (LBA 2-17) に書き込み
#   boot/loader_hdd.bin を変更した場合のみ実行する
deploy-boot: boot/loader_hdd.bin
	@echo "=== Boot Loader Deploy ==="
	$(NHD_DEPLOY) write-boot boot/loader_hdd.bin

# nhd-migrate-pt: 区画表を旧配置 → PC-98 標準配置へ移す (票 TASK_HDD_INSTALL N3)。
#   KAPI v64 以降のカーネルとローダは**標準配置しか読まない**ので、旧配置の NHD は
#   区画表・ローダ (LBA 2-17)・/boot/vmkernel.lz4 を**同時に**替える必要がある。
#   deploy-kernel はカーネルしか替えないので移行しない。NP21/W を止めて実行 ([D1])。
#   既に標準配置なら何も書かずに終わる。
nhd-migrate-pt: boot/loader_hdd.bin $(BUILD_OUT)/vmkernel.lz4
	@echo "=== NHD partition table migration (legacy -> PC-98 standard) ==="
	$(NHD_DEPLOY) migrate-pt --loader boot/loader_hdd.bin --kernel $(BUILD_OUT)/vmkernel.lz4

# deploy-nhd: NHDフルデプロイ (ローダー+全ファイル)
deploy-nhd: $(BUILD_OUT)/vmkernel.lz4 programs unicode_bin
	@echo "=== NHD Deploy (using deploy.yaml) ==="
	$(NHD_DEPLOY) sync
	$(PRUNE_STALE) both $(PRUNE_FLAG)
	$(NHD_DEPLOY) deploy

# prune-stale: 配備先 (HostDrv + NHD) に残ったマニフェストに無い *.bin を掃除する。
#   配備は書くだけで消さないので、KAPI 変更後の stale バイナリ (別関数へ飛んで
#   rshell ごと沈黙する) が溜まる。deploy / deploy-nhd / deploy-kernel は既定で
#   削除まで行う (NO_PRUNE=1 で一覧だけ)。対象はシステム側ディレクトリ直下の
#   *.bin のみで、/home /data /etc には触れない。
#   例: make prune-stale            一覧だけ (dry-run)
#       make prune-stale-delete     削除 (NHD 側は次の deploy で Windows へ反映)
PRUNE_STALE = python3 tools/prune_stale.py
PRUNE_FLAG  = $(if $(NO_PRUNE),,--delete)
prune-stale:
	$(PRUNE_STALE) both
prune-stale-delete:
	$(PRUNE_STALE) both --delete

# hotdeploy: 廃止 (2026-09-09)。物理末尾に 256KB の窓を予約する仕組みだったが、
#   その窓は CPL=3 スタック帯と同じ範囲で、8MB 構成ではアプリと必ず衝突していた。
#   ユーザーランドの配送は HostDrv に一本化した:
#       make deploy            (ホスト → C:\os32)
#       ゲストで hsync         (/host → / 、既定で sys は除く)
#   カーネルも /host/boot/vmkernel.lz4 として運べる (次回起動から有効)。
#   IPL/ブートセクタだけは NHD 書き込み (deploy-boot) が要る。
hotdeploy:
	@echo "make hotdeploy は廃止 (2026-09-09)。"
	@echo "  1) make deploy      # ホスト → C:\\os32"
	@echo "  2) ゲストで hsync   # /host -> / (sys は 'hsync sys' で明示)"
	@echo "経緯: docs/tasks/hotdeploy/DESIGN.md"
	@exit 1

# nhd-mount: NHDのext2パーティションをマウント
nhd-mount:
	$(NHD_DEPLOY) mount

# nhd-umount: NHDのext2パーティションをアンマウント
nhd-umount:
	$(NHD_DEPLOY) umount

# nhd-pull: Windows側NHDを build/nhd/ に取り込む (フォーマットしない)
#   deploy-nhd はローカル NHD が無ければ自動で pull するので通常は不要
nhd-pull:
	$(NHD_DEPLOY) pull

# nhd-init: 初回セットアップ (Windows側NHDコピー + フォーマット + マウント)
#   フォーマットするのでゲスト側で作られたデータは消える
nhd-init:
	$(NHD_DEPLOY) init

.PHONY: deploy deploy-kernel deploy-boot deploy-nhd nhd-migrate-pt hotdeploy nhd-mount nhd-umount nhd-pull nhd-init prune-stale prune-stale-delete
