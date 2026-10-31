# tools/tests/recover_tables_oracle.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN・GF6200_VPLL_PROBE・SLIM_HWDEV_DESIGN (D1 で移した)
ソース: `tools/tests/recover_tables_oracle.c`

**SUB7 (MEM、12020h〜1202Eh) と SUB6 (CORE、12000h〜12005h) の書込み 21 本の verify mask をすべて 0 にする**。nouveau (MIT、nv04.c の setPLL_double_lowregs) はこれらを書いた後に読み戻さず、rnndb にも読み戻せるビットの定義が無い。
書込みの列・値・順・保存値の復元は変えない。

main 0〜3 で verify が非零のまま残る 10 規則のうち **9 本を 0 にする**
VIDEO PLL の 5 本 (T:572-576、C040h・680580h・680578h・680508h・C040h、SUB6/7 と同じ PLL の列で nouveau の nv04.c は書いた後に読まない)
RESET の 184Ch の戻し
h0/h1 CR57 の戻し
1700h の容量の写し
**残す: h0 CR3C (T:581、COPY の写し) の verify FFh**。nouveau も CR3C を書いた後に IO_RESTRICT_* の選択の添字として読み直し、VBIOS は値が保たれる前提。
COPY の後は index の選択だけで data の書込みが無く、選択の読取りも REC に値を残さないので、old による後からの観測ができない — ここを外すと組の誤りを止めずに別の組の PFB を書き、原因を辿れない。

680508h
N:205,207の保存読取り。係数定義E:10–14,41。HWDEV read_safe=1（O/kdebug/generated/recover_gf_tables.inc:739）
680578h
N:206,208の保存読取り。E:26–29,88はNV31:NV34限定なのでNV44Aへの根拠はN。read_safe=1（同:740）
680580h
N:220–224のNV41以降の保存読取り。read_safe=1（同:615）
00C040h
N:257–259,272の保存→変更→復元。read_safe=1（同:612）
68050Ch
HWDEV行は不要。

## 3. 同値の確かめ方

- oracleは圧縮生成器から生成し直さない。旧型の別translation unitとしてリンクし、製品には入れない。

- oracle は旧 recover_gf.c のヘッダ・定数・参照配列・式関数を含む前処理済み C translation unit として固定し、元コミット・元ソースと fixture の SHA256・抽出コマンドを tools/tests/recover_tables_oracle.json に記録する（圧縮生成器から再生成しない）。
