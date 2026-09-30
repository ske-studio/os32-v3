# Step 07: デプロイパイプライン刷新

## 目的

カーネル更新を「rawセクタ書き込み」から「通常ファイルコピー」に変更し、
デプロイ手順を簡素化する。

## 設計方針

- **ローダーのみ** rawセクタ書き込み (IPL + Mini-Loader = ~8.5KB)
- **カーネル** (vmkernel.lz4) は ext2パーティションへのファイルコピー
- HostDrvデプロイ経由でも vmkernel.lz4 を配置可能に

## 変更ファイル

### [MODIFY] tools/nhd_deploy.py

#### `write-kernel` コマンドの変更

旧:
```
write-kernel kernel.bin loader_hdd.bin sqlite.bin
→ IPL + ローダー + kernel.bin + sqlite.bin を全てrawセクタに書き込み
```

新:
```
write-boot loader_hdd_new.bin
→ IPL (LBA 0) は既存を維持
→ ローダー (LBA 2-17) のみrawセクタ書き込み

deploy-vmkernel vmkernel.lz4
→ ext2パーティションの /boot/vmkernel.lz4 にファイルコピー
```

#### 旧定数の削除

```python
# 削除
KERNEL_LBA = 6
KERNEL_COUNT = 256
SQLITE_LBA = 262
SQLITE_COUNT = 1200
```

#### 新コマンド追加

```python
# write-boot: ローダーのみrawセクタ書き込み
elif cmd == 'write-boot':
    loader_data = read_file(args[0])
    write_raw_sectors(nhd, LOADER_LBA, loader_data)

# deploy-vmkernel: ext2にvmkernel.lz4をコピー
elif cmd == 'deploy-vmkernel':
    mount_ext2(nhd)
    copy_to_ext2('/boot/vmkernel.lz4', args[0])
    umount_ext2()
```

### [MODIFY] tools/hostdrv_deploy.py

HostDrvデプロイに vmkernel.lz4 を追加:

```python
# deploy.yaml に追加するエントリ:
# - src: vmkernel.lz4
#   dst: /boot/vmkernel.lz4
```

### [MODIFY] tools/deploy.yaml

```yaml
files:
  # カーネルイメージ (新方式)
  - src: vmkernel.lz4
    dst: /boot/vmkernel.lz4

  # 旧エントリ削除:
  # - src: kernel.bin   ← 削除
  # - src: sqlite.bin   ← 削除
```

### [MODIFY] build/deploy.mk

```makefile
# 旧: deploy-kernel: kernel.bin sqlite.bin
#   $(NHD_DEPLOY) write-kernel kernel.bin boot/loader_hdd.bin sqlite.bin
#   $(NHD_DEPLOY) sync-from-hostdrv
#   $(NHD_DEPLOY) deploy

# 新:
deploy-kernel: vmkernel.lz4
	$(NHD_DEPLOY) write-boot boot/loader_hdd_new.bin
	$(NHD_DEPLOY) deploy-vmkernel vmkernel.lz4
	$(NHD_DEPLOY) sync-from-hostdrv
	$(NHD_DEPLOY) deploy

# HostDrvデプロイ (再起動不要)
deploy: vmkernel.lz4 programs unicode_bin
	@echo "=== HostDrv Deploy ==="
	$(HOSTDRV_DEPLOY) sync
```

### [MODIFY] build/image.mk

FDDイメージ生成を更新:

```makefile
# 旧: /kernel.bin=kernel.bin
# 新: /VMKRNL.LZ4=vmkernel.lz4
```

### [MODIFY] .agents/workflows/build-os32.md

ワークフローのデプロイステップを新コマンドに更新。

### [MODIFY] .agents/workflows/full-build.md

同上。

## 旧コード退役

### [DELETE] boot/loader_hdd.asm (旧HDDローダー)
### [DELETE] boot/loader_fat.asm (旧FDDローダー)

※ 新ローダーの安定動作確認後に削除。
  移行期間中は旧ファイルを `boot/_legacy/` に移動して保持。

## 検証

```bash
# フルビルド + デプロイ
make clean
make all
make deploy-kernel

# NP21/W再起動
# HDDブートでカーネル起動確認
# プログラム変更のみの場合:
make deploy  # HostDrv経由、再起動不要
```

## CDインストーラーへの影響

CDインストーラー (`install.bin` / `cdinst.bin`) は以下のように更新:

1. ISO9660 から `vmkernel.lz4` を読み込み
2. HDD ext2パーティションの `/boot/vmkernel.lz4` にコピー
3. ローダー (loader_hdd_new.bin) を rawセクタ (LBA 2-17) に書き込み
4. IPL (boot_hdd.bin) を LBA 0 に書き込み

※ CDインストーラーの更新は本計画の範囲外 (別タスクとして実施)
