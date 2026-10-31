# fs/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_D2_DESIGN・DEP_DIR_D3_DESIGN (D1 で移した)
ソース: `fs/`

- FSの媒体I/Oは既存Device/VFS経由を維持。256/512B変換、part_offset控除、範囲演算、丸め、失敗時の初期値はFS側で維持。新ヘルパ内で再計算しない。

### ide_limits

`fs/ide_limits.h` は共通契約から上限を得て mask を維持（hd名/bootinfo幾何/MBR選択は変えない）。

### vfs_fd

- stdinは既存canonical処理をVFSに残す。新しいTTY・Deviceインスタンスは不要。

```c
/* fs/vfs_fd.c: リダイレクトの先行判定、size==0、部分読出しはそのまま。 */
int c = input_getchar_kapi(); /* 従来kbd_getchar_kapiと同じprovider */
if (c < 0) return i ? (int)i : c;
if (c == '\r') c = '\n';
p[i++] = (u8)c;
if (c == '\n') break;
```

### vfs_device

```c
/* fs/vfs_device.h: VFS 内部契約。VFS_DEV_* の既存値は固定。 */
enum vfs_name_mode { VFS_NAME_DIGIT_PREFIX, VFS_NAME_PREFIX, VFS_NAME_EXACT };
struct vfs_device_name {
    const char *prefix;
    int mode, type, first, count;
};
void vfs_device_names_register(const struct vfs_device_name *rows, unsigned n);
```

### vfs

- rows/件数を VFS が保持、実体は platform/pc98/boot_names.c。pc98_boot_bind で一括登録、確保不要。VFS/FS の通常 init でこの表を消さない。未登録時は parse/番号検証とも失敗。
- 純粋な字句解決表であり、drivers/dev の実在 Device 表とは別。fd2/3 や不在 hd も旧来の parse は成功するため dev_find 成功を条件にしない。loop/lo は新規に受理しない。
- DIGIT_PREFIX は prefix 直後の **1 桁だけ**を範囲検査し、末尾は無視。COM、COM0、COM9、COMxyz は全て SERIAL/1。hostdrv0 は失敗。大小文字は区別。成功するまで出力ポインタへ書かない。

- 使う文脈は起動/協調/KAPI、IRQ 内で parse/mount しない。ABI・マウント衝突判定・FS 側の型検査はそのまま。

名前のレジストリの登録を mount の前提にしない。
装置の番号の上限は VFS 側の定数 (`VFS_DEV_UNIT_MAX` 等)
名前→装置の解決だけに絞る。

## 索引

- [fs/ext2/](ext2/README.md)
