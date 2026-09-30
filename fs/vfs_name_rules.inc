/* ========================================================================
 *  vfs_name_rules.inc — 下の FS が「別の綴りを同じ実体」と読む名前を断る純規則
 *
 *  票: docs/archive/kernel_v21/TASK_VFS_FD_PATH.md (実装レビュー ラリー 2 の blocker)
 *  ホスト試験: tools/tests/vfs_fd_path_host.c の段 fatname / hostname
 *
 *  VFS の BUSY / pinned (使用中の SQLite DB・loop イメージ) は、FD を開いた
 *  名前と操作の名前を **1 バイトずつ** 比べる (大文字小文字は VfsOps の
 *  name_fold で畳む)。ところが下の FS は、それ以外にも「違う綴りを同じ名前」
 *  と読む:
 *
 *    FatFs (FF_USE_LFN 0、fs/fatfs/ff.c の非 LFN 版 create_name)
 *      (a) '\' も区切り。先頭・重複した区切りは読み飛ばす ("d\img.dat" =
 *          "D/IMG.DAT"、"\disk.img" = "/DISK.IMG")
 *      (b) 0x20 以下のバイトで名前が終わる ("DISK.IMG " も
 *          "DISK.IMG ignored" も "DISK.IMG")。**途中の空白も**名前を打ち切る
 *      (c) 本体の直後の '.' で拡張子が空なら同じ SFN ("DISK." = "DISK")
 *    Windows (HostDrv の向こうの Win32)
 *      (a) '\' は区切り
 *      (b) 名前の**末尾の**空白と '.' を落とす ("disk.img " / "disk.img." =
 *          "disk.img")。**途中の空白は正当な名前の一部** ("my file.txt")
 *      制御文字 (0x01〜0x1F) は Win32 の名前に使えない
 *
 *  素通しすると vfs_rm("/fd0/disk.img ") が BUSY の比較をすり抜けて、使用中の
 *  実体を消す。畳み方を VFS で再現するより、**意味が変わる綴りを入口で断る**
 *  ほうが狭く確か — 断った後は 1 バイトの fold だけで下の FS と一致する。
 *
 *  断るもの (どちらの規則でも):
 *    - '\'
 *    - 0x20 未満のバイト (制御文字)
 *    - 要素の末尾の空白
 *    - 要素の末尾の '.' ("." / ".." そのものは除く — VFS が正規化で解く)
 *  FAT だけ断るもの:
 *    - 要素の途中の空白 (FatFs はそこで名前を打ち切る。Win32 では正当)
 *  Win32 (HostDrv) だけ断るもの (実装レビュー ラリー 3 B2 / ユーザー決裁):
 *    - Win32 の禁止文字 ':' '*' '?' '"' '<' '>' '|' (':' は代替データ
 *      ストリーム "disk.img::$DATA" = 同じ実体の既定ストリーム)
 *    - **最短形の正しい UTF-8 で、BMP (U+0000〜U+FFFF、サロゲート
 *      U+D800〜U+DFFF を除く) の文字**でないもの: 途中で切れた列
 *      ("disk.img\xC2")、単独の継続バイト ("a\x80")、冗長な符号化
 *      ("\xC1\xA4isk.img" = "disk.img")、サロゲート、4 バイト列 (BMP 外)。
 *      名前は lib/kutf16.c の kutf8_to_utf16le で UTF-16 にしてホストへ
 *      渡る。変換できない名前を通すと、変換の途中終了・冗長形の受理・
 *      U+FFFD への置換で「検査した名前と別の名前」がホストに届き、BUSY /
 *      pinned の比較をすり抜けた。変換側も同じ規則で -1 を返す (二重の防御)。
 *  保証しないもの (既知の制限、docs/06_filesystem.md §6-1): Windows の 8.3
 *  短名 ("DISK-I~1.IMG")、予約名 (CON など)、ASCII 以外の大文字小文字。
 *  これらで HostDrv 上の使用中ファイルを別名で指したときの BUSY / pinned は
 *  効かない (影響は開発者のホスト上のファイルとそれを載せた loop に限る)。
 *
 *  パスは FS の中の相対パス ("/a/b"、"a/b"、"" = 根)。空の要素 ("//"、末尾の
 *  "/") は数えない。戻り値は VFS_OK / VFS_ERR_INVAL。
 * ======================================================================== */

#ifndef VFS_NAME_RULES_INC
#define VFS_NAME_RULES_INC

#define VFS_NAME_RULE_WIN32  0   /* HostDrv: 途中の空白は通す */
#define VFS_NAME_RULE_FAT    1   /* FatFs:   空白はどこでも断る */

/* p[0] から始まる UTF-8 の 1 文字が最短形・BMP・非サロゲートなら
 * そのバイト数 (1〜3)、そうでなければ 0。p[0] は 0 以外。
 * lib/kutf16.c の kutf8_to_utf16le と同じ規則。 */
static u32 vfs_name_utf8_len(const u8 *p)
{
    u8 c = p[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF)
        return ((p[1] & 0xC0) == 0x80) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && p[1] < 0xA0) return 0;     /* 冗長 (< U+0800) */
        if (c == 0xED && p[1] >= 0xA0) return 0;    /* サロゲート */
        return 3;
    }
    return 0;   /* 単独の継続バイト、0xC0 / 0xC1、4 バイト以上 */
}

static int vfs_name_rule_check(const char *path, int rule)
{
    const u8 *p = (const u8 *)path;
    u32 start = 0;
    u32 i;

    if (!path) return VFS_OK;
    for (i = 0; ; i++) {
        u8 c = p[i];
        if (c == '\0' || c == '/') {
            u32 len = i - start;
            if (len > 0) {
                u8 last = p[i - 1];
                int dot_entry = (len == 1 && p[start] == '.') ||
                                (len == 2 && p[start] == '.' && p[start + 1] == '.');
                if (last == ' ') return VFS_ERR_INVAL;
                if (last == '.' && !dot_entry) return VFS_ERR_INVAL;
            }
            if (c == '\0') break;
            start = i + 1;
            continue;
        }
        if (c == '\\') return VFS_ERR_INVAL;
        if (c < 0x20) return VFS_ERR_INVAL;
        if (c == ' ' && rule == VFS_NAME_RULE_FAT) return VFS_ERR_INVAL;
        if (rule == VFS_NAME_RULE_WIN32) {
            u32 n;
            if (c == ':' || c == '*' || c == '?' || c == '"' ||
                c == '<' || c == '>' || c == '|')
                return VFS_ERR_INVAL;
            n = vfs_name_utf8_len(p + i);
            if (n == 0) return VFS_ERR_INVAL;
            i += n - 1;     /* 継続バイトは区切りにも空白にもならない */
        }
    }
    return VFS_OK;
}

#endif /* VFS_NAME_RULES_INC */
