# CPU の状態と例外

> 上位: docs/ppc/README.md

対象は 32 ビット OEA の G4。Book E・601・970 の定義を混ぜない。
PVR の上位 16 ビットは MPC7450 が `0x8000`、7455 が `0x8001`
(N/sys/arch/powerpc/include/oea/spr.h:89)。同じ G4 でも機能は型番で確認する。

## MSR のマスク

値は N/sys/arch/powerpc/include/psl.h:46 と同 :49 以降。
O/sys/arch/powerpc/include/psl.h:40 の共通ビットとも一致する。
CPU マニュアルのビット位置表記と混同しないよう、数値マスクで示す。

| 名前 | マスク | 意味 |
|---|---|---|
| VEC | `0x02000000` | AltiVec 使用可 |
| POW | `0x00040000` | 省電力制御 |
| ILE | `0x00010000` | 例外時の little-endian 選択 |
| EE | `0x00008000` | 外部割込み許可 |
| PR | `0x00004000` | 1 ならユーザーモード |
| FP | `0x00002000` | 浮動小数点使用可 |
| ME | `0x00001000` | machine check 許可 |
| FE0 / FE1 | `0x00000800` / `0x00000100` | 浮動小数点例外モード |
| SE / BE | `0x00000400` / `0x00000200` | 単一命令 / 分岐のトレース |
| IP | `0x00000040` | 例外プレフィックス選択 |
| IR / DR | `0x00000020` / `0x00000010` | 命令 / データのアドレス変換 |
| PM | `0x00000008` | 性能監視モード |
| RI | `0x00000002` | 復帰可能性の状態 |
| LE | `0x00000001` | 1 なら little-endian |

SRR0 は保存された実行位置、SRR1 は保存された状態・例外情報を扱う。
SPR 番号は SRR0=`0x01a`、SRR1=`0x01b`
(N/sys/arch/powerpc/include/spr.h:134)。
FreeBSD は例外入口で両者を退避し、復帰前に書き戻す
(F/sys/powerpc/aim/trap_subr32.S:96、同 :231)。
SRR1 を MSR 全体の単純なコピーとみなさない。POW/ILE は保存コピーに入らないとの
注意が N/sys/arch/powerpc/include/psl.h:90 にある。
[未] 各例外で SRR0 が指す命令と SRR1 の原因ビット・復帰条件は CPU マニュアルで補う。

## 例外の入口

下表は低位プレフィックス側のオフセット。根拠は
N/sys/arch/powerpc/include/trap.h:38。同ヘッダには他 CPU 専用の入口も含まれる。
[未] 7450/7455 ごとの実装有無、IP=1 の配置、reset の条件はメーカー資料で照合する。

| オフセット | BSD の分類 | 適用の注意 |
|---|---|---|
| `0x0100` | reset | 通常の割込み復帰と区別 |
| `0x0200` | machine check | 原因と復帰可否の確認が必要 |
| `0x0300` / `0x0400` | data / instruction storage | データ側と命令側を区別 |
| `0x0500` | external interrupt | 装置の IRQ 番号とは別 |
| `0x0600` | alignment | 非整列アクセス等 |
| `0x0700` | program | 原因は SRR1 等で区別 |
| `0x0800` | floating-point unavailable | FP 使用不可時 |
| `0x0900` | decrementer | 時計の割込み |
| `0x0a00` | I/O controller interface | ヘッダでは 601 専用 |
| `0x0b00` | 本調査のヘッダに定義なし | [未] 予約の正式分類 |
| `0x0c00` | system call | `sc` の入口 |
| `0x0d00` | trace | トレース |
| `0x0e00` | floating-point assist | 型番別に確認が必要 |
| `0x0f00` / `0x0f20` | performance / AltiVec unavailable | ヘッダでは 604/750/7400 系、7400 系 |
| `0x1000` / `0x1100` / `0x1200` | 命令 miss / load miss / store miss | ヘッダでは 603 専用。G4 の miss 入口としない |
| `0x1300` / `0x1400` | instruction breakpoint / system management | ヘッダでは 604/750/7400 系 |
| `0x1500` | 本調査のヘッダに定義なし | [未] 745x の正式分類 |
| `0x1600` / `0x1700` | AltiVec assist / thermal management | ヘッダでは 7400 系、750/7400 系 |

この表は BSD の名称と適用注記の整理であり、745x の完全な例外仕様表ではない。
