# tools/tests/test_ide_pio.py — IDE 模型のビルドと変異

> 状態: **現行 (2026-10-31)**

製品 ide.c/ide_addr.c と [ポート模型](ide_pio_host.md) を GCC GNU11 で組む。
CHECK は MULTIPLE 実装・試験 inc を含む入力を宣言する。
`--mutate` は未変更コピーの成功を確認してから、要求分割・相転送・設定・失効を
一か所ずつ壊す。ビルド成功後の assertion (rc=1) だけを RED と数える。
実行は `check_slot.sh` を通す。実機時間・媒体の永続化は模型の対象外。
