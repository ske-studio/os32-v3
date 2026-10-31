/* D5a: exercise the real ISO/ATAPI path; request and READ(10) logs are separate. */
typedef struct {
    u32 next;
} DirExpected;

static void dir_cb(const VfsDirEntry *e, void *raw)
{
    DirExpected *d = raw;
    char name[32];
    sprintf(name, "E%05u.TXT", (unsigned)d->next);
    CHECK(strcmp(e->name, name) == 0);
    CHECK(e->size == TINY_SIZE);
    CHECK(e->type == VFS_TYPE_FILE);
    d->next++;
}

/* Put the directory at the very end of the volume; one named record per sector.
 * Padding is zero, with an additional record beyond a partial last sector's size. */
static u32 dir_fixture(Iso9660Ctx *c, u32 size)
{
    u32 n = size ? (size - 1) / SEC + 1 : 0;
    u32 base = g_media_a.secs - n;
    c->root_lba = base;
    c->root_size = size;
    for (u32 i = 0; i < n; i++) {
        char name[32];
        u8 *p = g_media_a.img + (base + i) * SEC;
        memset(p, 0, SEC);
        sprintf(name, "E%05u.TXT;1", (unsigned)i);
        u32 len = put_rec(p, TINY_LBA, TINY_SIZE, 0, name, (int)strlen(name));
        if (i == n - 1 && size % SEC)
            put_rec(p + len, TINY_LBA, 999, 0, "OUTSIDE;1", 9);
    }
    iso_drop_caches(c);
    model_reset_counts();
    g_dev_n = 0;
    return base;
}

static void dir_requests(u32 base, u32 n, int window)
{
    u32 next = base;
    CHECK(g_dev_n == (window ? ceil_div(n, ISO_RA_SECTORS) : n));
    CHECK(M.n_read10 == g_dev_n);
    for (u32 i = 0; i < g_dev_n; i++) {
        u32 count = window ? n - (next - base) : 1;
        if (count > ISO_RA_SECTORS) count = ISO_RA_SECTORS;
        CHECK(g_dev_lba[i] == next && g_dev_cnt[i] == count);
        CHECK(M.log_lba[i] == next && M.log_cnt[i] == count);
        next += count;
    }
    CHECK(next == base + n);
    CHECK(M.n_sectors == n);
}

static void t_dir_batch(void)
{
    static const u32 sizes[] = { 0, SEC, 4 * SEC, 16 * SEC, 17 * SEC, 32 * SEC,
                                 16 * SEC + 46 };
    for (int window = 0; window <= 1; window++) {
        for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
            setup(); g_kmalloc_fail = !window;
            Iso9660Ctx *c = do_mount(); g_kmalloc_fail = 0;
            u32 size = sizes[k], n = size ? (size - 1) / SEC + 1 : 0;
            u32 base = dir_fixture(c, size);
            DirExpected d = {0};
            CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_OK);
            CHECK(d.next == n);
            printf("dir_batch window=%d sectors=%u READ(10)=%u Device=%u\n",
                   window, (unsigned)n, (unsigned)M.n_read10, (unsigned)g_dev_n);
            dir_requests(base, n, window);
            if (n > 1) CHECK(c->stats.scache_fills == (window ? 0 : n));
            if (window && n > ISO_SCACHE_SLOTS && n <= ISO_RA_SECTORS) {
                u32 before = M.n_read10, hits = c->stats.ra_hits;
                u32 lba, found_size; u8 flags;
                CHECK(iso_find_in_dir(c, base, size, "E00000.TXT",
                                      &lba, &found_size, &flags) == VFS_OK);
                CHECK(found_size == TINY_SIZE && lba == TINY_LBA);
                CHECK(M.n_read10 == before && c->stats.ra_hits == hits + 1);
            }
            /* Last visited sectors remain in LRU, even though a window was used. */
            if (n) {
                u32 before = M.n_read10;
                c->ra_valid = 0;
                CHECK(iso_get_sector(c, base + n - 1));
                CHECK(M.n_read10 == before);
            }
            iso9660_ops.umount(c);
        }
    }
}

static void t_dir_find(void)
{
    static const u32 indices[] = {0, 16, 32}; /* first, last, absent */
    for (unsigned k = 0; k < sizeof(indices) / sizeof(indices[0]); k++) {
        setup(); Iso9660Ctx *c = do_mount();
        u32 base = dir_fixture(c, 17 * SEC), size;
        char name[32];
        sprintf(name, "/E%05u.TXT", (unsigned)indices[k]);
        CHECK(iso9660_ops.get_file_size(c, name, &size) ==
              (indices[k] < 17 ? VFS_OK : VFS_ERR_NOTFOUND));
        if (indices[k] < 17) CHECK(size == TINY_SIZE);
        dir_requests(base, indices[k] == 0 ? ISO_RA_SECTORS : 17, 1);
        u32 before = M.n_read10;
        CHECK(iso9660_ops.get_file_size(c, "/E00016.TXT", &size) == VFS_OK);
        if (indices[k] == 16) CHECK(M.n_read10 == before);
        iso9660_ops.umount(c);
    }
}

/* One callback overwrites the shared window with a file, or a nested directory.
 * All later requests made by the parent scan must be single-sector requests. */
typedef struct {
    DirExpected expected;
    Iso9660Ctx *c;
    int nested;
} DirReentry;

static void dir_reentry_cb(const VfsDirEntry *e, void *raw)
{
    DirReentry *r = raw;
    dir_cb(e, &r->expected);
    if (r->expected.next != 1) return;
    if (r->nested) {
        DirExpected child = {0};
        u32 save_lba = r->c->root_lba, save_size = r->c->root_size;
        r->c->root_lba = ROOT_LBA; r->c->root_size = SEC;
        CHECK(iso9660_ops.list_dir(r->c, "/", dir_cb, &child) == VFS_OK);
        CHECK(child.next == 1);
        r->c->root_lba = save_lba; r->c->root_size = save_size;
    } else {
        u8 buf[9];
        CHECK(iso9660_ops.read_stream(r->c, "/E00000.TXT", buf, sizeof(buf), 3) == 9);
        for (u32 i = 0; i < sizeof(buf); i++)
            CHECK(buf[i] == file_byte(&g_media_a, TINY_LBA, i + 3));
    }
}

static void t_dir_reentry(void)
{
    for (int window = 0; window <= 1; window++) {
        for (int nested = 0; nested <= 1; nested++) {
            setup(); g_kmalloc_fail = !window;
            Iso9660Ctx *c = do_mount(); g_kmalloc_fail = 0;
            u32 base = dir_fixture(c, 16 * SEC);
            memset(g_media_a.img + ROOT_LBA * SEC, 0, SEC);
            put_rec(g_media_a.img + ROOT_LBA * SEC, TINY_LBA, TINY_SIZE,
                    0, "E00000.TXT;1", 12);
            DirReentry r = { .c = c, .nested = nested };
            CHECK(iso9660_ops.list_dir(c, "/", dir_reentry_cb, &r) == VFS_OK);
            CHECK(r.expected.next == 16);
            u32 commands = 0, sectors = 0;
            for (u32 i = 0; i < g_dev_n; i++) {
                if (g_dev_lba[i] < base || g_dev_lba[i] >= base + 16) continue;
                if (commands) CHECK(g_dev_cnt[i] == 1);
                commands++; sectors += g_dev_cnt[i];
            }
            CHECK(commands == 16);
            CHECK(sectors == (window ? 31 : 16));
            iso9660_ops.umount(c);
        }
    }
}

static void t_dir_failure(void)
{
    for (int needed_bad = 0; needed_bad <= 1; needed_bad++) {
        setup(); Iso9660Ctx *c = do_mount();
        /* Unrelated path/LRU entries must survive the failed refill. */
        u32 size;
        CHECK(iso9660_ops.get_file_size(c, "/TINY.TXT", &size) == VFS_OK);
        u32 base = dir_fixture(c, 16 * SEC);
        CHECK(iso_get_sector(c, ROOT_LBA));
        c->pc_valid = 1; strcpy(c->pc_path, "/saved"); c->pc_size = 77;
        c->pc_flags = 0; c->pc_lba = TINY_LBA;
        c->ra_valid = 1; c->ra_lba = base; c->ra_count = 1;
        /* Force refill, not a window hit; poison any partially read buffer. */
        c->ra_lba = base - 1;
        M.fail_lba = (long)(base + (needed_bad ? 0 : 2));
        g_dev_poison = 1;
        DirExpected d = {0};
        CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_ERR_IO);
        CHECK(d.next == (needed_bad ? 0u : 2u));
        CHECK(!c->ra_valid && c->pc_valid);
        CHECK(iso9660_ops.get_file_size(c, "/saved", &size) == VFS_OK && size == 77);
        if (!needed_bad) {
            u32 before = M.n_read10;
            CHECK(iso_get_sector(c, ROOT_LBA));
            CHECK(M.n_read10 == before);
        }
        CHECK(g_dev_cnt[1] == 16); /* [0] was unrelated ROOT_LBA */
        for (u32 i = 2; i < g_dev_n; i++) CHECK(g_dev_cnt[i] == 1);
        /* Lower layer tries singles until the bad sector, FS rereads only needed. */
        CHECK(M.n_read10 == (needed_bad ? 4u : 8u));
        iso9660_ops.umount(c);
        g_dev_poison = 0;
    }
}

static void t_dir_media(void)
{
    setup(); Iso9660Ctx *c = do_mount();
    DirExpected d = {0};
    g_sw_calls = g_sw_arm = g_sw_read = g_sw_deep_b = 0;
    CHECK(iso9660_ops.list_dir(c, "/SUB", sw_cb, NULL) == VFS_OK);
    /* A known generation change before a cache hit drops all shared caches. */
    M.m = &g_media_b; s_media_gen++;
    u32 size;
    CHECK(iso9660_ops.get_file_size(c, "/SUB/DEEP.BIN", &size) == VFS_OK);
    CHECK(size == DEEP_SIZE_B);
    CHECK(c->stats.drops > 0);
    iso9660_ops.umount(c);
    /* Tick wrap and the exact boundary retain the existing unsigned rule. */
    setup(); c = do_mount();
    dir_fixture(c, SEC);
    tick_count = (u32)0 - 11u; /* wrap at the target/host u32 width */
    CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_OK);
    u32 before = M.n_read10;
    tick_count += ISO_IDLE_TICKS; d.next = 0;
    CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_OK);
    CHECK(M.n_read10 == before);
    tick_count++; d.next = 0;
    CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_OK);
    CHECK(M.n_read10 == before + 1);
    iso9660_ops.umount(c);
}

static void t_dir_tail(void)
{
    setup(); Iso9660Ctx *c = do_mount();
    u32 base = dir_fixture(c, 16 * SEC + 30);
    DirExpected d = {0};
    CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_OK);
    CHECK(d.next == 16); /* last record extends beyond the directory byte size */
    u32 lba, size; u8 flags;
    iso_drop_caches(c);
    CHECK(iso_find_in_dir(c, base, 16 * SEC + 30, "E00016.TXT",
                          &lba, &size, &flags) == VFS_ERR_NOTFOUND);
    /* A child extent longer than the volume still cannot make a window read
     * past the last physical sector. No new validation of ISO names is added. */
    dir_fixture(c, SEC);
    c->root_size = 17 * SEC;
    d.next = 0;
    CHECK(iso9660_ops.list_dir(c, "/", dir_cb, &d) == VFS_ERR_IO);
    CHECK(d.next == 1 && g_dev_n == 1 && g_dev_cnt[0] == 1);
    iso9660_ops.umount(c);
}
