/* T4c-3: real IME + product-config ILP32 SQLite/MEMSYS5 + VFS/FD + ext2.
 * Only block I/O, owner/TTY and kernel heap are host boundaries. The RAM disk
 * is disposable; SQLite uses only its product 512KiB pool. Module adapters'
 * allocation context calls are explicit here; service startup is T4c-2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#define __KSTRING_H
void *kmemcpy(void *d, const void *s, u32 n) { return memcpy(d, s, n); }
void *kmemset(void *d, int c, u32 n) { return memset(d, c, n); }
u32 kstrlen(const char *s) { return strlen(s); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
int kstrncmp(const char *a, const char *b, u32 n) { return strncmp(a, b, n); }
char *kstrncpy(char *d, const char *s, u32 n)
{
    u32 i = 0;
    if (!n) return d;
    while (i + 1 < n && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
    return d;
}
char *kstrncat(char *d, const char *s, u32 n)
{
    u32 used = kstrlen(d);
    if (used < n) kstrncpy(d + used, s, n - used);
    return d;
}
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

#include "ext2_priv.h"
#include "ide.h"
#include "kmalloc.h"
#include "hdd_pt_fake.h"

void *kzalloc(u32 n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kprintf(u8 attr, const char *fmt, ...) { }
void serial_puts(const char *s) { }
int utf8_strlen(const u8 *s)
{
    int n = 0;
    while (*s) { if ((*s & 0xc0) != 0x80) n++; s++; }
    return n;
}
volatile u32 tick_count;
u32 sys_time(void) { return 0; }
int res_owner_get(void) { return 2; }
int fd_is_redirected(int fd) { return 0; }
u16 fd_redirect_ifmt(int fd, int *out) { *out = -1; return OS_S_IFCHR; }
int fd_redirect_read(int fd, void *buf, u32 n) { return VFS_ERR_INVAL; }
int fd_redirect_write(int fd, const void *buf, u32 n) { return VFS_ERR_INVAL; }
int kbd_getchar_kapi(void) { return '\n'; }
void console_write(const char *buf, u32 size, u8 color) { }

#define DISK_FS_SECTORS 16384u
#define DISK_BASE_LBA 1088u
#define DISK_SECTORS (DISK_BASE_LBA + PT_FAKE_ROUNDUP(DISK_FS_SECTORS))
static u8 disk[DISK_SECTORS * 512u];
static Device hd0;
static unsigned disk_writes;
Device *dev_find(const char *name) { return !strcmp(name, "hd0") ? &hd0 : NULL; }
int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{
    CHECK(dev == &hd0 && count > 0 && lba + (u32)count <= DISK_SECTORS);
    memcpy(buf, disk + lba * 512u, count * 512u);
    return 0;
}
int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{
    CHECK(dev == &hd0 && count > 0 && lba + (u32)count <= DISK_SECTORS);
    memcpy(disk + lba * 512u, buf, count * 512u);
    disk_writes += count;
    return 0;
}
int ide_drive_present(int drive) { return drive == 0; }
int ide_get_info(int drive, IdeInfo *info)
{
    if (drive != 0) return IDE_ERR_NO_DRIVE;
    memset(info, 0, sizeof(*info));
    info->cylinders = 1024; info->heads = 8; info->sectors = 17;
    info->phys_sector_size = 512; info->total_sectors = DISK_SECTORS;
    return IDE_OK;
}
#include "../../fs/ext2_super.c"
#include "../../fs/ext2_inode.c"
#include "../../fs/ext2_dir.c"
#include "../../fs/ext2_file.c"
#include "../../fs/ext2_fmt.c"
#include "../../fs/ext2_vfs.c"
#include "../../fs/vfs.c"
#include "../../fs/vfs_fd.c"
#include "../../lib/sqlite3/sqlite3.h"
#include "../../lib/sqlite3/os32_sqlite_vfs.c"
#define OS32_SQLITE_DIRECT_TEST 1
#include "../../kernel/ime_dict.c"

static void close_db(Os32SqliteGroup *g, sqlite3 *db)
{
    CHECK(os32_sqlite_group_begin_close(g) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(g, SQLITE_OK) == SQLITE_OK);
}
static void setup(void)
{
    Os32SqliteGroup group;
    sqlite3 *db = NULL;
    CHECK(sizeof(void *) == 4);
    CHECK(os32_sqlite_init() == SQLITE_OK);
    CHECK(pt_fake_write(disk, DISK_BASE_LBA, DISK_SECTORS - DISK_BASE_LBA) == 0);
    hd0.name = "hd0"; hd0.type = DEV_BLOCK; hd0.bus_type = DEV_BUS_IDE;
    hd0.sect_size = 512; hd0.total_sects = DISK_SECTORS;
    hd0.heads = 8; hd0.spt = 17;
    CHECK(ext2_format(0, DISK_FS_SECTORS) == EXT2_OK);
    ext2_init();
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    CHECK(vfs_mkdir("/db") == VFS_OK);
    CHECK(vfs_sqlite_backend("/db/fep.db") == VFS_SQLITE_EXT2);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &group) == SQLITE_OK);
    os32_sqlite_allocation_context(NULL, &group);
    CHECK(os32_sqlite_group_open(&group, "/db/fep.db",
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db,
        "PRAGMA page_size=1024;"
        "CREATE TABLE dict(yomi TEXT,kanji TEXT,pos_id INT,cost INT);"
        "CREATE INDEX idx_dict_yomi ON dict(yomi);"
        "INSERT INTO dict VALUES('か','科',1,10),('にほんご','日本語',2,20);",
        NULL, NULL, NULL) == SQLITE_OK);
    close_db(&group, db);
}
static void open_dict(IME_Dict *dict)
{
    /* Open has not acquired its group yet. The product FEP adapter selects
     * RESIDENT allocation context at this same boundary before open. */
    budget->context = BUDGET_FEP;
    CHECK(ime_dict_open(dict, "/db/fep.db") == 0);
    CHECK(dict->learn_stmt);
    CHECK(os32_sqlite_boot_check(1, OS32_SQLITE_FEP_RESERVE) == SQLITE_OK);
}
static void conversion(IME_Dict *dict)
{
    IME_Result result[IME_MAX_RESULTS];
    os32_sqlite_allocation_context(dict->db, &dict->group);
    CHECK(ime_dict_search(dict, "か", result, IME_MAX_RESULTS) == 1);
    CHECK(!strcmp(result[0].kanji, "科"));
    CHECK(ime_dict_search(dict, "にほん", result, IME_MAX_RESULTS) == 1);
    CHECK(!strcmp(result[0].kanji, "日本語"));
}

/* Keep app SQL allocations alive, then fill its rounded remainder exactly.
 * Do not warm the FEP statements before pressure: search must allocate while
 * APP+SYSTEM is already at the product cap, with no spare admission credit. */
static void pressure(IME_Dict *dict)
{
    Os32SqliteGroup group;
    sqlite3 *app = NULL;
    static void *held[8192];
    unsigned count = 0, rows = 0;
    const unsigned limit = 319488u;
    int rc;
    CHECK(BUDGET_CAPACITY - budget->reserve == limit);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 2, &group) == SQLITE_OK);
    os32_sqlite_allocation_context(NULL, &group);
    CHECK(os32_sqlite_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &app) == SQLITE_OK);
    CHECK(sqlite3_exec(app, "CREATE TABLE pressure(x)", NULL, NULL, NULL) == SQLITE_OK);
    while ((rc = sqlite3_exec(app, "INSERT INTO pressure VALUES(zeroblob(4096))",
                             NULL, NULL, NULL)) == SQLITE_OK) CHECK(++rows < 200);
    CHECK(rc == SQLITE_NOMEM && rows > 0);
    while (budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] < limit) {
        CHECK(count < sizeof(held) / sizeof(held[0]));
        held[count] = sqlite3_malloc(1);
        CHECK(held[count]); count++;
    }
    CHECK(budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] == limit);
    CHECK(sqlite3_malloc(1) == NULL);
    conversion(dict);
    CHECK(memsys5_check_canary() == 0);
    CHECK(budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] == limit);
    printf("PASS pressure app=%u system=%u cap=%u fep=%u rows=%u exact/prefix\n",
           budget->live[BUDGET_APP], budget->live[BUDGET_SYSTEM], limit,
           budget->live[BUDGET_FEP], rows);
    os32_sqlite_allocation_context(app, &group);
    while (count) sqlite3_free(held[--count]);
    close_db(&group, app);
    CHECK(budget->live[BUDGET_APP] == 0);
}
static void learned(IME_Dict *dict, int freq)
{
    IME_UserEntry entries[4];
    os32_sqlite_allocation_context(dict->db, &dict->group);
    CHECK(ime_user_list(dict, "か", entries, 4) == 1);
    CHECK(!strcmp(entries[0].yomi, "か") && !strcmp(entries[0].kanji, "蚊"));
    CHECK(entries[0].freq == freq);
}
static void learning(IME_Dict *dict)
{
    IME_Result results[IME_MAX_RESULTS];
    OS32_Stat st;
    unsigned before = disk_writes;
    conversion(dict);
    ime_dict_learn(dict, "か", "蚊");
    learned(dict, 1);
    ime_dict_learn(dict, "か", "蚊");
    learned(dict, 2);
    CHECK(disk_writes > before);
    CHECK(ime_dict_close(dict) == 0);
    CHECK(vfs_sync() == VFS_OK);
    vfs_umount("/");
    CHECK(vfs_mount("/", "hd0", "ext2") == VFS_OK);
    open_dict(dict);
    learned(dict, 2);
    CHECK(ime_dict_search(dict, "か", results, IME_MAX_RESULTS) == 2);
    CHECK(!strcmp(results[0].kanji, "蚊"));
    CHECK(vfs_stat("/db/fep.db-journal", &st) == VFS_ERR_NOTFOUND);
    puts("PASS ext2 RW learning insert/update remount freq=2 ranking journal_deleted");
}
int main(int argc, char **argv)
{
    IME_Dict dict = {0};
    CHECK(argc == 2);
    setup();
    open_dict(&dict);
    if (!strcmp(argv[1], "pressure")) pressure(&dict);
    else { CHECK(!strcmp(argv[1], "learning")); learning(&dict); }
    os32_sqlite_allocation_context(dict.db, &dict.group);
    CHECK(ime_dict_close(&dict) == 0);
    CHECK(memsys5_check_canary() == 0);
    CHECK(budget->live[BUDGET_APP] == 0 && budget->live[BUDGET_FEP] == 0);
    return 0;
}
