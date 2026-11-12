# PPC_SHELL_W8_DESIGN — PowerPC の到達点 S-D (書ける媒体を後から mount して保存する、段 W8) の詳細設計

> 状態: **確定 (2026-11-12)** — 既存の SerialFS の VfsOps を再利用し、PowerMac 専用の SfsIo で LAN の PLN1 に載せて `/host` を自動 mount。期限は TB 由来の 10ms の時計、lan_message は 256B のまま FS 型だけ直接写す、ホストは新しい boot に追従。共通の場所は変えない (x86 不変)。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-w8/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き確定、PM が #3・#4・#5 と R2-1〜R2-3 を反映)。前提の設計は [PPC_SHELL_DESIGN](PPC_SHELL_DESIGN.md) (段の計画 W8) と [PPC_SHELL_SC_DESIGN](PPC_SHELL_SC_DESIGN.md) (S-C)。

- 対象：PowerPC mac99 → PowerMac G4。PC-98は接続契約と本線の計画まで。
- 前提：S-CのUSER `sh.bin`、シングルタスク・協調切替・整数演算。
- 採用案：既存SerialFSのVfsOpsを再利用し、PowerMac専用SfsIoでPLN1に接続する。
- `/host` はRAM rootの後に自動mount。ホストへの接続は最初のアクセス時に行う。
- 確認基点：HEAD・origin/mainともに `041ca09f`。r1とレビューの関係箇所だけを再確認した。
- 読み取りのみ。ファイル変更・preflight・build・試験は実施していない。
- GPL・QEMUソース・`docs/archive/` は参照していない。以下のコード断片は実装予定の契約。

**1. 実物の確認**

|番号|計画と実物の食い違い・制約|実物と処置|
|---|---|---|
|D1|N1のhost_*はFSの口ではない|HOST_SERVICES_PLAN.md:64、net/link_host_api.inc:157。W8はVFS→SerialFSを使う|
|D2|HostDrvFSは一般的なHost Services FSではない|fs/hostdrv/hostdrvfs.c:81はNP21/W専用I/O。PowerPCには組まない|
|D3|ops登録・mountだけでは動かない|serialfs.c:118のSERIAL型・permitと、persistent所有者・SfsIoが必要|
|D4|PowerPC端末はゲストのUDP protocolではない|ppc_lan_host.py:70のUDPはEthernet搬送、lan_service.c:105はEtherType判定|
|D5|現行PLN1は型8まで・payload256B|lan_boot.h:94、lan_wire.c:82。FS最大1044Bを通常messageへコピーしない|
|D6|既存mnt群を丸ごと登録できない|PPC profileにmntなし。cmd_mnt.cは3引数必須・serialfs拒否・他コマンドも登録|
|D7|旧§8のram0/cd0は現行名ではない|PPC_SHELL_DESIGN.md:369、root_boot.c:7のrd0/rd1を維持|
|D8|素のServerだけではLANの遅延複製を防げない|tools/lanfs_host.py:31,58–106のLanServerとfs/serialfs/sfs_lan.c:32–146を手本にする|

- W8の方向は `docs/decisions/ppc/PPC_SHELL_DESIGN.md:50` と一致する。
- SerialFSのFS本体・既存VfsOps・resource generationを再利用し、輸送と接続先観測を差し替える。
- `sfs_client.h:24` のSfsIoはput/get/now/idle/can_wait/put_untilを持つ。
- `serialfs_persistent.c:72` はpermit→vfs_mountを実装済み。開始時にHELLOを待たない。
- PC-98のLAN常駐 `/host` とhsyncの受入は `TASK_NET_HOSTSVC.md:3` にある。
- N1の100Hz IRQ駆動状態機械は移植せず、PowerPCのGMAC pollを使う。
- KAPIのsfs_begin/end・host_*不在指定は維持し、既存FSとplatformの間で閉じる。

**2. S-D の到達点と受入の形**

|対象|S-Dの契約|
|---|---|
|root|rd0の読み取り専用RAM ISO。起動後に元の媒体を読まない|
|作業用|rd1の小さな `/tmp`。再起動で消える|
|保存先|ホストで指定したディレクトリを `/host` として公開|
|mount・接続|root初期化後・sh前に自動mount。ホスト不在でもsh起動まで完了|
|成功の意味|WRITE応答成功までにホストOSへの書込み・closeが完了|
|永続性|ゲスト・ホストプロセスの再起動を越える。ホスト電源断耐性は約束しない|
|操作・制限|既存SFSの作成・上書き・読取り・列挙・ディレクトリ操作、パス・サイズ・期限を維持|

```text
ホスト：ppc_lan_host.py の既存起動指定に
        --root <保存用dir> --allow-write <許可する相対パス> を追加
ゲスト：起動 → RAM root → /tmp → /host 自動mount → USER sh
sh> echo x > /host/f
sh> cat /host/f
x
ホストは動かしたまま、ゲストだけを再起動
sh> cat /host/f
x
sh> ls /tmp
前の起動で作った作業ファイルは存在しない
```

- `x\n` をホスト実体とも照合する。RAM ISOにhost項目がなく `ls /` に出なくてもよく、`ls /host` は使える。
- 書込み中も端末・診断・ログを進め、ホスト停止時は有限時間でエラーを返して `/tmp` 操作を続けられる。
- ホスト再開後は新しいアクセスで接続を回復する。失敗したWRITEを新sidへ自動再実行しない。
- 複数RPCに分かれるファイル全体の原子的置換は追加しない。

**3. 境目と接続契約**

|層|再利用する物|W8で足す物|
|---|---|---|
|USER・VFS|S-Cのfile/dir・redirect・既存KAPI、mount・FD・世代|なし|
|FS|serialfs.c・persistent・client・proto|なし|
|platform・arch|GMAC・PLN1・poll・trap dispatch|SFS輸送・観測・起動結線、割り込み文脈照会|
|ホスト|HostFS/Server、LanServerの複製・HELLO処理|PLN1 adapter・boot追従|

```c
/* root_boot.c：既存表を一括登録する。 */
static const struct vfs_device_name names[] = {
    {"rd",   VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM,    0, 2},
    {"COM1", VFS_NAME_EXACT,        VFS_DEV_SERIAL, 0, 1},
};
vfs_device_names_register(names, sizeof(names) / sizeof(names[0]));
```

- COM1は既存SerialFSの論理名。UART装置、新device type、block device、KAPI slotは追加しない。
- 既存lexical registryとVFSの型・番号encodeを使い、後から別表を登録してrd0/rd1を失わせない。

```c
/* hostfs_boot.c：新規。 */
int pm_hostfs_boot(void)
{
    struct SfsPersistentConfig cfg = {
        .io = &pm_hostfs_io, .io_ctx = &pm_hostfs_transport,
        .lan_enabled = 1
    };
    int rc = serialfs_init();
    if (rc) return rc;
    return sfs_persistent_start(&cfg);
}
/* 既存所有者がserialfs_permit(client)の後、
 * vfs_mount("/host", "COM1", "serialfs")を実行する。 */
```

- VfsOpsを複製せず、read/write・stream・resource_check/refreshを既存のまま使う。
- hostfs初期化失敗はログへ出し、root・tmpが正常ならshを起動する。通信未接続は初期化失敗ではない。
- cmd_mnt.cは組まない。将来の手動mountもplatform所有者経由とし、USERにpermitを直接操作させない。

**PLN1 の拡張と格納域**

|項目|決める形|
|---|---|
|輸送|同じGMAC・PLN1 EtherType 0x88b6、QEMUでは既存UDP搬送路|
|追加型|9=FS_OPEN、10=FS_REQ、11=FS_RSP。既存1〜8の意味・上限256Bを維持|
|FS_OPEN|ホスト→ゲスト、payloadなし、FS専用の外側nonceを通知|
|FS_REQ/RSP|完全なSFSフレーム1個。12B header＋1028B payload＋4B CRC＝最大1044B|
|外側サイズ|Ethernet 14＋PLN1 80＋SFS 1044＝1138B、分割不要|
|整数|外側は既存BE、内側は既存LE。構造体の直接転送は禁止|
|予約値|FS型のseq/request_id/opcode/status/flags/drop counters等は0|
|nonce|FS_REQは0、ホストは検査しない。FS_OPEN/RSPはホストFS専用16B nonce|

- PLN1で包むのは、QEMUのUDP搬送上でもGMACがEtherType 0x88b6を直接判定する形を保つため。
- 内側は既存のENQ・SF・op・sid・seq・len・body・CRC。応答op、status、READ/WRITE/HELLOの配置も維持。
- `PLN1_PAYLOAD_MAX=256` と `lan_message.payload[256]` を維持し、FS専用最大値1044を別定数にする。
- ヘッダdecodeは型・長さ・予約値・nonce規則を検証し、payloadをコピーしない。以下は追加APIの形。

```c
/* lan_boot.h：lan_headerはpayloadを含まない外側フィールド。 */
int pm_lan_decode_header(const u8 *src, u32 n, struct lan_header *h);
int pm_lan_encode_payload(u8 *dst, u32 cap, const struct lan_header *h,
                          const u8 *payload, u32 len, u32 *used);
/* 既存pm_lan_decode/encodeは型1〜8・256B以下に限定したwrapperを保つ。 */
/* lan_service.c：s.rxは既存1514Bのまま。拡大はs.txだけ。 */
u8 tx[14 + PLN1_HEADER_BYTES + PLN1_FS_FRAME_MAX], rx[1514];
/* receive()：Ethernetの宛先・送信元・EtherType検査の後。 */
struct lan_header h;
if (pm_lan_decode_header(s.rx + 14, n - 14, &h)) return GEM_OK;
if (!same(h.boot_id, s.boot->boot_id, 16)) return GEM_OK;
if (h.type >= PLN1_FS_OPEN) {
    pm_hfs_receive(&h, s.rx + 6, s.rx + 14 + PLN1_HEADER_BYTES);
    return GEM_OK; /* FS_REQはゲスト側では拒否。 */
}
/* ここから従来のpm_lan_decode(..., &req)と端末・診断の分岐。 */
/* FS送信口：RUNNING確認、Ethernet header設定の後。 */
r = pm_lan_encode_payload(s.tx + 14, sizeof(s.tx) - 14,
                          &h, frame, frame_bytes, &used);
if (r) return r;
return gem_tx(&s.g, s.tx, used + 14);
```

- FS受信は許可peer・boot・型別長・予約値を確認し、内側の完全長・CRC検証後、hostfsの受信枠へ直接コピーする。
- s.rxへのポインタを保持しない。未消費の受信枠は上書きせず、短いdatagramの末尾を次へ連結しない。
- s.txは既存経路と共用する作業領域。再試行のたびに再encodeし、poll越しに中身の保持を仮定しない。
- req/rsp・logs・term_state・beacon・s.response・lan_monitorを大型化せず、端末分割幅・ringも維持する。
- 外部payload版はlenとヘッダ長の一致、型別上限、出力容量をコピー前に確認する。Python codecも同じ規則。
- Ethernet paddingをSFS本文へ渡さず、PLN1宣言長の範囲で完全なSFSフレーム1個だけを検証する。

**SfsIo と待機**

- USER scは `kernel_trap.c:90–101` でEE=0のまま実行されるため、tick_count・ppc_irq_enabledを待機判定に使わない。
- TB読取りは `terminal_input.c:58–73` の上位・下位・上位の再読取りを手本にする。

```c
static u64 pm_hfs_tb(void)
{
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((u64)hi << 32) | lo;
}
static u32 pm_hfs_now(void *ctx)
{
    struct pm_hfs_transport *t = ctx;
    return (u32)(pm_hfs_tb() / (t->tb_hz / 100u));
}
static int pm_hfs_can_wait(void *ctx)
{
    (void)ctx;
    return !ppc_trap_in_irq(); /* タスク・syscallは1、DEC・外部IRQ中は0。 */
}
static void pm_hfs_idle(void *ctx)
{
    if (pm_hfs_can_wait(ctx)) pm_lan_poll(pm_hfs_tb());
}
/* kernel_trap.c：既存DEC/外部IRQ分岐のhandler呼出しだけを囲む。 */
static volatile u32 trap_irq_depth;
int ppc_trap_in_irq(void) { return trap_irq_depth != 0; }
/* if (handler) { ++trap_irq_depth; handler(f); --trap_irq_depth; return; } */
```

- trap_irq_depthは新しい私有変数。既存DECのkctx_irq_depth操作を変更せず、MPICのIPI callbackも外側で覆う。
- 起動時にtb_hzを渡し、既存の許容範囲を検証する。64bit除算は既存自前helperをリンクし、libgccを使わない。
- SfsIoへ上記now/can_wait/idleとput/get/put_untilを登録。EEを有効化せず、USER入力を消費しない。
- put_untilはcan_waitを確認し、LANがRUNNING以外なら即座に負を返す。GMACなし・未初期化でも装置を触らない。
- RUNNINGでは絶対期限を符号付き差分で判定し、TX混雑だけをpoll付きで再試行する。状態は各送信前に再確認する。
- 送信済みで応答が来ない場合は既存RPC期限に任せる。全体100tick・再送10tickと1RPC制約を維持する。
- RX処理はコピーと観測まで。RX内からRPCを呼ばず、再送・sid/seq/op照合は既存SfsClientへ任せる。

**FS の観測とホストのboot追従**

- FS専用nonceは端末nonceと分離する。FS_OPENは各BEACONへの通知に使い、同一boot中は同じ値を保持する。
- FS_REQは外側nonce=0で送る。ホストはその値を条件にせず、許可peer・現在boot・内側フレームを検査する。
- FS_RSPにはFS専用nonceを載せる。ゲストは許可peerの検証済み新nonceをFS_OPENと同じ観測として扱う。
- 同nonceの反復は無操作。初回応答でも観測できるため、FS_OPENより先にHELLOが出ても応答を拒否しない。
- host_alive_snapshotはその観測を返す。nonce変更時にローカルhost_generation・observationを進める。
- persistent所有者が変更を読み、mount世代更新・再HELLOを行う。失効した応答や旧FDを新接続へ流用しない。
- ホストは許可peer・宛先・codec検査後、新boot_idのBEACONで現在bootを切り替える。FS_REQ等では切り替えない。
- 同bootのBEACONは再初期化しない。切替済みboot_idは退役集合へ残し、旧BEACONを含む旧bootのframeを捨てる。
- boot_idには大小関係を設けない。初見BEACONを採用し、一度退役したbootへ戻らない。
- 明示的boot固定オプションは既存の絞込み契約を維持し、通常のS-D起動・A1では指定しない。

|新boot採用時に初期化する物|処置|
|---|---|
|現在boot|新IDへ更新。旧IDを退役集合へ追加し、集合自体は切替で消さない|
|端末|connected=false、in_seq/out_next=0、端末nonceを更新してTERM_OPEN|
|診断pending|旧要求を破棄し、next_idを初期値へ。旧応答はboot検査で拒否|
|ログ追跡|high・max_next_seq・seen・boot内generationを初期化。保存済みログは保持|
|FS Server|新しいServerを作り、sid・closed・cache・その他セッション状態を初期化|
|FS adapter|high_seq・last_request/response・HELLO nonce/responseを初期化|
|FS外側nonce|新しい16B値へ更新し、FS_OPEN/RSPに使用|
|維持する物|保存ディレクトリの実ファイル、root・許可設定、peer・輸送、退役boot集合|

**ホスト側の受け手**

```python
# ppc_hostfs.py：FSの生成時およびboot切替時。
server = sfs.Server(fs, max_payload=sfs.LAN_MAX_PAYLOAD)  # 1028
# adapterはLanServerのHELLO nonce再送・high_seqガードを再利用する。
reply = adapter.handle_complete_sfs(m["payload"], source_mac)
if reply is not None:
    send_fs_rsp(reply, boot=current_boot, nonce=fs_nonce)
```

- HostFSのパス検証・ROFS契約を使い、`--root` 未指定ではFSサービスを開始しない。許可は `--allow-write` を反復指定する。
- LanServerのEtherType 0x88b5の包みは使わず、PLN1検証後の完全SFS要求へ同じ複製・HELLO処理を適用する。
- 同HELLO nonceは保存したHELLO応答を再送。同seqの同一要求は再応答し、古いseq・同seq別内容は実行しない。
- 素のServerの直前1件cacheだけに依存せず、Serverの既存処理も保持する。seqはセッション内で周回させない。
- WRITE後のcloseを保存成功の根拠とし、既存の無操作syncに電源断耐性を追加したとは扱わない。

**4. 大きさの見積もり**

|区分|予算|
|---|---|
|新規・変更製品コード|約1100〜1500行。共通FS・VFS変更は0、PPC trapの照会を追加|
|新規試験・受入道具|約1300〜1500行。各試験ファイル400行以下|
|PPC text/rodata|既存SerialFS閉包込み概算16〜32KiB。リンクmapで確定|
|常駐RAM|client・hostfs受信枠・TX拡張込み概算8〜16KiB。s.rx・既存messageは不変|
|一時stack|大型lan_message由来の増加を撤回。FS検証・既存FS局所配列を含め追加4〜8KiB以内で計測|
|heap・転送|全ファイルcacheなし。READ最大1024B、WRITEは1028Bから既存header/path分を引く|

- 192KiB heap拡張を前提にせず、64KiBの保存・読戻しで時間と再送数を実測する。

**5. ファイル別の実装段**

- 各段は1ファイル・差分400行以下。「共」は共有領域、「★」はx86生成物変更を伴う本線段。

|段|ファイル|変更・依存|行予算／x86|
|---|---|---|---|
|P01|platform/powermac/hostfs.h 新規|輸送・起動・観測の宣言、P04〜08へ結線|70／不変|
|P02|platform/powermac/lan_boot.h|256B維持、FS型・header型・外部payload API|80／不変|
|P03|platform/powermac/lan_wire.c|header decode・外部encode・既存wrapper、P02|140／不変|
|P04|platform/powermac/hostfs_io.c 新規|TB・SfsIo・完全RX・期限TX、P01〜03/P12|300／不変|
|P05|platform/powermac/hostfs_peer.c 新規|FS_OPEN/RSP観測・HostAlive、P01|150／不変|
|P06|platform/powermac/lan_service.c|FS直接RX・外部payload TX・状態gate、P04/05|150／不変|
|P07|platform/powermac/root_boot.c|COM1追加・sizeofで登録個数算出|10／不変|
|P08|platform/powermac/hostfs_boot.c 新規|FS登録・時計設定・persistent開始|50／不変|
|P09|init/ppc_kernel.c|第3像のroot後・sh前にP08を呼ぶ|20／不変|
|P10|build/ppc-kboot.mk|FS閉包・provider・既存除算helper確認、P09|65／不変|
|P11|arch/ppc/kernel_trap.c|DEC/外部IRQの私有depth・照会関数|25／不変|
|P12|arch/ppc/ppc_trap.h|P11の照会宣言、P04へ結線|5／不変|
|H01|tools/ppc_hostfs.py 新規|LanServer契約再利用・1028B・boot reset|280／不変|
|H02|tools/ppc_lan_host.py|codec・オプション・boot追従・初期化表|180／不変|
|T01|tools/tests/ppc_hostfs_wire.py 新規|C/Python境界・外部payload・256B保護|330／不変|
|T02|tools/tests/ppc_hostfs_io.c 新規|実SfsIo・偽TB/LAN・IRQ文脈・状態gate|360／不変|
|T03|tools/tests/ppc_hostfs_io.py 新規|T02のbuild/run/CHECK/変異・trap接続|180／不変|
|T04|tools/tests/ppc_hostfs_server.py 新規|実Server・遅延複製・boot/nonce・保存|360／不変|
|T05|tools/accept/ppc_sd.py 新規|第3像受入・ホスト継続稼働でゲスト再起動|350／不変|
|D01|docs/decisions/ppc/PPC_SHELL_W8_DESIGN.md 新規|共：確定した本契約|260〜320／不変|
|D02|docs/decisions/ppc/ARCH_BRINGUP_ORDER.md|共：S-D到達条件・試験入口|15／不変|

- #2の再見積りはP02=45→80、P03=45→140、P06=100→150、T01=230→330行。
- 共通serialfsの4実装をリンクし、UART session・旧LAN輸送は組まない。VFS・kernel・SDK・shell共通部は変更しない。
- ★PC-98後続段は本線が起動後mountを結線し、FD/HDD/Host Servicesの保存・再起動をNP21/Wで受け入れる。
- ★既存HostDrvとの `/host` 競合は常駐所有者のBUSY契約を保ち、二重mountしない。

**6. 受入項目 → 試験・変異・関門**

|受入項目|host / QEMU試験|1行変異 → RED|
|---|---|---|
|A1 保存・再起動|T04/T05：ホスト稼働のままゲスト再起動、実体とcat照合、旧BEACON再注入|boot切替を無効化 → 読戻し失敗|
|A2 root/tmp境界|T05：起動媒体切離し、root書込み拒否、tmp消失|mount先を/tmpへ → 境界不一致|
|A3 最大frame|T01：1044B、1024B READ、BE/LE、256B guard、切詰め・過大長拒否|FSを通常decodeへ → guard破損または受信失敗|
|A4 再送・遅延複製|T02/T04：応答欠落、後続要求後の旧WRITE、同HELLO再送、1028B合意|high_seqガード無効化 → 旧WRITE再実行|
|A5 ホスト不在・文脈|T02/T05：EE=0 syscallで期限、tmp続行、DEC/外部IRQは待機拒否|nowを停止tickへ → timeout|
|A6 再開・旧FD|T02/T04/T05：新nonce、FS_OPEN前HELLO応答、旧FD失効、ホスト再起動|FS_RSPの観測削除 → 初回観測・世代不一致|
|A7 端末共存|T05：転送中TERM入力・診断、端末再接続後cat|idleのpoll削除 → 応答欠落|
|A8 保存範囲|T04：許可内成功、範囲外・RO先拒否|許可検査迂回 → 禁止先に実ファイル|
|A9 大きなファイル|T04/T05：64KiB・短い上書き・append・列挙|2個目offsetを0へ → 内容不一致|
|A10 既存利用者・LAN状態|S-C・診断/CLI回帰、T02で全非RUNNING状態とGMACなし|状態gate削除 → 禁止状態でTXを検出|

- G1：P02/P03/H01＋T01/T04でwire・Server・複製契約を固定。boot統合はH02後にもT04で確認する。
- G2：P04〜08/P11/P12＋T02/T03でmount・TB期限・IRQ文脈・世代・VFS接続を固定する。
- G3：P09/P10/H02＋T05。第3像を再buildし、mac99の7450/7455でS-DとS-CのUSER起動・子exec復帰を確認する。
- 各CHECKに製品入力を宣言。変更行の既存変異anchorを検索し、一意なanchorと正常対照・実行時REDを確認する。
- 追加・変更試験の `--mutate` と `python3 -B tools/checks/run.py --lint` を実行し、最後の変更後の完走ログだけを採用する。
- 実装者は触ったPPC像をbuild。PM/CIはx86生成物を基点比較し、PPC診断/CLI像はcodec・trap変更を含め動作回帰する。
- check-changed・全変異は実装者では実行しない。x86不変のW8段はNP21/W不要、★段は本線の受入で確認する。
- G4：実機G4で保存・断線・再起動を確認する。未実施は延期台帳へ残し、QEMU合格と区別する。

**7. 未決と反論**

- ユーザー判断が必要な未決は0件。保存root・書込み許可範囲は起動オプションで指定する運用値。
- 電源断耐性・ファイル全体の原子的保存は、必要になった時点で別段の契約とする。
- 反論：なし。#1〜#8を反映し、PM決定#3・#4・#5を採用した。

r1→r2 の変更の数 8 / 反論の数 0


## 確定 (2026-11-12、Opus 5.5 の第 2 回の条件付き確定を PM が反映)
- R2-1 (P2): 起動の引数 `os32-lan-boot=` で boot_id を固定すると再起動しても boot_id が同じになる。ホストは同じ boot の中で BEACON の seq が巻き戻ったとき (既存の「possible restart」) にも、FS の Server と adapter を初期化する (§3 の初期化の表に行を足す扱い)。T04 に 1 件足す。
- R2-2 (P3): 起動の後、FS_OPEN より先に HELLO が出るとその応答の nonce が最初の観測になり、次の操作で HELLO が 1 回余分に出る。害は無く想定どおり。T02 で回数を数えるなら期待値は 2。
- R2-3 (P3、PM 決定): P11 は新しい私有の depth を作らず、既存の kctx_irq_depth を DEC と外部割り込みの分岐で増減させて流用する (exec_longjmp で戻されるので安全)。can_wait は kctx_irq_depth == 0。
- 共通の場所: 共通の serialfs の 4 実装をそのまま link し、VFS・kernel・SDK・共通の sh は変えない。全段 x86 不変。PC-98 の後の段 (起動の後の mount の結線、FD・HDD・Host Services の保存・再起動の NP21/W の受入) は本線が起こす。

## 実装で決めたこと (2026-11-12、PM)
- 段の分け方: A (P02・P03・T01) → C (H01・H02・T04) → B (P01・P04・P11・P12・T02・T03) → D (P05〜P10) → E (T05・D02)。T05 の置き場は sb_run・sc_run と並べて `tools/ppc/sd_run.py` (表の tools/accept/ppc_sd.py から変更)、検査名 ppc-sd。T01 は検査 ppc-hostfs-wire、T02/T03 は ppc-hostfs-io。
- P11: kctx_irq_depth の定義を持たない試験の fixture 4 本 (kboot_vectors_fixture.c・kboot_trap_host.c・test_user_trap_host.c・test_lan_p22d_host.c) には試験の側に定義を足した。製品の共有の所は変えない。P12 は変更なし。
- 共通の fs/serialfs/sfs_proto.h の `SFS_TICK_MS` を `TICK_MS` から `10u` に (SfsIo.now の 10ms の約束)。PowerPC の像の監査 (PIT_HZ の利用者の禁止) に当たったため。本線 PM 了承、x86 の生成物は不変。x86 の SfsIo.now の供給元は tick_count (serialfs_session.c の sio_now、kernel_boot_memory.inc の boot_sfs_now)。TICK_MS・PIT_HZ のほかの利用者は本線の段 L。
- P06・P07: lan_service.c・root_boot.c の追加は KBOOT_SH の像だけに入れ、診断・CLI の像を不変に保つ。hostfs.h に pm_hostfs_boot・pm_hfs_bind_peer・pm_hfs_peer を足した。FS_REQ の宛先は最後に観測した FS の peer、未観測なら boot の peer_mac。
- T05 の変異 no-boot-follow は「ホストは boot2 を採用するが FS の Server/adapter と FS の nonce を持ち越す」形 (レビュー第 1 回の指摘で変更)。不在の時のエラーまで約 1.0 秒。
