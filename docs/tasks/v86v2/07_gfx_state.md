# 07. グラフィック状態の引き渡し (Phase 3-5) — 完了

> 作成: 2026-08-09 / 完了: 2026-08-10
> 前提: [06_pic_plan.md](06_pic_plan.md) まで完了
> 結果: **素の NP21/W とスクリーンショットがピクセル単位で一致した**

---

## 1. 何が起きていたか

`v86 -b /host/Ys.D88` で Ys I のタイトル画面は描画されていた。
GVRAM の中身を素の NP21/W と比べるとプレーン 0/1/2 は**バイト単位で一致**する。
つまりディスクも描画も正しい。**壊れていたのは表示のされ方だけ**だった。

| | 素の NP21/W | OS32 の V86 (修正前) |
|---|---|---|
| 見え方 | 640x400 に 1 枚 | 縦半分に潰れた絵が上下に 2 枚 + 右端に断片 |

> プレーン 3 (`E0000`, 輝度) が空なのは**正常**。
> PC-9800 Bible 2-7 に「8色モードではプレーン0〜2の3枚を使用します。
> **プレーン3は存在しません**」とある。Ys は 8 色モードのディザ絵。

---

## 2. 原因 — ゲストはグラフィックを設定しない

観測モード ([§5](#5-観測モード--素通しポートを覗く)) でゲストの
グラフィック I/O を全部記録した結果、**セッション全体でこの 4 回だけ**だった。

```
OUT 0062 = 4B   at 0060:05F0     テキスト GDC
OUT 0062 = 0F   at 0060:05F5     テキスト GDC
OUT 00A2 = 0D   at 0060:0666     グラフィック表示開始
OUT 00A2 = 0D   at 0160:024D     同上
```

**解像度 (`68h`) もカラーモード (`6Ah`) もページ (`A4h`/`A6h`) も
GDC パラメータも CRTC も GRCG も EGC も、パレットすら一切触っていない。**

つまりゲストは **BIOS が IPL 直後に残した状態を丸ごと前提にしている**。
1987 年のゲームとしては当然の作りで、BIOS の設定を信じて必要な所だけ書き換える。

ところが OS32 は `gfx/gfx_core.c` の `gfx_init()` で 400 ライン・16 色・
描画ページ 1・スクロール PRAM (LEN=400) を設定したまま渡していた。
**この差がそのまま画面の乱れになる。**

> ディスクの物理ヘッドと同じ形の間違い。
> **ゲストが設定しないものは、こちらが実機と同じ初期値を用意する義務がある。**

---

## 3. どうやって突き止めたか

`68h` だけ 200 ラインに戻す実験を 2 回やって、どちらも外した
(1 回目は変化なし、2 回目は飛び越し表示になって悪化)。
**症状から 1 ポートずつ推測するのは無駄**だと分かったので、方法を変えた。

ゲストのポートは書き込み専用が多く読み戻せない。GDC のパラメータ RAM
(SYNC / CSRFORM / SCROLL / PITCH) に至っては読む手段が無い。
**ゲストの中からは原理的に答えが出ない**。

そこで NP21/W ai-debug fork に `GET /api/gdc` (`emu_gdc`) を足した。
エミュレータ内部の `gdc` / `gdcs` / `grcg` / `crtc` / `dsync` を
そのまま JSON で出すだけのもの。外から見れば全部ただの構造体フィールドで、
**「症状の比較」が「2 台の状態の diff」に変わる**。

素の NP21/W で `Ys.D88` を起動した状態と、OS32 の V86 セッション中とを
突き合わせた結果、差は **4 つだけ**だった。

| 項目 | 素の NP21/W | OS32 の V86 | 意味 |
|---|---|---|---|
| `mode1` (68h) | `0x99` | `0x89` | bit4 = 200 ライン |
| `mode2` (6Ah) | `0x00` | `0x01` | 8 色 / 16 色 |
| `s_csrform` (CCHAR) | `01 00 00` | `00 00 00` | LR = 2 ラスタ/行 |
| `s_scroll` (PRAM) | 全部 `00` | LEN=400 | 表示分割の長さ |

`mode1` `mode2` `disp_page` `access_page` `crtc` `m_*` `s_sync` `s_pitch`
`dsync` — それ以外は最初から完全に一致していた。

そして 4 つを合わせた後、**パレットだけが残った** (画面が白一色になった)。

| 項目 | 素の NP21/W | OS32 の V86 |
|---|---|---|
| `degpal` | `04 15 26 37` | `0f 0f 0f 0f` |

`04 15 26 37` は NP21/W の `defdegpal` そのもの、つまり BIOS 既定値。
OS32 の `palette_init()` は 16 色アナログのつもりで `A8h/AAh/ACh/AEh` を
叩くが、8 色モードではこの 4 ポートが**デジタルパレットレジスタ**になる。
最後の書き込み (idx=15, R=G=B=15) がそのまま `0F0F0F0F` として残っていた。

---

## 4. 直し方 — `kernel/v86_io.c`

`gfx_state_for_guest()` (セッション開始時) が BIOS 相当の状態を作る。

```c
io_out(GDC_GFX_CMD, GDC_CMD_STOP);       /* 表示停止。開始はゲストがやる */

io_out(MODE_FF2_PORT, MFF2_8COLOR);      /* 6Ah = 8 色 */
io_out(MODE_FF1_PORT, MFF1_200LINE);     /* 68h = 200 ライン */

io_out(GDC_GFX_CMD, GDC_GFX_400LINE);    /* CCHAR (4Bh) */
io_out(GDC_GFX_PARAM, 0x01);             /*   P1 下位 5bit = LR-1 */
io_out(GDC_GFX_PARAM, 0x00);
io_out(GDC_GFX_PARAM, 0x00);

io_out(GDC_GFX_CMD, GDC_CMD_SCROLL);     /* PRAM (70h) を全部 0 に */
for (i = 0; i < 8; i++) io_out(GDC_GFX_PARAM, 0x00);

io_out(PAL_IDX_PORT, 0x37);              /* デジタルパレット既定値 */
io_out(PAL_G_PORT,   0x15);
io_out(PAL_R_PORT,   0x26);
io_out(PAL_B_PORT,   0x04);

io_out(GDC_DISP_PAGE,   0x00);
io_out(GDC_ACCESS_PAGE, 0x00);
```

`gfx_state_for_os32()` (セッション終了時) が OS32 の設定に戻す。
16 色に戻してから `palette_init()` を呼ぶ (先に呼ぶとデジタルパレット側に落ちる)。
**VRAM は消さない** — ゲストが描いた絵を残すため。

### 落とし穴 2 つ

- **`mode1` bit4 と CCHAR の LR は必ずセットで変える。**
  bit4 は「奇数ラスタを捨てる」で、GDC 側の行あたりラスタ数と揃っていないと
  飛び越し表示になる。§3 の 2 回目の失敗はこれ。
- **デジタルパレットのポートは 8 色モードにしてから書く。**
  16 色モードのままだと `A8h` は `palnum` (アナログパレット番号) に化ける。

---

## 5. 観測モード — 素通しポートを覗く

`kernel/v86_io.c` の `v86_io_observe`。

素通しにしたポートは `#GP` を起こさないので `v86_iolog` に何も残らない。
「ゲストがグラフィックをどう設定したか」を知りたいとき、この盲点が
そのまま行き止まりになっていた。

観測モードでは対象ポートを I/O 許可ビットマップから落とし、
ハンドラ側で **ログを取ってから実ポートへ中継する**。
動作は素通しと同じまま、値と発行元 CS:IP が全部見える。

```bash
# 有効化 (再ビルド不要)
emu_write_mem addr=v86_io_observe hex=01000000
v86 -b /host/Ys.D88
emu_read_mem addr=v86_iolog len=1024
```

対象は GDC / モード F/F / CRTC / GRCG / EGC。
常時有効にすると `#GP` が増えて重いので既定は 0。

> **この 1 手で「ゲストは何も設定していない」が確定した。**
> カウンタでもログでも見えなかったのは、見たいものが
> 「トラップされない側」にあったから。
> 同じ盲点は他にもある (FM 音源のポートなど)。

---

## 6. 検証

| 確認 | 結果 |
|---|---|
| 素の NP21/W と V86 のスクリーンショット差分 | **差分ゼロ** (640x400 全画素一致) |
| セッション中の `emu_gdc` | 4 項目 + `degpal` すべて素の NP21/W と一致 |
| セッション後の `emu_gdc` | OS32 の値 (`mode1=0x89` / `mode2=0x01` / PRAM LEN=400 / ページ 0,1) に復帰 |
| セッション後の OS32 描画 | `blit_test` が全項目 PASS、配色も正常 |
| `v86 -t` | OK |

```bash
# 素の NP21/W 側 (FDD は引数、INI は ysprof.ini を指定)
np21x64w.exe ...\ysprof.ini C:\os32\Ys.D88
curl -s http://127.0.0.1:8025/api/gdc

# OS32 の V86 側
curl -sX POST http://127.0.0.1:8025/api/cmd -d "v86 -b /host/Ys.D88"
curl -s http://127.0.0.1:8025/api/gdc
```

---

## 7. 参照

- `/mnt/c/WATCOM/docs/PC9800Bible/2-7_グラフィック.md` — プレーン構成、
  8色/16色モード、モード F/F、GDC コマンド
- `/mnt/c/WATCOM/docs/PC9800Bible/4-3_I_Oマップ.md` — `68h`/`6Ah` の出力値一覧
- `np21w-src/src/io/gdc.c` — `gdc_o68` / `gdc_o6a` / `gdc_oa8`〜`gdc_oae` /
  `gdc_biosreset()` (`defdegpal = {0x04,0x15,0x26,0x37}`)
- `np21w-src/src/io/gdc_cmd.h` — パラメータ RAM の配置 (SYNC 0 / CSRFORM 9 /
  SCROLL 12 / PITCH 28)
- `np21w-src/docs/02-architecture.md` §12 — `/api/gdc` の設計
- `gfx/gfx_core.c` `gfx_init()` / `gfx/gfx_scroll.c` — OS32 側の設定内容
- [04 §7-1](04_implementation_status.md) — 現在地
