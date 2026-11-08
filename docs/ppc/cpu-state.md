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
SRR0 の代表例と復帰条件は末尾に補足する。
MPC7450UM 4.3.2 では SRR1[0,7〜9] は 0、[1〜5,10〜15] は例外固有情報、
[6,16〜31] は MSR の対応ビットを保存する (MSB=0 の仕様表記)。
例外入口では IR/DR をともに落とす。SRR1 の未保存ビットを MSR と同一視しない。

## 例外の入口

下表は低位プレフィックス側のオフセット。根拠は
N/sys/arch/powerpc/include/trap.h:38。同ヘッダには他 CPU 専用の入口も含まれる。
以下の BSD 共通ヘッダ表と、その後の MPC7450UM による型番別補足を併読する。

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
| `0x0b00` | 本調査のヘッダに定義なし | MPC7450UM 表 1-3 では予約 |
| `0x0c00` | system call | `sc` の入口 |
| `0x0d00` | trace | トレース |
| `0x0e00` | floating-point assist | 型番別に確認が必要 |
| `0x0f00` / `0x0f20` | performance / AltiVec unavailable | ヘッダでは 604/750/7400 系、7400 系 |
| `0x1000` / `0x1100` / `0x1200` | 命令 miss / load miss / store miss | ヘッダでは 603 専用。745x の STEN 条件は下記 |
| `0x1300` / `0x1400` | instruction breakpoint / system management | ヘッダでは 604/750/7400 系 |
| `0x1500` | 本調査のヘッダに定義なし | MPC7450UM 表 1-3 では予約 |
| `0x1600` / `0x1700` | AltiVec assist / thermal management | ヘッダでは 7400 系、750/7400 系 |

この表は BSD の名称と適用注記の整理であり、745x の完全な例外仕様表ではない。

## 745x の補足 (MPC7450UM Rev 3.1)

表 1-3、4.6.12〜4.6.18 により、0x0a00〜0x0bff・0x0e00・0x1500・
0x1700 は予約。0x0e00 の FP assist と 0x1700 の thermal は実装しない。
0x0f00 は performance、0x0f20 は AltiVec unavailable、0x1300 は IABR、
0x1400 は system management、0x1600 は AltiVec assist。
HID0[STEN]=1 の場合には 0x1000/0x1100/0x1200 の ITLB/load/store miss を使う。
上表の BSD ヘッダの「603 専用」は 745x のソフトウェア探索機能を網羅しない。
この適用範囲についてはメーカー資料の条件を採り、通常の hardware search と区別する。

MSR[IP]=0 の基底は 0、IP=1 は 0xfff00000。hard reset は 0xfff00100 へ入り、
soft reset は IP に従う (MPC7450UM 4.6.1)。
DSI/ISI・alignment・非 FP の program 例外では原因命令、sc では次の命令を SRR0 に保存する。
外部割込み・DEC では復帰して実行する次の命令を保存する (同 4.3、4.6、PEM 6.4)。
復帰は SRR0/SRR1 を用いる rfi だが、machine check は RI と原因・破損状態を確認し、
通常例外と同様に無条件復帰しない (MPC7450UM 4.3.3〜4.3.4、4.6.2)。
[未] 全例外の SRR1 原因ビットの一覧と CPU revision 別 errata。
