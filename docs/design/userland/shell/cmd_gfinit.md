# userland/shell/cmd_gfinit.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4C_DESIGN・GF6200_S7_SHELL・GF_STAGES_DESIGN (D1 で移した)
ソース: `userland/shell/cmd_gfinit.c`

## C 書込み照合の実行時指定 (2026-10-29)

`gfinit prepare <sid> --mode-c --allow-write [--verify required] [--no-verify-file <path>]`
で A2 の計画を固定する。
`--verify required` は出口 CHECK に非零 mask のある対象を必須として維持し、
それ以外の C 書込みを記録だけにする。SELECT、入口/出口 CHECK、旧 main 列は変更しない。
`--no-verify-file` は OS32 上のファイルを読み、指定した C 書込みを記録だけにする。
両方を指定した場合、ファイル指定を優先するが、出口 CHECK は維持する。
ファイルは 1 行 1 本の `MMIO 680a24` / `CR 1c` / `SR 01` (数値は十六進、0x も可)、
空行と `#` 以降の注釈を許可する。最大 64 指定、注釈を除く各行は 63 byte まで。
形式不正・読込み失敗・C の書込みに一致しない指定は A2 を拒否する。

```c
/* userland/shell/cmd_gfinit.c: shell.hの後、ゼロ初期化して入力のみ設定 */
#include "../../kdebug/gfinit_wire.h"
rc = g_api->kdebug_call(op, &q, sizeof(q));
```
| コマンド | op・役割 |
|---|---|
| `gfinit inspect <sid>` | 5、flags=0、ROM/version.chip確認 |
| `gfinit abort <sid> --allow-write` | 10、IDLEではlease/ROM解放、それ以外は既存終端 |
| `gfinit size-bars <sid> --allow-write` | 6、A1測定と終端 |
| `gfinit prepare <sid> --allow-write` | 7、A2後PAUSED |
| `gfinit run-one <sid> <main> --allow-write` | 8、main 0〜4の指定1本 |
| `gfinit verify <sid> --allow-write` | 9、全main後のA4・終端 |

- sid/mainは十進u32。過不足・未知語・負数・桁溢れ・main範囲外はusage/終了値2、呼出し0。正常0、拒否/失敗1。出力4欄は毎回ゼロ。
- コマンド引数sidは現在値（statusまたは前回応答）を使う。PAUSED中は補助命令を挟まない。自動再試行・自動次段は設けない。
- KDEBUG=0でも登録し既存NOSYSを表示、失敗終了、装置I/Oなし。API版不足/NULLはunavailable。hwdev未公開の-1と区別する。

## 4 書込みの op の既定
- op6〜10は毎回--allow-write必須、無指定は呼ばない。inspectは旗を拒否。旗はGF_ALLOW_WRITEへ写すだけで、追加の対話確認・権限分離は設けない。

`GF inspect-end` 行は shell が kdebug_call の戻り値から必ず出す (hwdev が早く失敗して 1 行目が無くても、終わりの行で rc が分かる)。

- inspectの呼出し失敗はinspect-endのrcだけで示し、abort失敗は既存recover先例の `gfinit: rejected rc=`、abort成功は追加行なしとする；API不足/NULLは呼ばずunavailableとする。

- size-bars/prepare/run-one/verify は abort と同じく非ゼロ rc を `gfinit: rejected rc=`、ゼロ rc は追加行なしとし、REC の結果判定は既存 monitor に任せる。

`gfinit c-only --allow-write --verify record|required [--no-verify-file PATH]`

## 4. 照合とS0

既定は `required`。指定はCの開始ごとに受け取り、形式／対象の誤りは書込み前に返す。
現 `MMIO 680a24`／`CR 1c`／`SR 01`、16進、空行・#コメント、64指定の形式を継承する（`userland/shell/cmd_gfinit.c:21`）。実行中にファイルを再読しない。
