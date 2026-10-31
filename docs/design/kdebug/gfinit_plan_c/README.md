# kdebug/gfinit_plan_c/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4C_DESIGN.md・GF_STAGES_DESIGN.md (D1 で移した)
ソース: `kdebug/gfinit_plan_c/`

### baseline.inc

ソース: `kdebug/gfinit_plan_c/baseline.inc`

s(1C/33/3C/4B/52/60081C/600850/680524)を採取

- s(x) の 8 値は main4 後・入口 CHECK の後・Q 前に READ/SELECT で採取し、既存 recover capture を消費先ごとに割り当てる；CR52 は (s+4)&FF、CR1C/33/4B と SEL_CLK の変換は §3 の式のまま（nouveau crtc.c:494,532,542–565,685–689、保存時刻だけ OS32 の cold 起動に合わせる）。

CR52は毎回 `(初回s+4)&FF`、C040の復元先も初回値で固定する。

### checks.inc

ソース: `kdebug/gfinit_plan_c/checks.inc`

入口CHECK：ID/BAR/Command=0102、cfg50/54と1850/1854=0、route&3=0、main4完了。1854の再書込みは不要。

新しい要約ログ行は設けず、入口/出口CHECKとgf_busで読む。

- 入口 CHECK は既存 token の identity、保存した BAR 3 本、Command=0102、cfg50/54・1850/1854=0、route&3=0 を比較する。main4 後の C entry から Q 前の採取へ進む。

- CHECK の node は source/key/mask/expected/keep/result-slot を hash に含める。524・CR33・C040 は既存 single-use capture を使い、結果は `gfinit_plan.c_checks` に保持する。510 は mask=0、TEST は bit20 のみで sense を除外する。

builderの `c_check_in`（`gfinit_plan_c.c:184–201`）が発行する全CHECK nodeに入口の印を付け

recordで不一致を非停止にするのは **印のない `REC_CHECK_TARGET` だけ**。入口TARGET（Command・cfg50/54・1850/1854・BARを含む）とIDENTITY/ROUTE/CELLは照合を続ける。読取り/送信失敗は印に関係なくS0へ進む。

印はnodeに付随する内部flag（`#define`）とし、CHECK番号やレジスタ番地で入口/出口を推定しない。requiredは印の有無によらず必須CHECKを照合する。
