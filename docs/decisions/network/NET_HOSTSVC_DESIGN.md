# NET_HOSTSVC_DESIGN — ネットワーク越しの Host Services の詳細設計

> 出所: ~/os32-tmp/run/net-hostsvc/DESIGN.final.md (astra 起草 r1→r3、Opus レビュー 3 回、R1〜R3 は PM が反映)。
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_NET_HOSTSVC が持つ。

対象: main 3a0f9ab4、2026-11-05。設計案、未実装・未実測。REVIEW.r2 の N1〜N7・PM 決定と USER_DECISIONS_r3 を反映する。
作成者: GPT-6 系 Codex（実行環境から詳細モデル ID は確認できない）。独立 Opus レビューは PM の次工程。

## r2→r3 の変更
- N1: ホストで起動前のFDへNETCD/peer/SFS_LANを書き、netcdが先にcd0を取る手順と道具の段を追加。
- N2: nic_lgy98.cをvmkernelへ置き、lgy98.cと同じLGY98条件でビルド。
- N3: カーネルがALIVE観測世代を公開し、netcdは次アクセスでalive_seenと比較（呼び戻しなし）。
- N4: 1000ms期限切れは該当handleだけHOSTDOWN、session再同期は既存RTO×5/PROBE×5条件を維持。
- N5: B1/A9/H5でHELLO payloadを拡張し新旧長を受理。
- N6: TIMEOUTはKAPI_SPECの誤り番号予約表で確保しSDKと共有（番号追加だけでは版上げなし）。
- N7: SFSファイルはパス＋offset、遠隔handleの再取得はnetcdのBOPENだけ。
- PM-LIST: os32.file/iso9660をorigin=sfsの静的行、予約スタブをorigin=stubとして掲載、IPCは戻さない。
- PM-TICK: PC98_TICK_NE2Kのne2k_timer_tickを含む共通tickの順序を明記。
- U-LIST: 標準・スタブ・別設計の一覧を追加し、CD名をos32.iso9660、DVD予約名をos32.udfに固定。
- U-TIME: 別設計の順番と結び目のみ記載し、ALIVEのホスト世代変化時は最初に時刻合わせへ渡す。

## 1. 実物の確認
以下の 12 件は、今回の前提と現在の実装との差分（既存実装の不具合との断定ではない）。
| # | 前提／票 | 実物と必要な変更（基点での file:line） |
|---|---|---|
| 1 | 常時 /host | `fs/serialfs/serialfs_session.c:225,231,294` は shell owner・空き mount を要求、`:145` で終了時 unmount。常駐寿命を分離する。 |
| 2 | ホスト側 --allow-write | `tools/lanfs_host.py:154` の引数は iface/root/peer のみ、`:169` は read only。オプション実装が必要。 |
| 3 | cdinst には /cd | `userland/system/cdinst.c:33,37,115` は `/cd0` と `cd0` を使用。本設計の正式名は `/cd0`、cdinst は変更しない。 |
| 4 | 一般の仮想 CD に ISO を載せるだけ | `fs/isofs/iso9660.c:42` は Device 読みだが `:73,406,457` は ATAPI 世代直結、`:56` は負値を IO に潰す。世代・エラーも一般化する。 |
| 5 | L-C NIC 境界 | `serialfs_session.c:59` は e82557 固定、`net/link.c:241,305` は ne2k 送信・受信ページ数直結。`drivers/net/ne2000/lgy98.c:123` が link_init を所有。 |
| 6 | 0.5〜1秒・down 状態なし | `fs/serialfs/sfs_proto.h:121–125` は初動 2秒・再送3回・連続失敗3回で DEAD。LAN に別の要求期限が必要。 |
| 7 | Ubuntu の全サービス | `tools/host_agent.py:1134,1156,1200,1254` は win32/WSL。L-D2 (`TASK_LAN_82557.md:47`) はまだ必要。 |
| 8 | NOTIFY とメールボックス | `net/link.h:31–40` の op は 1〜10 のみ。リンクの通知用系列と KAPI を追加する。 |
| 9 | 張りっぱなしのSFS | `tools/lanfs_host.py:83` はseqの範囲を制限、`fs/serialfs/sfs_client.c:177–178` はDEAD_SEQ。閾値更新が必要。 |
| 10 | ATAPIとnetcd共存 | `drivers/ata/cd_body.c:14` はcd0固定、Device登録の同名拒否（レビュー指摘2）と衝突するため空きcdNへ。 |
| 11 | EtherType共用 | `net/link.h` のEOF=5とSFS ENQ=0x05が一致。MARKの3バイト全体で判定する。 |
| 12 | HOSTDOWNへの整理 | `net/link_host_api.inc:92,96,117` は非UPでSTALE。§6の表へ変更する。 |
- `TASK_LAN_82557.md:5,10,45–48` の達成は LAN hsync、印刷/wget は後回し。今回この後回しを解除する。
- `HOST_SERVICES_PLAN.md:19–74` の v51の非同期APIを維持、ハンドルは2→8へ。`LINK_PLAN.md:134–162` の20Bヘッダを使う。参照先の archive は未読。
- `docs/06_filesystem.md:117,319–337` の HostDrv は hypercall。実機経路は SerialFS とし、HostDrv をネットワークへ改造しない。
## 2. 全体の形
```text
アプリ ─ VFS /host ─ SerialFS常駐 ─ SFS client ─┐       ┌ lanfs_host ─ HostFS / ISO像
cdinst ─ /cd0 ─ iso9660 ─ netcd ─ SFS block ───┤NIC共有┤
アプリ ─ host_* ─ link(要求/本文/NOTIFY) ──────┘       └ lan_bridge ─ host_agent ─ Ubuntu
                 └ 通知リング → 内部ALIVE観測 / KAPI / GUI出来事
                    NIC = 82557(lan.mod) または LGY-98、単一受信所有者
```
- SFS はファイル・仮想媒体、link は TIME/CLIP/PRINT/GET と通知。共に EtherType **0x88B5**。
- Ethernet payloadは長さ>=3かつSFS MARK (`05 53 46`) の3バイト一致でSFSへ、それ以外はlinkとして検証。link EOF op=5とSFS ENQ=0x05は同値なのでbyte0判定は禁止。長さ・宛先・固定 peer MAC を先に確認。暗号・認証・ソース許可リストは追加しない。
- ホストでは raw の lanfs_host と lan_bridge が同居できるが、bridge は SFS MARK を転送しない。NIC の送信 MAC は両サービスで同じにする。
- OS32 は協調切替。IRQ は受信を積むだけ。100Hz の共通 tick が上限付き受信・link の状態機械を駆動し、VFS 呼出しが SFS 応答を待つ間も tick は進む。
```c
/* 新規 net/nic.h。戻り値は0/負、recv空はAGAIN。irqの登録/解除は各adapterの寿命内。 */
struct NetNicOps {
 int (*tx)(const void *, u32); int (*rx)(void *, u32, u32 *);
 void (*mac)(u8 *); void (*poll)(void); u32 (*rx_credit_bytes)(void);
};
int net_bind(const struct NetNicOps *ops); /* 起動時1回。共有者はNICを直接drainしない */
int net_rx_take(u16 protocol, void *out, u32 cap, u32 *len); /* SFS/link別の固定キュー */
/* 接続の擬似コード。NIC bind時に登録、LGYのlink_tick直接呼出しは除去。 */
tick_register(PC98_TICK_LINK, net_tick); /* IRQ0: NE2K(ne2k_timer_tick) → E82557 → LINK → LGY98（各PC98_TICK_*順） */
void net_tick(void) {
 if (!bound || (is_82557 && !active_ref_held)) return;
 ops->poll(); demux_bounded(); link_tick(); /* 待たない、再入禁止 */
}
/* net_rx_take: IF保存/cli→自分のqueueが空ならpoll/demuxを1回→取得→IF復元。
 * taskからlink状態機械は進めない。82557のbind前にtaskでsession_acquire、
 * unbindはcliでcallbackを外してからrelease。IRQ0でref取得・解放はしない。
 * recvはIF=0を保存したbounded rx_scan/copy/recycleのみ、待ち/yieldなし。
 * PC98_TICK_E82557が先に回復、共通pollは回復tickを二重実行しない。 */
```
- LGY の page credit は adapter で byte に換算、82557 は descriptor/queue の空きを使う。WINDOW は byte credit と link 自身の空きの小さい方を既存単位へ切り下げる。
- RX は各経路4枠×1514Bを上限に事前確保（既存キュー再利用可）。両方を毎 tick 処理、片方の待ちが他方を食べない。SFS reset も自分のキューだけ捨てる。
## 3. 実機の /host 常時マウント
- 新規 `serialfs_persistent.c` が常駐 client と mount generation を所有。設定 `SFS_LAN=1`・peer MAC で起動時 mount を公開し、HELLO は最初の操作時に行う。相手不在でも mount は消さない。
- `sfs run` の LAN 分岐から UART 初期化依存・LOG/EXIT・コマンド終了時 BYE/unmount・shell owner による寿命を外す。v66 の begin/end は LAN 常駐時「scope開始/終了」にし、serial モードの現行動作は維持する。
- NP21/W は既定 HostDrv を優先。SFS 試験時は起動設定で /host を SerialFS に選ぶ。使用中 HostDrv を暗黙に上書きしない。
```c
struct SfsPersistent { SfsClient client; u32 generation, last_use, alive_seen; };
int sfs_ensure_session(u32 deadline); /* sid無し/30秒idle/新ALIVE/seq>0xF000でHELLO、downフラグ無し */
int sfs_request_deadline(u8 op, const void *in, u16 n, void *out, u32 deadline);
void sfs_alive_observe(u32 host_generation); /* 非消費の内部観測、再接続予約だけ */
```
- LAN の1操作に入口から **1000ms の絶対期限**（HELLO・TX待ち・再送込み）。同一 sid/seq の再送のみ 100ms 間隔、期限を伸ばさない。応答無しは HOSTDOWN、UART は従来時間式のまま。
- 期限切れの結果はその要求に記録し、次のアプリ要求は必ず通信を試す。連続失敗数で LAN client を DEAD に固定しない。切れた sid は破棄してよい（通信状態であり down 状態ではない）。
- 不明 sid 応答／新しいホスト起動世代の ALIVE は mount generation を進め、以前の FD の read/write/fstat/seek は STALE、close は解放できる。次の open は新しい HELLO で開始する。
- seqが **0xF000を超えた次要求の前**に同じ1000ms期限内でBYE→HELLO。netcdはBOPEN・同一性を再確認。同じ像は媒体交換にしない。SFSファイルはパス＋offsetで要求し遠隔handleを持たない。再取得が必要な遠隔handleはnetcdのBOPENだけ（旧handleを新sidへ送らない）。
- 通常の30秒 idle や同じ起動世代の定期 ALIVE による HELLO 更新だけでは既存 FD を失効させない。sid と FD 世代を分ける。ALIVE を失っても次の要求の sid 不一致で検出する。
- 期限切れした write は「適用されたか不明」。自動的に新 sid で再実行しない。アプリが確認・再要求を判断する。同一 sid/seq の重複は現行 LanServer の応答キャッシュで二重実行しない。
- `--allow-write` を lanfs_host に追加し既存 HostFS の書込み設定へ渡す。既定 read-only、拒否値は既存 SFS→VFS の ROFS 等へ写像。OS32 側の許可設定は作らない。
## 4. 仮想ブロックデバイスとネットワーク越しの CD
- `--image install=/path/os32.iso` をホストで設定。OS32 は alias `install` を指定。ホストの実機器は同じ image インターフェースを提供するラッパーの仕事。
- SFS の既存 sid/seq/CRC・LE明示直列化を再利用。新 op（0x30〜0x32）は実装時に衝突検査する。NOTIFY は SFS に足さない。
```text
BOPEN 0x30: alias UTF-8（残り全体、NUL無し、1..63B）
 response: status i32, handle u32, size_lo u32, size_hi u32, mtime_ns_lo u32, mtime_ns_hi u32
BREAD 0x31: handle u32, lba512 u32, count u16（1..2）
 response: status i32, data[count*512]（成功時のみ、長さ一致必須）
BCLOSE 0x32: handle u32 → status i32
```
- 全応答は `op|0x80`。handle は sid 内だけ有効。BOPEN は read-only FD を保持、BREAD はその FD を pread。範囲・乗算オーバーフローを検査、EOF 短読は IO。変更前後の fstat が違えば STALE。
- 現行 LAN 最大 payload=1028B (`sfs_proto.h:43`) に2×512B+statusが収まる。Device は sector=512、ISO の2048B読みを2往復に割る。単一SFS clientで /host と netcd の sid 更新を共有し、handle は張り直し後 BOPEN で再取得。
- 像の同一性は **size u64 + mtime_ns u64**。先頭ハッシュは採らない（ISOの先頭ゼロ域では交換検出が弱い）。同一size/mtimeに意図的に戻した変更は検出対象外。整数の上下32bitで運べる。
- size は512B倍数、初版の上限は u32 セクタで表せる範囲。Device total_sects に正確に設定。CDは読み取り専用、write は ROFS。
```c
struct NetCd { u32 handle, media_gen, last_use, alive_seen; u32 size[2], mtime[2]; };
int netcd_read(Device *dev, u32 lba, int count, void *buf); /* 0/HOSTDOWN/STALE/IO */
int dev_media_check(Device *dev, u32 *generation); /* ATAPI/netcdを選び、cache hit前にも確認 */
```
- 30秒未使用後・seq閾値による再接続後・新着 ALIVE 後の最初のアクセスで HELLO+BOPEN+同一性比較を行う。カーネル公開のALIVE観測世代とalive_seenの差を検査予約としてcoalesceし、ネットワーク待ちはtick内で行わない。検査成功時に取得済みの観測世代をalive_seenへ保存し、その間の新着は次アクセスへ残す。
- 同じ像なら媒体世代維持。違えば media_gen++、VFSの当該mount/FDを失効、ISOのPVD/パス/先読み/sector cache全破棄。その操作は STALE。次のパス操作で同じ `/cd0` を載せ直し、旧FDは永久にSTALE。
- ISO の全 `atapi_media_gen()` 使用をデバイス毎の検査に置換。キャッシュ参照前と読み完了後に世代照合し、HOSTDOWN/STALEを IO に潰さない。媒体不在で古いcacheを成功として返さない。
- `init/kernel_boot_memory.inc:51,60–66` は現在CD→PCI→LAN。NETCD有効時はPOST_ROOTでPCI資源確立→lan.modロード/NIC bind→netcdをcd0登録→cd.modロード→子mountに変更（PCI/LAN段は二重実行しない）。登録時にホスト応答を待たない。
- `drivers/ata/cd_body.c:14` の固定cd0を最初の空きcdNへ変更、`drivers/ata/cd_module.c` の登録契約も更新。cdinstの固定箇所は `userland/system/cdinst.c:33,37,115`。
- 起動設定 `NETCD=install` で仮想デバイスを **cd0として先に登録**、物理CDは次の空き番号へ。未設定時は現状通り。VFSの `VFS_DEV_CD` と `iso9660_mount` の cdN 命名をそのまま満たす。
- FD手順: ホストで新規 `tools/fd_netcd_config.py <入力FD像> --output <出力FD像> --netcd install --peer <MAC>` を実行し、起動前にFDの起動設定へ `NETCD=install`・peer・`SFS_LAN=1` を書く → lanfs_host(--image、必要なら--allow-write)とbridge/agent起動 → 出力FDで起動 → 上記POST_ROOT順でnetcd=cd0を確保 → `mount /cd0 cd0 iso9660` → 従来のcdinst。起動後の付け直しの口は作らない。cdinst自身のmountが既存mountを許すことも受入で確認。
- netcd無指定の実ATAPI CD、HostDrv、シリアル sfs run、通常HDD起動は回帰対象。CD導入は任意選択で既存cd0を横取りしない。

## 5. メールボックス
```text
link op=11 NOTIFY、20B既存ヘッダ、rid=0、seq/ackは要求から独立。
payload: host_generation u32, service u16, kind u16, length u16, reserved u16=0, data[0..236]
番号はLISTが世代内で割当（1..65534）、0は未割当、65535は検索時の全種指定専用。
ALIVE: service=LIST上のos32.alive、kind=1、data={host_generation u32}。
HELLO応答の旧payload末尾へhost_generation u32 LEとalive番号 u16 LEを追加（SYNACK 6→12B、ESTAB 2→8B）。
SYN/CONFIRMは0Bのまま。新旧の応答長だけ受理し、旧長では拡張フィールドを読まずLIST取得前の内部ALIVE観測を無効とする。
B1で長さ/offset、A9で長さ検査・長さを渡すdecode、H5でhost_agentのHELLO生成/検査を更新。
ACK/WINDOWはrid=0。重複seqはACKのみ。古いsession/世代のフレームは破棄。
```
- 起動時ヒープから64×256B=16KiB確保（失敗時は通知NOSYS、通信本体は継続）。最新ALIVE1枠・管理情報は別。初期化済み配列を像に入れない。
- 通知WINDOWは4通（最大248B/通）、累積ACK・Go-Back-N・100ms再送。要求8handleと本文ringを消費しない。ホスト未ACK上限64通、満杯では最古未送信を捨て計数し、既送信seqに穴を作らない。
- 受入時ACK、満杯なら最古を捨てdropped++。同世代ALIVEは内部観測と最新1枠の上書きだけ、初回/世代変更時のみリングへ1通。
```c
/* 公開C89、sizeof=256。wireとのmemcpyはしない。 */
typedef struct HostNotice {
 u32 serial, dropped, host_generation;
 u16 service, kind, length, reserved; u8 data[236];
} HostNotice;
int host_notice_read(u32 generation, u32 service, HostNotice *out);
u32 host_notice_serial(void); /* GUI向け非消費の投入通番 */
/* IF保存/cli→最大64件探索・kernel一時領域へcopy→IF復元→copyout
 * →IF保存/cli→同じserial/世代が残っている時だけ削除→IF復元。
 * copyout失敗は不消費。IRQ0が押し出して置いた新serialは削除しない。 */
```
- readは0/AGAIN(-14)/INVAL(-9)/NOSYS(-10)、古い指定世代はSTALE(-11)。service=65535は全種、generation=0は全種取得時のみ現世代。世代変更時は旧通知を捨てdroppedに加える。
- キューは共有の競合消費。SDKが名前から番号を引き、他サービスは保持。購読・複製は初版に入れない。空ならAGAINであってHOSTDOWNではない。
- 内部ALIVEはカーネルでhost_generationとALIVE観測世代（受理ごとに進む通番）を非消費で公開。`/host` はホスト世代変更で失効し、netcdは次アクセス時に観測世代をalive_seenと比較する。IRQ0からlan.modへの呼び戻しは作らず、公開値の一貫したsnapshot取得口を輸出する。同世代の定期ALIVEでFDを壊さない。
- os32.aliveのホスト世代が変わった復帰時は、別設計のos32.timeへ最初の時刻合わせを渡す（IRQ0では実行しない）。観測通番だけの変化と区別し、具体的な時計/RTC処理と実行順の結線はos32.timeの設計で定める。
- GUIはgshellがhost_notice_serialを非消費で確認し各ownerにpendingを立て、HOST_NOTICEを1件配送。消費後clear、イベントring満杯時はpending保持。アプリはSDKでAGAINまで取得。他アプリが先に取って空でも正常。
- host_agentのnotify(name,kind,data)はLIST登録簿で番号化。起動/HELLO時と5秒毎にALIVE。起動世代は再起動で新しくし、番号再利用と区別する。
- 永続sfs_generationとagent IPCは削除。不明sidのSTALE (`tools/serialfs_host.py:697–699`) でSFS単独再起動を検出しagent不在でも成立させる。
- agent停止中は1秒毎のHELLO再同期だけを行い、復帰時は最新ALIVEを送る。通常サービス要求を勝手に再発行しない。
## 6. Host Services の実機化
- L-C: 82557存在時優先、なければLGY。選択は起動設定で上書き可。`lan_module_session_acquire/release` を sfs run の寿命からNIC共用参照へ移し、link利用中もlan.modを保持。ドライバのIRQ/DMA初期化は変更しない。
- L-D2: CLIP=auto は Wayland環境でwl-copy/wl-paste、X11でxclip、既存Windows/WSL経路も維持。コマンド無し/表示server無しは503。UTF-8、PUTの4KB制限は現行通り。
- PRINT は非同期 `lp` stdin、ジョブIDは既存Agent管理、STATUSでqueued/done/error。rawは `lp -o raw`、textはUTF-8をCUPSへ。CUPS未導入は503。shell展開なしのargvで実行する。
- TIME/GET(wget)は既存 `_service_now` (`host_agent.py:784`) を再利用。HTTP/TLS/DNSはホストのみ。既存のGETファイル設定以外に許可リストを追加しない。
- link要求も **相手からの応答待ちが1000msを超えればHOSTDOWN**。長いHTTP/印刷はSTATUSへのPROCESSING応答が生存応答となる。本文転送中も進捗/STATUS応答で期限更新するが、無応答時は延長しない。
- v51は待たない。非UPのopenは即HOSTDOWN、HELLOは背景再同期。確保済み要求は未完ならAGAIN、1000ms期限切れならHOSTDOWN。closeはローカル解放可能。
| 場合 | 旧v51 | 新v51 |
|---|---|---|
| open時リンク非UP (`link_host_api.inc:92,96,117`) | STALE | HOSTDOWN |
| 応答なし/要求期限切れ | 再送/PROBE後に再同期・STALE | HOSTDOWN |
| handleの旧session世代 | STALE | STALE（この用途に限定） |
| 有効handleの未完 | AGAIN | AGAIN |
- 無応答1000msの期限切れはそのhandleだけHOSTDOWNとして再送を止め、他の7本・sessionは維持。session全体の張り直しは既存RTO=200ms×5・PROBE=1s×5の条件に達した時だけで、要求期限を理由にPROBEを前倒ししない。全体の通信障害による既存の再同期は別に進む。
- L-E: LGYでL0〜L3とSFS同時動作 → 82557で同じ試験 → TIME/CLIP/PRINT/wget。HTTPが1秒以上かかる試験でもPROCESSINGが続けば成功すること。
## 7. KAPI の追加と誤りコード
| 項目 | 契約 |
|---|---|
| HOSTDOWN | **-18案**。`KAPI_SPEC.md:136–149` で未予約。実装時に最新shared header/予約表を再確認。番号追加だけなら版上げ不要。 |
| TIMEOUT | host_call全体期限用。実装時にKAPI_SPECの誤り番号予約表でHOSTDOWN等と重複しない番号を確保しshared/SDKで共有。番号追加だけでは版上げ不要。 |
| 新API | `host_notice_read(u32 generation, u32 service, HostNotice *out)` と `host_notice_serial(void)` の2本。v80予約(slot255〜257)の後、**v81/slot258〜259案**。着手時の末尾に調停し固定値先取りしない。 |
| エラー | HOSTDOWN=リンク非UP/要求の無応答、STALE=-11=既存資源失効、AGAIN=-14=未完/空、ROFS=-15=ホスト拒否、IO=-1=壊れた応答/媒体I/O。 |
| GUI | 新出来事番号は既存イベント型の空きに末尾追加、構造体は拡張しない。KAPI版と同時公開、未対応アプリは無視。 |
- os32-kapi-add / KAPI_SPEC §3-1: kapi.json末尾・includes/externs/out指定 → JSONとsharedの版を同期 → `python3 sdk/gen_kapi.py` / `sdk/kapi_rust_gen.py` → 生成差分と旧slot不変を確認。
- KAPI実体はユーザポインタ検査・kernel一時HostNoticeへ取り出し・copyout。CPL3で壊れたポインタがキューを消費しない契約を試験する。
- 公開SDKはC89、内部GNU11、kstring、定数はconfig/公開契約/実装の3層。Device/lan/cd部品の境界変更は輸出入表・部品契約世代も同期し旧組合せをロードさせない。
- 仕様表と新API利用者のapp.conf要求版更新。[ABI]のclean→allはPM統合段で必須。外部apps/gameはH-7再開決定まで構築しない。

## アプリの口 (KAPI・SDK・LIST・名前)
- KAPIのopen/status/read/write/closeは任意の要求行を扱う汎用APIのまま。NIC・link/SFS・将来の統合経路をアプリから隠す。
```c
int host_call(const char *req, const void *in, u32 in_len,
              void *out, u32 cap, u32 timeout); /* SDK、成功はbyte数、失敗は負値 */
int os32_host_notice_read(const char *name, HostNotice *out); /* LISTで名前解決 */
```
- host_callはopen→送受信/statusをyieldしながら待つ→close。timeoutは呼出し全体の期限(ms)、0は非待機。非UP/無応答1000msはHOSTDOWN、全体期限はKAPI_SPECの予約表で確保したTIMEOUTとして区別（SDK独自の番号は定義しない）。出力不足はNOSPCで切詰め成功にしない。
- `LIST` はUTF-8行列: 先頭 `generation <u32>`、後続 `<name> <version> <id:u16> <origin:builtin|plugin|sfs|stub>`。本文分割で運びSDKが世代ごとcache、STALE/新ALIVEで破棄・再取得。番号の固定表を持たない。
- 要求行は `<service> <verb> [args]`。名前は小文字英数字・点、`os32.`だけOS標準予約。KAPI_SPEC/SDK説明に **「os32. は OS32 標準、ske-studio が定めた」** と明記。他の名前は自由、作者接頭辞は強制しない。
- 標準名は次節の一覧に従う。LISTのoriginは提供元の分類で、接頭辞から推定しない。プラグインのos32.名乗りは拒否せず警告1行。重複名は後勝ちで警告し、LISTは有効な1件を返す。
- 旧GET/TIME/CLIP/PRINTをos32.http/time/clip/printへ対応付け、移行中は旧行も受理。標準SDK関数はos32_host_*、libos32host/os32api host.rsの用途別APIはhost_call上へ。自作はhost_callで任意名を呼べる。
- `net/link.h:93` のLINK_HANDLESを8へ。要求8枠、本文ringは各8KiBをヒープ確保（計64KiB、既存1本から+56KiB）。ridごとの空きとNIC共通余力を分配し、合計を超すcreditを広告しない。NOTIFYは独立予約、1本満杯で他の7本を止めない。
- agentのLISTはSFS側のos32.file/os32.iso9660をorigin=sfsの静的行として登録（世代内idも割当）。IPCや稼働確認は足さず、掲載は接続成功を保証しない。ファイル/媒体の要求本体は既存VFS/SFS経路を使い、LISTへの掲載でagentへ転送する仕組みは追加しない。
- os32.udf/mail/feed/chatはorigin=stubで掲載し、ホストへの要求は501。別設計の未提供名は予約のみで、今回のLISTに実装済みとして載せない。
- Tridentの改訂は別の設計往復。
## サービスの一覧 (標準・スタブ・別の設計へ)
| 区分 | 名前 | この設計との結び目／別設計の順番 |
|---|---|---|
| 標準 | os32.alive / os32.clip / os32.print / os32.http | ALIVE通知／CLIP／PRINT／旧GET、origin=builtin。 |
| 標準 | os32.file / os32.iso9660 | lanfs_hostのファイル／仮想CD、origin=sfs。CDの旧候補名os32.cdは使わない。 |
| 標準名予約 | os32.deploy | 開発運用はこの名前だけ。提供済みの機能だけLISTへ載せ、具体化は別途。 |
| スタブ | os32.udf / os32.mail / os32.feed / os32.chat | DVD形式／メール／フィード／チャットの名前のみ確保、origin=stub、ホストは501。 |
| 別の設計へ・1 | os32.time | ALIVEのホスト世代変化 → 最初に時刻合わせ。既存TIMEの互換は維持。 |
| 別の設計へ・2 | os32.conv.image | 汎用host_call・LISTへの結線。 |
| 別の設計へ・3 | os32.audio / os32.midi | 汎用host_call・LISTへの結線。 |
| 別の設計へ・4 | os32.screen | 汎用host_call・LISTへの結線（優先度低め）。 |
- os32.conv.textは追加しない。os32.ime・os32.build・os32.exec・os32.aiは予約も提供もしない。別設計の中身はここでは定めない。
## 8. 部品の大きさの見積もり
基準はREVIEW.r1の既存成果物（この木にはbuild/outがなく未再測定）、KBはKiB。増分は同設定の実装前後を比較する。
| 像/部品 | 基準サイズ | 増分の算式/対象 | RAM増分 |
|---|---:|---|---:|
| vmkernel.lz4 | 統合時採取 | 新lz4−旧lz4（共通NIC/RX・LGY adapter(LGY98時)・通知/KAPI/VFS・登録口） | 通知16KiB+最新枠、本文+56KiB、RX最大12KiB+管理 |
| lan.mod | 16,284B | 新mod−16,284B（82557 adapter/block/netcd・輸入表） | 媒体/handle管理を実測 |
| cd.mod | 12,128B | 新mod−12,128B（空きcdN/媒体境界） | 管理情報を実測 |
| SDK/gshell | 旧成果物を統合時採取 | FD収録分だけ新旧FAT割当差 | LIST cache/pending |
- r1の15〜28KBは根拠不足につき撤回。FD増分=Σ(新FAT割当−旧FAT割当)、空き=87KiB−増分。実測23KiB超過時だけ除外をユーザーへ尋ねる。新コードのKBは未測定。
- lan.mod/cd.modは既存core/MINIMAL/FD収録。通知16KiB/本文ringはヒープでFDを増やさない。kdebug.binには新依存を置かない。
- `build/packages.yaml:116–121` で kdebug.bin/hwdev.mod/geforce.mod は既にFD除外済み。これらを追加節約として数えない。kdebug機能・MINIMAL集合を削る案ではない。
- 超過時の候補（ユーザー決裁後だけ適用、実サイズで不足分を満たす順）:
```yaml
# fd.exclude に追加する候補。CDのMINIMALには残す。
- guest: /boot/fm.mod
  reason: 起動FDの復旧・ネットワークCD導入にFM音源は不要
- guest: /boot/pcm.mod
  reason: 起動FDの復旧・ネットワークCD導入にPCM再生は不要
```
- fm.mod=9,704B（9.477KiB）、pcm.mod=9,868B（9.637KiB）、計19.113KiBが基準。FAT回収量は各ceil(size/cluster)×cluster（1KiB clusterなら各10KiB）。FM→PCMの順を推奨し実cluster/新成果物で確定。2HD/1.44MB双方でpackages検査、FD→install→HDD集合も確認。cd.mod/lan.mod/v86.mod/sqlite.modは導入経路の依存を確認せず除外しない。
## 9. 1 本に寄せる道筋（今回は実装しない）
1. NIC共用・通知・期限・資源世代を今回の共通境界として固定し、既存SFS/Host Services双方の受入を保つ。
2. 次期にSFSのファイル/ブロック要求をlinkのREQUEST/WDATA/DATA上へ包むadapterを作る。FS/Device側のAPIは変えない。
3. ホストのHostFS/image backendを単一Agentに載せ、機能交渉で新旧を並行試験。再送時のwrite重複排除と媒体世代を同等にする。
4. 全利用者・実機・FD導入の合格後にraw SFS LAN経路とlanfs_host前段を廃止。シリアル救済経路の廃止は別決定。今は2本を保つ。

## 10. ファイルごとの変更一覧と段
各行=1ファイル=1実装者の段（半日〜1日以下）。契約を先に固定し、同じ既存ファイルの段は直列。新しい機能本体は500行以下を目標に分離。ヘッダだけの段は契約試験で受入可能にする。
| 段 | ファイル（新=新規、改=変更、像はビルド指定で決定） | 行数目安 | 所有する機能／依存 | 像 | lan_imports.def の輸入 |
|---|---|---:|---|---|---|
| A1 | 新 net/nic.h | 70 | NIC契約、最初に固定 | 共有ヘッダ | 不要（境界追加はIで宣言） |
| A2 | 新 net/nic.c | 220 | NIC選択・寿命、A1 | vmkernel | 提供: bind/解除/tx |
| A3 | 新 net/nic_rx.c | 220 | 単一RX・振分、A1と並列adapter後結線 | vmkernel | 提供: net_rx_take |
| A4 | 新 net/nic_lgy98.c | 100 | LGY adapter、A1、lgy98.cと同じく常にビルドし実行時にLGYが見つかったときだけbind (R2) | vmkernel | 不要: 同像ne2k |
| A5 | 新 net/nic_e82557.c | 100 | 82557 adapter、A1、A4と並列 | lan.mod | 不要: 同像82557 |
| A6 | 改 drivers/net/ne2000/lgy98.c | 30 | link初期化/tick所有移管、A2 | vmkernel | 不要（境界追加はIで宣言） |
| A7 | 改 drivers/net/e82557/lan_module.c | 50 | NIC利用中のmodule参照、A2 | vmkernel | 不要（境界追加はIで宣言） |
| A8 | 改 net/link.c | 90 | ne2k直結除去・要求期限hook、A3 | vmkernel | 不要（境界追加はIで宣言） |
| A9 | 改 net/link_rx.inc | 35 | 共用RX取得・NOTIFY分岐・HELLO payload拡張/新旧長受理、B1/B2後 | vmkernel | 不要（境界追加はIで宣言） |
| A10 | 改 fs/serialfs/sfs_lan.c | 60 | 私有drain廃止、A3 | lan.mod | 要: net_rx_take/tx |
| B1 | 改 net/link.h | 35 | op11/rid0・HELLO payload拡張/新旧長契約、独立先行可 | 共有ヘッダ | 不要（境界追加はIで宣言） |
| B2 | 新 net/link_notify.c | 260 | NOTIFY順序・ACK/WINDOW、B1 | vmkernel | 不要（境界追加はIで宣言） |
| B3 | 新 net/host_mailbox.c | 180 | 64枠・内部ALIVE世代snapshot公開・投入通番、B1、B2と並列 | vmkernel | 不要（境界追加はIで宣言） |
| B4 | 新 kapi/kapi_host_notice.c | 70 | 検証/copyout、B3 | vmkernel | 不要（境界追加はIで宣言） |
| B5 | 改 userland/gshell/src/wm.rs | 40 | 非消費通番からHOST_NOTICE配送、B3/B4 | gshell | 不要（境界追加はIで宣言） |
| C1 | 新 fs/serialfs/serialfs_persistent.c | 260 | 常駐mount・世代、A10/B3 | vmkernel | 提供: ensure/共有client |
| C2 | 改 fs/serialfs/serialfs_session.c | 70 | v66 LAN scope化、C1 | vmkernel | 不要（境界追加はIで宣言） |
| C3 | 改 fs/serialfs/sfs_client.c | 90 | LAN deadline・DEAD分離、C1契約後 | vmkernel | 提供: deadline要求口 |
| C4 | 改 fs/serialfs/serialfs.c | 70 | ensure/誤り伝播、C1 | vmkernel | 不要（境界追加はIで宣言） |
| C5 | 改 fs/serialfs/serialfs.h | 25 | 常駐契約、C1前に固定 | 共有ヘッダ | 不要（境界追加はIで宣言） |
| D1 | 改 fs/serialfs/sfs_proto.h | 40 | BOPEN/BREAD/BCLOSE契約、独立 | 共有ヘッダ | 不要（境界追加はIで宣言） |
| D2 | 新 fs/serialfs/sfs_block.c | 200 | SFS媒体RPC、D1/C3 | lan.mod | 要: kernel SFS client要求口 |
| D3 | 新 drivers/block/netcd.c | 280 | Device・同一性・世代、D2/B3、次アクセスでalive_seen照合 | lan.mod | 要: Device登録/媒体世代・ensure・ALIVE snapshot |
| D4 | 新 fs/isofs/iso9660_media.c | 160 | media一般化・cache世代境界、D3契約後 | vmkernel | 不要（境界追加はIで宣言） |
| D5 | 改 fs/isofs/iso9660.c | 90 | media呼出し/負値維持、D4 | vmkernel | 不要（境界追加はIで宣言） |
| D6 | 改 fs/vfs_fd.c | 90 | mount失効/再mount、C1/D4 | vmkernel | 不要（境界追加はIで宣言） |
| H1 | 改 tools/lanfs_host.py | 70 | allow-write/image引数・dispatch、D1 | ホスト | 不要（境界追加はIで宣言） |
| H2 | 新 tools/host_image.py | 220 | image handle/pread/identity、D1、H1と並列 | ホスト | 不要（境界追加はIで宣言） |
| H3 | 新 tools/host_notify.py | 240 | NOTIFY送信・host世代（SFS IPCなし）、B1 | ホスト | 不要（境界追加はIで宣言） |
| H4 | 新 tools/host_linux.py | 180 | Ubuntu CLIP/PRINT、独立 | ホスト | 不要（境界追加はIで宣言） |
| H5 | 改 tools/host_agent.py | 65 | H3/H4接続・PROCESSING・HELLO payload拡張/新旧長受理、B1/H3/H4後 | ホスト | 不要（境界追加はIで宣言） |
| H6 | 改 tools/lan_bridge.py | 20 | SFS除外、A3契約後 | ホスト | 不要（境界追加はIで宣言） |
| I4 | 新 tools/fd_netcd_config.py | 180 | 起動前FD像のFDにだけ置く /etc/netcd.cfg へNETCD/peer/SFS_LAN設定 (R1)、I1契約後、2HD/1.44MB読戻し | ホスト | 不要 |
| I5 | 改 build/kernel.mk | 10 | A4をvmkernelへ登録する1行 (R2) | build | 不要 |
| I | 統合登録は下記のように1ファイルずつ直列段へ分解 | 各10〜80 | sdk/kapi.json、shared header、イベント定義、Device宣言/登録、init起動設定、module輸出入定義、build登録、app.conf、仕様正典 | 各対象 | 不要（境界追加はIで宣言） |
- Iの生成物は手編集しない。LAN_OBJSへA5/D2/D3を追加、kernel通常パスの自動収集から除外。sfs_clientは現状 `build/kernel.mk:43` ではvmkernelであり、lan.modのsfs_lanと区別してD2から輸入する。A4はvmkernelに置きlgy98.cと同じLGY98条件で登録し、lan.modのne2k輸入を不要にする。netcd/82557 adapter本体をkernelへ複製しない。
- 追加段は各1ファイル: D7 `drivers/ata/cd_body.c`（cd.mod、40行、空きcdN）、D8 `drivers/ata/cd_module.c`（vmkernel、40行、登録）、I1 `init/kernel_boot_memory.inc`（vmkernel、30行、順序）、I2 `build/lan-module.mk`（build、30行）、I3 `tools/prototypes/lan_imports.def`（生成入力、80行、上表輸入）、A11 `net/link_host_api.inc`（vmkernel、60行、期限/8枠/エラー）。
- I4は入力像を保持して別の出力像へNETCD/peer/SFS_LANを記録する。
- SDK/hostの追加段も各1ファイル・半日〜1日: S1 新 `userland/lib/host/host_call.c`（SDK、180行）、S2 改 `userland/lib/host/libos32host.c`（SDK、80行）、S3 改 `userland/lib/host/libos32host.h`（SDK、40行）、S4 改 `sdk/rust/os32api/src/host.rs`（SDK、180行）、H7 新 `tools/host_services.py`（host、180行、LIST登録・sfs静的行・stub/501）。lan_imports不要、S1→S2/S4・H7→H5。
- 各試験ファイルも次節の1行を1段で新設、CHECK.inputsを宣言。Iの正典更新はKAPI_SPEC、06_filesystem、HOST_SERVICES_PLAN、LINK_PLAN、TASK_LAN_82557、packagesの各ファイルを別段とする。
## 11. 受入 → 試験 → 変異
H=host単体、N=NP21/W、R=82557実機。各行は新規 `tools/tests/test_hostsvc_<項目>.py` の半日〜1日段、ゲスト台本はtools/acceptへ別ファイルで作る。
| 項目／受入 | 試験場所・観測 | 1行変異（runtime RED必須） |
|---|---|---|
| nic | H/N/R: SFSとGET交互・同時、片方queue満杯でも他方成功、LGY L0〜L3維持、kernel-nolgy98で82557のlan.mod輸入解決、LGYだけのlinkはlan.mod不要 | MARKをbyte0だけで判定（link EOF op=5も試験に投入） |
| deadline | H/N/R: 応答遮断で入口から1000ms+1tick以内HOSTDOWN、次要求で再送信を観測、linkの1本だけ期限切れでも他の7本/sessionを維持 | deadline比較を無効化 |
| persistent | H/N/R: sfs run外でread/write、コマンド終了後もmount保持、restart後旧FD STALE・新open成功 | FD世代比較を削除 |
| write | H/N/R: allow-write有無の差、応答喪失再送で1回だけ書込み、serial既存動作維持 | duplicate応答キャッシュを迂回 |
| image | H/N/R: 2048B一致・範囲外拒否・30秒/ALIVE後交換で旧FD STALE、同じ像は維持 | size/mtime比較を常に一致 |
| isocache | H/N: キャッシュhit直前に像交換、旧PVD/dirを返さず載せ直し、ATAPI回帰 | cache前media_checkを省略 |
| mailbox | H/N: 65通で最古廃棄・drop=1、service別取得、重複投入無し、内部ALIVEをアプリ消費で失わない | 重複seqを再enqueue |
| gui | H/N: 連続64通でもownerごと出来事1件、取得後の新着で再通知、空ならAGAIN | pending clearを削除 |
| linux | H/R: TIME/CLIP往復・PRINT実出力・wget内容一致、依存無し503、遅いHTTPのPROCESSING | Linux backend選択をnoneに固定 |
| bootcd | N/R: 道具で起動前FD設定を書き読戻し、FDから従来cdinstで導入しHDD起動、ATAPI同居でnetcd=cd0/ATAPI=cd1、無指定はATAPI=cd0、空き>=64KB | 仮想CDの登録名をcd1へ変更 |
| seq | H/N/R: 100MiBを1024B単位で連続読出、0xF000越えでBYE/HELLO/BOPEN、同じISOは世代維持 | 閾値判定を除去 |
| alive | H/N: 同世代ALIVE100回で通常通知を押し出さず、世代変更時だけ1通/FD失効、netcdはsnapshot比較で再検査、HELLO新旧長受理・他長拒否 | 毎回リング投入 |
| race | H/N: copyout中IRQ投入/満杯上書きで新serialを消費しない、不正outも不消費 | serial照合を削除 |
| handles | H/N/R: 8本同時本文一致、1本満杯と通知併走、9本目資源不足、credit合計確認 | 全ridへcredit重複広告 |
| list | H/N: 再起動/番号変更で再解決、独自名/旧名/警告/出自/標準wrapper、sfs静的行・stubの501・除外名不在確認 | 新世代でcache保持 |
| sdk | H/N: yield・予約表/sharedと一致する全体期限TIMEOUT・非UP/無応答HOSTDOWN・旧handle STALE | RTO/PROBE回数を期限より優先 |
| abi | H/N: 旧slot/版/生成一致、CPL3 notice取得、不正outでキュー不消費、旧アプリ起動 | out検証前にdequeue |
- 契約A1/B1/D1 → adapter/H系は並列 → C/D結線 → I(ABI/部品世代一括整合) → N → R。NIC/FS/ISOの未結線単体合格だけでは完了にしない。
- 実装段はpreflight、対象ビルド、該当チェックMUTATE=0、触った試験--mutate、lint。統合は[ABI]clean/all、全差分のチェック、FD実容量、guest実行。試験枠は同時2本まで、物理操作/配備はPM経由。
- 今回は設計ファイルのみ作成。repoは読み取り専用指定のためpreflight --fix-fonts・build・全試験・配備は未実施。実測KB/実機速度/Opus独立レビューは次工程であり合格とは記載しない。
## 12. 未決（ユーザー判断のみ）
- **1件: FDで23KBを超えた場合の除外対象。推奨はfm.mod、足りなければpcm.modをfd.excludeへ追加。** 実測の不足KB・各候補の回収KB・起動回帰結果を添えて決裁する。今は変更しない。
- `/cd0`の採用、同一性size+mtime、世代と定期ALIVEの区別、v81/slot258の調停は既存契約と上記決定からの設計選択で、追加のユーザー決裁待ちにはしない。

## 把握している危険 (対策しない)
- 通常LANと独立したUSB LANアダプター等をホストへ接続し、PC-98とクロスケーブルで直結を推奨（README記載済み）。暗号・認証は追加しない。直結でもホスト/プラグイン由来の危険は残る。
- 最も重い: 同じLANでホストになりすまし、hsync/cdinstへ偽の像を渡すとPC-98で任意コードが動く。
- OS32になりすましてホスト公開範囲を読み書きし、os32.httpの踏み台利用・印刷ができる。
- ホストのパス処理の抜けで公開範囲を越える危険がある。
- プラグインはホスト権限で何でもでき、os32.のなりすましは警告だけ。
- 平文を盗み見でき、要求/通知を溢れさせて妨害できる。
- 運用: 普通のユーザーで実行・公開範囲を狭く・--allow-writeは必要時だけ。複数接続には通常LANとつながない専用スイッチを使う。
反論: 0件。

## 確定 (2026-11-05、Opus 第 3 回、PM が反映)
- R1 (P2): NETCD・SFS_LAN・peer は **FD にだけ置く設定ファイル /etc/netcd.cfg** に書く (/etc/system.cfg に書くと install.bin が /etc を HDD へ写し (install.c:485)、HDD 起動でも netcd が cd0 を取る)。/etc/netcd.cfg を install.c の fd_only と build/packages.yaml の fd.only の両方に足す (case 9 も合わせる)。§4 の手順・I4 はこれに従う — 追加段 I6 `build/packages.yaml`・I7 `userland/sbin/install.c` (各 1 ファイル、10〜20 行)。
- R2 (P3): A4 は lgy98.c と同じく常にビルドし (build/kernel.mk:37、LGY98 は CFLAGS の定義だけ)、使うかは実行時に LGY が見つかったかで決める。I5 は登録の 1 行。§11 nic の「kernel-nolgy98 で…」は「LGY の無い機械で 82557 の lan.mod の輸入が解決する」と読む。
- R3 (P3): agent は origin=sfs の名前 (os32.file・os32.iso9660) への要求に 501 を返す (§アプリの口 191 行)。
- 未決はユーザー 1 件のまま: FD から外す物 (実測で 23KB を超えたときだけ尋ねる、推奨 fm.mod → pcm.mod)。
