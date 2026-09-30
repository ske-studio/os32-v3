# コーダー1 タスク: ユーザランドの特権命令を除去 (M1 前提)

> 発行: PM (2026-09-01) / 状態: **完了記録 (2026-09-03)**
> 担当: コーダー1 (Opus 5) / 前提: なし (今すぐ着手可)
> 親: [PLAN.md](PLAN.md) / 契約: [CONTRACTS.md](CONTRACTS.md)
> 検査: `python3 tools/check_privileged.py --strict` が exit 0 になれば完了

## なぜ

v2 でユーザプログラムは CPL=3 で走る (M1)。CPL=3 では `hlt` や `outb` は
#GP になる。`tools/check_privileged.py` (2026-09 追加) が、現状の userland に
これらが 5 ファイル残っていることを検出した。**今は CPL=0 で動くが、リング3 を
入れた瞬間にこれらのプログラムは即死する。** M1 の前に潰しておく。

これは背骨 (M1a-) とは独立した小タスク。M1 着手前でも今すぐ実施できる。

## 対象と対応

`check_privileged.py --strict` が挙げる 5 ファイル。

### hlt → `sys_halt` KAPI (4 ファイル)

`sys_halt` KAPI は既にある (M0 で使用実績)。raw `hlt` を置換する。
プログラムは第 3 引数の `KernelAPI *api` (または crt0 が入れる global `kapi`)
経由で呼ぶ。

| ファイル:行 | 現状 | 置換 |
|---|---|---|
| `userland/cmds/less.c:81` | `__asm__ volatile("hlt");` | `api->sys_halt();` |
| `userland/cmds/more.c:103` | 同 | 同 |
| `userland/cmds/sleep.c:44` | `__asm__ __volatile__("hlt");` | `api->sys_halt();` |
| `userland/tests/mouse_test.c:119` | `__asm__ volatile("hlt");` | `api->sys_halt();` |

- 各ファイルで `api` / `kapi` のどちらの名前が使われているかはソース先頭
  (main の第3引数, crt0_c.c の global) を見て合わせる
- 待ち時間の意味は保たれる: `sys_halt` は次の割り込み (PIT 100Hz) まで止まる

### outb 0xA4/0xA6 → テスト廃止 (1 ファイル)

`userland/tests/flip400_test.c` は 400 ライン時のページフリップ (ポート
0xA4/0xA6 直叩き) を検証するテスト。**KAPI に隠すとテストの意味が消える**
(検証対象がポート I/O そのものだから)。かつページフリップは既に
`gfx_present` / `gfx_present_dirty` が内包しており、直叩きテストは現行では冗長。

**対応: flip400_test を廃止する。**
- `userland/tests/flip400_test.c` を削除
- deploy.yaml / package_defs.yaml に flip400 の記載は**無い** (PM 確認済み) の
  で、削除は `.c` のみ。ビルド対象からは C_TESTS の除外か削除で外れる
- ページフリップの回帰確認が要るなら `gfx_present` 経由の別テストで代替
  (このタスクの範囲外。必要なら PM に相談)

## 完了条件

```
python3 tools/check_privileged.py --strict   → exit 0 (== ユーザランドの特権命令 なし)
make programs                                 → エラーなし
make check                                    → exit 0
```

## 注意

- **C2 コーダー (Rust GUI) の排他ファイルには触れない** (CONTRACTS C7)
- 変更は userland のみ。カーネル (背骨の排他) には触れない
- flip400_test 削除で `make clean && make all` が通ることまで確認
