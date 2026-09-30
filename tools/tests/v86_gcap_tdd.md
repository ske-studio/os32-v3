# v86_gcap — RED → GREEN の記録

票: [`docs/tasks/realhw/TASK_PEGC480_REALHW.md`](../../docs/tasks/realhw/TASK_PEGC480_REALHW.md) §3 段 1
対象: `kernel/v86_gcap_math.c` (記録・通すポート・打ち切る命令・並びの判定・戻しの判定) /
`kernel/v86_gcap.c` (段取り、`--target` で i386-elf に通すだけ) / `kernel/v86_test16_gcap.asm` (ゲスト)
試験: `tools/tests/test_v86_gcap.py` + `tools/tests/v86_gcap_host.c`
実行: `make check-v86-gcap-host` (`check-par` の列)

## 何を作っているか

`v86 -g` は実機の ROM の INT 18h AH=31h / AH=30h を V86 で呼び、その間に ROM が出した OUT を
畳まずに記録する。PEGC 640x480 の SYNC が NP21/W 由来の値のままで、実機の ROM と同じか確かめる手段が
無い (uPD7220 の SYNC は書き込み専用) から。

## なぜホストで見るのか

- **AH=31h の bit の並び** (NP21/W の bit2 / Bible 3-2 の bit3) は NP21/W では bit2 しか返らず、実機が
  どちらかは実機の回まで分からない。両方・どちらでもない・印のまま・両方で正しい唯一の値 (AL=08h BH=00h)
  を表で固定する。
- **溢れ** (513 件目) と **戻しの判定** (③ が断られたら ④ を呼ばない、代行レビュー P2-2) は実機の ROM が
  そう振る舞うかが分からず、狙って踏めない。
- ゲストのバイト列が正本の asm を組んだものと同じか、入口と期待 IP が命令を指すか (nasm で組み直す)。

## ケースと変異

ケース 9 本: `port_list` / `insn` / `record_order` / `overflow` / `in_table` / `pass_ops` / `decide` /
`mode_31k` / `restore`。変異 31 本 (全部 RED)。主なもの:

| 変異 | 落とすケース |
|---|---|
| 09A8h を通さない / 偶数に絞らない (PIT を ROM に触らせる) | `port_list` |
| 66h の向きの取り違え / OUTSW を打ち切らない | `insn` |
| 溢れの境界が 1 ずれる (配列の外へ書く — 番兵で検出) | `overflow` |
| 16 ビットの OUT / IN を 8 ビットの口で通す | `pass_ops` |
| 溢れたら実機へ通すのをやめる | `pass_ops` |
| 並びの予約 bit・行数・解像度・31kHz の条件を外す / 両方で正しい値で決めてしまう | `decide` |
| AL bit0 (ラスタスキャン) を弾く / 30h へ渡す AL で bit0 を落とす / 予約の bit1 を許す (2026-09-29、実機 Ra266 の AX=310Dh) | `decide` |
| ③ が断られても ④ を呼ぶ / ③ が途中で終わっても呼ばない / ④ の途中終了を ROM とみなす | `restore` |

## ゲスト内の自己試験 (`v86 -g -t`)

ホストでは見られない「#GP ハンドラの差し込み口・V86 の中での幅・打ち切り・時間の見切り」は
`v86 -g -t` がゲスト内から見る (捕まえたポートは実機へ通さない)。`selftest_fail` のビット:
`0x001` 列が走らない / `0x002` OUT 列 / `0x004` IN の値 / `0x008` IN の表 / `0x010` 口の幅 /
`0x020` seq / `0x040` 溢れ / `0x080` OUTSB / `0x100` OUT DX,EAX /
`0x200` **時間の見切り** (代行レビュー P2-1: IF を立てて INT で入り、I/O も #GP も出さずに回るゲストが
0.3 秒で `V86_EXIT_TIMEOUT` になり、INT の先で IF が立っていること)。

NP21/W で 2026-09-26 に `-g -t` と `-g` を通した (PM、票 §3 段 1 の記録)。`0x200` を足した版は未実施。

### 2026-09-29 — 実機 Ra266 の AH=31h が AX=310Dh (AL bit0)

実機の `v86 -g` (44bd0fe) が `AH=31h value not recognized (status=2)`、`R 31h : AX=310d BX=0100 layout=-` で
30h まで進まなかった。AL=0Dh は bit2 並び (NP21/W の 08h|04h) に **bit0** が足された形。bit0 は
[US] memsys.md 0000:0459h bit3「INT 18h Function 30h,31h のモード設定情報 bit 0」(ラスタスキャンモード、
BIOS とは値の意味が逆) で、並びの判定が予約 bit として弾いていた。

- RED: `decide` に `v86g_decide(0x0D, 0x01)` == BIT2 (30h へ AL=0Dh BH=32h) を足して落ちることを確認
  (旧判定を戻した変異 16 が RED = 旧判定は NONE を返す)。
- GREEN: 両方の並びで AL bit0 を許し、30h へ渡す AL に bit0 を保つ (`V86G_AL_RASTER`)。戻しは従来どおり
  31h の値そのまま。変異 5 本を足した (全部 RED)。
