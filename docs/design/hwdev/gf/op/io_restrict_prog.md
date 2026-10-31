# hwdev/gf/op/io_restrict_prog.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/io_restrict_prog.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 32 IO_RESTRICT_PROG | I:621 `init_io_restrict_prog` |

CR3C の組を確認する。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| 32 IO_RESTRICT_PROG | s=(VGA[index]&m)>>shift、count個のu32から選びR32へ。11+4count B。範囲外は無書込みで記録・続行 | I:621–651 |

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 32 IO_RESTRICT_PROG | 11+4n：p16@1,i8@3,m8@4,s8@5,n8@6,d32@7,値32[]@11 | X(t,i); Q(t); 各kにC`[I(m),I(s),I(k),I(n)]`; L(d,値[k]) | J、比較はQの選択値を使用。全n選択肢、同じ値でも各W=eを保持 |
