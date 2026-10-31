# include/gfx_hal.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `include/gfx_hal.h`

GUI/グラフィックコンソール→RAM CLIENT→`gfx_present/dirty`→`backend_geforce`→video service→GF 部品。KAPI とアプリ側描画経路を変えない（[HW1]、`O/include/gfx_hal.h:37–97`）。

## 内部設計と契約 (D1)

内部の記述子は**台帳の SURFACE への参照**として扱う (中身: owner・面種別・物理範囲または VRAM 範囲・形式・pitch・プレーン配置・権限・キャッシュ属性・世代。全部を公開 ABI にはしない)。**lease の結果 = 呼出元 AS の lease 窓の仮想番地は、記述子とは別に返す**。
**カーネル内部の仮想番地と AS に返す lease 番地を分け**

**PEGC 直描きは全画面 lease に限る** (D19)。

present は gshell (CPL=0) が窓へ直接、Cirrus は BLT 呼び出し。**v1 の描画モデル (R1〜R3) を変えない**

カーネルの最小限 = モード設定 / probe / enter / leave、窓の写像 (supervisor+PCD)、present / flip / VSYNC、能力表、HW エンジンの薄い口、**lease**。外へ出すもの = BB の所有 (gshell)、描画 (libos32gfx)、合成・カーソル・ドラッグ枠・FEP 窓 (gshell)。

```c
/* include/gfx_hal.h / gfx/gfx_core.c: no probe, no mode switch */
int gfx_ver_state(GFX_ScreenInfo *out, u32 *kind, u32 *active);
```

- **commit 前の描画を表示面に出さないのはバックエンドの責任** (2026-09-05): ソフトウェア
  バックエンドは主記憶バックバッファ、アクセラレータ (Cirrus) はカード VRAM の**非表示領域**
  をクライアント面にし、commit で表示面へエンジン BLT する。クライアントから見た保証は
  同じ (半端な状態は見えない)。表示面へ直接描くバックエンドは作らない。

> [要確認] commit 前の非表示保証と、9801 バックエンドの既存表示経路との適用範囲は要確認。

`gfx_hw_fill_rect` /
`gfx_hw_blit`。CPU バックエンドでは `OS32_ERR_NOSYS` (-10) を返し、共有ライブラリは
`GFX_CAP_HW_*` を見て CPU 実装へフォールバックする。

### G7. カウンタ (DESIGN §8)

`stats() -> {present_bytes, hw_ops, io_accesses, commits}`。累積。デバッグ用で契約の一部
(NP21/W ではこれで性能を見積もる)。

**テキスト VRAM でクローム (メニューバー・ステータス行) を描くのは 9801 バックエンドの最適化に格下げ**。GUI 本体はクロームをグラフィックに描く前提
ウィンドウアクセラレータは表示出力を 98 側から切り替えるのでテキスト面が出ない。

## 2. バックエンド抽象 (採用済み 3 点)

**バックエンド関数表 + 描画プリミティブ段階の HAL** (2026-09-04 改訂)
libos32gui は HAL の描画プリミティブ (塗り・矩形転送・文字・ビットマップ) と `present_rect` だけを呼ぶ。9801 / PEGC バックエンドはそれを主記憶のバックバッファ (9801 = 4 プレーン、PEGC = 8bpp) に描いて present し、Cirrus バックエンドは画面の実体をカードの VRAM に置いてエンジン (BitBLT / 塗り / 色展開) に流す。この違いは HAL の中に閉じ、GUI には見せない (Win3.1 の GDI とディスプレイドライバの分担と同じ)。present 時にチャンキー→プレーン変換する方式は 386 では重すぎるため不採用。

> [要確認] 現行 include/gfx_hal.h の GfxBackend に文字・ビットマップの関数ポインタはなく、CPU 共通実装が担う。

**能力ビット**
テキスト面オーバーレイ可否 / HW 塗り / HW ブロック転送 / 画面サイズ (400・480・それ以上) / 色数を問い合わせ、GUI 側は決め打ちしない。

## 6. HAL とドライバの層構造 (承認済み 2026-09-04)

**GUI (libos32gui) はデバイスを意識しない。** 触ってよいのは HAL (§2 のバックエンド表)
だけで、ポート・VRAM アドレス・機種名を GUI 側に書かない。画面サイズ・色数・画素形式は
起動時に HAL から受け取り、それ以外の前提を持たない。

## 7. ドライバの書き方の指針

新しいバックエンド / グルーを書く人が守ること。守れば GUI 側は無変更で動く。

1. **HAL の契約を満たす**: probe / init / shutdown、能力ビットの申告、描画プリミティブ
   (塗り・矩形転送・文字・ビットマップ)、`present_rect`、パレット設定、`enter` / `leave`
   (表示出力の切替。フルスクリーン GFX プログラムの前後で呼ばれる)、カウンタ (§8)。
   ソフトウェアバックエンドはプリミティブを共通の CPU 実装 (バックバッファ記述子 =
   先頭・ピッチ・画素形式を渡す) に委ね、present だけ自前で書けばよい。アクセラレータ系は
   プリミティブをエンジンで実装し、未対応のものは NULL にして CPU 版へフォールバックする。
2. **能力ビットは正直に**: 画面サイズ・色数・テキスト面の可否は実際に使える値だけを立てる。
   GUI はこれを信じて決め打ちを一切しない。

## 8. ボトルネックの所在 (採用 2026-09-04)

CPU との兼ね合いで、どこが律速になるかの整理。設計判断 (B1 改訂・§7-1 の
プリミティブ契約・カウンタ) の根拠。

共通の土台:

- **VRAM アクセス時間は CPU クロックに比例しない。** 表示コントローラが表示走査で VRAM を
  読み続けており、CPU アクセスは調停ウェイトを挟む。486DX2 でも 386 でも 1 アクセスの
  時間は大差ない。
- **データ幅は 16bit。** 32bit アクセスはバス側で 16bit ×2 に割られ、`rep movsd` にしても
  書き込み回数は減らない。9821 でも 98 標準グラフィックの VRAM 経路が 32bit 化された
  記述は資料に無く、16bit として扱う。
- **I/O ポートはさらに遅い。** OUT/IN 1 本が VRAM 書き込み数回分。レジスタを何本も叩く
  操作は小さい仕事ほど損 ([HW1] の根拠)。
- **VRAM の読み出しは書き込みより遅い** (値が返るまで CPU が止まる)。VRAM を読まない
  設計を維持する。

計測方針:

- **NP21/W はこれらのウェイトを再現しない**ので、性能はエミュレータで測れない。
- HAL に **カウンタ** (present したバイト数 / 発行したエンジン操作数 / I/O アクセス数) を
  持たせ、エミュレータでは回数を測り、実機で一度測ったコスト定数を掛けて見積もる。
  回数は機種に依らず正確に取れる。

```c
/* const GfxMode *modes;
   u16 mode_count, default_mode;
   const GfxMode *selected_mode; */
/* gfx_hal.h の構造体定義後だけに置く inline（mode.h には置かない）。 */
static inline const GfxMode *gfx_backend_mode(const GfxBackend *be);
static inline const GfxMode *gfx_mode_pick(const GfxBackend *be, int legacy200);
/* gfx_mode_pick の legacy200 分岐。表の範囲を検査済みとして示す。 */
if (legacy200) {
    for (u16 i = 0; i < be->mode_count; ++i)
        if (be->modes[i].format == GFX_FMT_PLANAR4 &&
            be->modes[i].height == GFX_HEIGHT_200) return &be->modes[i];
}
return &be->modes[be->default_mode];
```

- 循環 include を避けるため、GfxMode と layout は gfx_mode.h、Backend を使う inline は gfx_hal.h の構造体定義後だけに置く。
- GfxBackend は既に非 const の実体。g_backend の const ポインタは維持し、選択関数へ実体を渡して selected_mode だけ更新する。
- selected_mode は静的表内のポインタ。動的確保なし。init(void)/query/prepare の関数型は変更しない。

CirrusModuleContext の高さポインタも維持する。

- GFX_CAP は PC98 の PAGE_FLIP の実状態、PEGC の TEXT_OVERLAY、Cirrus の HW_FILL/HW_BLT を維持する。
- パレットリースは PC98 mask=0xFF7E、packed は first=16/count=240、reserved はゼロ。G5 のアプリ契約を変えない。
