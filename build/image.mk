# ============================================================================
#  image.mk — D88/ISO イメージ生成
# ============================================================================

# FDD最小ブートイメージ (images/os32_boot.d88)
# HDDインストール用ブートFD。**中身は CD の BOOT + MINIMAL と同じ集合**で、
# build/packages.yaml の fd: から tools/mkpkg.py --fd-args が作る (FD だけに
# 要る LOADER.BIN / profile と、FAT の 8.3 に合わせた置き場所の違いもそこ)。
# ここに一覧を書かないこと — 2026-09-24 まで FDD_MIN_CMDS を手で持っていて
# CD の MINIMAL とずれていた。make check-packages-host が実物で等しいかを見る。
# 2hd と 1.44MB で 1 つを使う。$(1) = LOADER.BIN のもと。
FDD_IMAGE_DEPS = boot $(BUILD_OUT)/vmkernel.lz4 programs unicode_bin \
                 $(BUILD_OUT)/settings.db assets-deployed build/packages.yaml \
                 userland/deploy.yaml build/core.yaml tools/mkpkg.py
define FDD_IMAGE_ARGS
args=$$(python3 tools/mkpkg.py --plan build/packages.yaml --base . --fd-args --fd-loader $(1)) || exit 1
endef

images/os32_boot.d88: $(FDD_IMAGE_DEPS)
	@mkdir -p images
	@echo "=== Building OS32 minimal FDD image (images/os32_boot.d88) ==="
	@$(call FDD_IMAGE_ARGS,boot/loader_fat_new.bin); \
	python3 tools/mkfat12.py -o images/os32_boot.img -b boot/boot_fat.bin -d images/os32_boot.d88 --tree $$args
	@echo "Copying os32_boot.d88 to NP21/W directory..."
	@cp images/os32_boot.d88 '$(NP21W_DIR)/os32_boot.d88' 2>/dev/null || echo "Warning: Failed to copy os32_boot.d88 to np21w directory."

# --- 1.44MB 版 (生イメージ) ---
# **D88 にしない。** 1.44MB の D88 は fd_type=0x21 かつ全セクタの rpm_flg=1 が
# 要り、tools/mkd88.py はまだ書けない。NP21/W は 1,474,560 バイトちょうどの
# 生イメージをサイズで 1.44MB と判定する (src/diskimage/fd/fdd_xdf.c の表)。
images/os32_boot144.img: $(FDD_IMAGE_DEPS)
	@mkdir -p images
	@echo "=== Building OS32 1.44MB FDD image (images/os32_boot144.img) ==="
	@$(call FDD_IMAGE_ARGS,boot/loader_fat144.bin); \
	python3 tools/mkfat12.py -g 144 -o images/os32_boot144.img -b boot/boot_fat144.bin --tree $$args
	@SIZE=$$(stat -c%s images/os32_boot144.img); \
	if [ "$$SIZE" != "1474560" ]; then \
		echo "ERROR: $$SIZE バイト。NP21/W は 1474560 ちょうどでないと 1.44MB と見ない"; \
		exit 1; \
	fi
	@cp images/os32_boot144.img '$(NP21W_DIR)/os32_boot144.img' 2>/dev/null || echo "Warning: Failed to copy os32_boot144.img to np21w directory."

fd144: images/os32_boot144.img
.PHONY: fd144

# パッケージ / ISO生成
# 中身は配備マニフェスト (build/core.yaml + userland/deploy.yaml) のタグから
# 決める (構成は build/packages.yaml)。mkpkg は登録ファイルの欠損をエラーに
# するので、配備マニフェストが挙げる成果物をすべて依存に結ぶ (clean 後の単独
# `make iso` や `make -j` でも欠損で落ちないように)。settings.db は媒体だけ、
# settings.v2.fixture は DEBUG が持つ。出力先の古い *.PKG は mkpkg が消す
# (ISO は packages/ を丸ごと焼くので、名前の変わった PKG が残らないように)。
packages: programs boot $(BUILD_OUT)/vmkernel.lz4 unicode_bin \
          $(BUILD_OUT)/settings.db $(BUILD_OUT)/settings.v2.fixture \
          assets-deployed assets/fep.db
	python3 tools/mkpkg.py --plan build/packages.yaml --output packages/ --base .

iso: packages
	@mkdir -p images
	genisoimage -o images/os32_install.iso -V "OS32_INSTALL" -input-charset utf-8 -R packages/

# イメージクリーン
clean-images:
	rm -f packages/*.PKG images/os32_install.iso os32_boot.img os32_boot.d88
	rm -rf images

.PHONY: packages iso clean-images
