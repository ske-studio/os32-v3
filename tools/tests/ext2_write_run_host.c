/* D6 stage 2: use the stage-1 product/RAM fixture and independent fsck oracle. */
#define start_c batch_start_c
#include "ext2_write_batch_host.c"
#undef start_c

static u32 data_requests(u32 offset, u32 size)
{
    Ext2Inode inode;
    u32 physical[66], first = offset / EXT2_BLOCK_SIZE;
    u32 blocks = (offset % EXT2_BLOCK_SIZE + size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    u32 i, j, requests = 0, covered = 0;
    CHECK(blocks <= 66);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    for (i = 0; i < blocks; i++)
        CHECK(ext2_bmap(&ctx, &inode, first + i, &physical[i]) == 0 && physical[i]);
    for (i = 0; i < event_count; i++) {
        if (!events[i].write) continue;
        for (j = 0; j < blocks; j++) if (events[i].lba == block_lba(physical[j])) break;
        if (j == blocks) continue;
        CHECK(j == covered && events[i].rc == 0);
        CHECK(events[i].count > 0 && events[i].count <= 64 && !(events[i].count % 2));
        u32 n = (u32)events[i].count / EXT2_SECTORS_PER_BLOCK, k;
        CHECK(j + n <= blocks);
        for (k = 0; k < n; k++) CHECK(physical[j + k] == physical[j] + k);
        if (offset % EXT2_BLOCK_SIZE && j == 0) CHECK(n == 1);
        if ((offset + size) % EXT2_BLOCK_SIZE && j + n == blocks) CHECK(n == 1);
        CHECK(j / EXT2_WRITE_WINDOW_BLOCKS == (j + n - 1) / EXT2_WRITE_WINDOW_BLOCKS);
        covered += n; requests++;
    }
    CHECK(covered == blocks);
    return requests;
}

static void case_lengths(void)
{
    static const u32 lengths[] = {64, 1, 2, 32, 33};
    u32 i, existing, n, offset = 269u * EXT2_BLOCK_SIZE;
    for (existing = 0; existing < 2; existing++) {
        for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            n = lengths[i]; prepare(1);
            if (existing) {
                CHECK(ext2_write_stream(&ctx, target, input, n * EXT2_BLOCK_SIZE, offset) ==
                      (int)(n * EXT2_BLOCK_SIZE));
                read_check(offset, n * EXT2_BLOCK_SIZE); /* warm D1, then change every byte */
                u32 j;
                for (j = 0; j < n * EXT2_BLOCK_SIZE; j++) input[j] ^= 0xa5;
            }
            trace_reset();
            CHECK(ext2_write_stream(&ctx, target, input, n * EXT2_BLOCK_SIZE, offset) ==
                  (int)(n * EXT2_BLOCK_SIZE));
            CHECK(data_requests(offset, n * EXT2_BLOCK_SIZE) == (n + 31) / 32);
            read_check(offset, n * EXT2_BLOCK_SIZE);
            sample(1);
        }
    }
}

static void case_boundaries(void)
{
    Ext2Inode inode;
    u32 phys, i, offset = 269u * EXT2_BLOCK_SIZE + 13;
    prepare(1);
    kmemset(ext2_g_dat, 0xa7, EXT2_BLOCK_SIZE);
    CHECK(ext2_write_stream(&ctx, target, input, 65536, offset) == 65536);
    CHECK(data_requests(offset, 65536) == 4); /* edge, 32, 31; next window's edge */
    read_check(offset, 65536);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    CHECK(ext2_bmap(&ctx, &inode, 269, &phys) == 0);
    for (i = 0; i < 13; i++) CHECK(media_block(phys)[i] == 0);
    CHECK(ext2_bmap(&ctx, &inode, 333, &phys) == 0);
    for (i = 13; i < EXT2_BLOCK_SIZE; i++) CHECK(media_block(phys)[i] == 0);
    sample(1);
    /* Existing edges preserve bytes outside the caller's range. */
    prepare(1);
    CHECK(ext2_write_stream(&ctx, target, input, 65u * EXT2_BLOCK_SIZE,
                           269u * EXT2_BLOCK_SIZE) == 65 * EXT2_BLOCK_SIZE);
    read_check(269u * EXT2_BLOCK_SIZE, 65u * EXT2_BLOCK_SIZE);
    for (i = 0; i < 65536; i++) input[i] ^= 0xa5;
    trace_reset();
    CHECK(ext2_write_stream(&ctx, target, input, 65536, offset) == 65536);
    CHECK(data_requests(offset, 65536) == 4);
    read_check(offset, 65536);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    CHECK(ext2_bmap(&ctx, &inode, 269, &phys) == 0);
    for (i = 0; i < 13; i++) CHECK(media_block(phys)[i] == (u8)(input[i] ^ 0xa5));
    CHECK(ext2_bmap(&ctx, &inode, 333, &phys) == 0);
    same(media_block(phys) + 13, input + 65536 + 13, EXT2_BLOCK_SIZE - 13);
    sample(1);
    /* New single-indirect table allocation interrupts physical adjacency. */
    prepare(0);
    CHECK(ext2_write_stream(&ctx, target, input, 65536, 0) == 65536);
    CHECK(data_requests(0, 65536) == 3); /* 13 data, then a table gap, 32, 19 */
    read_check(0, 65536); sample(1);
    /* A new double-indirect leaf splits a run inside the same window. */
    prepare(1); offset = 522u * EXT2_BLOCK_SIZE;
    CHECK(ext2_write_stream(&ctx, target, input, 65536, offset) == 65536);
    CHECK(data_requests(offset, 65536) == 3);
    read_check(offset, 65536); sample(1);
    case_fragment();
    CHECK(data_requests(0, 65536) > 32);
    read_check(0, 65536);
    for (i = 0; i < 65536; i++) input[i] ^= 0xa5;
    trace_reset();
    CHECK(ext2_write_stream(&ctx, target, input, 65536, 0) == 65536);
    CHECK(data_requests(0, 65536) > 32);
    read_check(0, 65536); sample(1);
    /* Closing/remounting and switching contexts leaves no write owner. */
    ext2_unmount(&ctx);
    CHECK(!ext2_write_work.active && !ext2_write_work.ctx && cache_slots_invalid());
    kmemset(&saved_ctx, 0, sizeof(saved_ctx));
    CHECK(ext2_mount(&saved_ctx, 0) == 0);
    CHECK(ext2_write_stream(&saved_ctx, target, input, 65536, 0) == 65536);
    CHECK(!ext2_write_work.active && !ext2_write_work.ctx);
    CHECK(ext2_read_stream(&saved_ctx, target, output, 65536, 0) == 65536);
    same(output, input, 65536);
    ext2_unmount(&saved_ctx);
    CHECK(ext2_mount(&ctx, 0) == 0);
    read_check(0, 65536); sample(1);
}

static u32 run_offset(int op)
{
    if (op == 6) return 269u * EXT2_BLOCK_SIZE;
    if (op == 7) return 269u * EXT2_BLOCK_SIZE + 13;
    if (op == 1 || op == 2) return 0;
    return expected_offset(op);
}

static u32 run_size(int op)
{
    if (op == 1 || op == 2 || op == 4) return 70000;
    if (op == 3) return 4097;
    if (op == 5) return 17;
    return 65536;
}

static int run_operation(int op)
{
    if (op < 6) return operation(op);
    return ext2_write_stream(&ctx, target, input, run_size(op), run_offset(op));
}

static void case_run_faults(int op)
{
    static struct { u32 lba, prefix; int count, write, data; } expected[EVENTS_MAX];
    Ext2Inode inode;
    u32 physical[70], old_physical[70], old_size, count, blocks, i, j, k, prefix = 0, original_target;
    u32 offset = run_offset(op), size = run_size(op);
    int mode;
    prepare(1);
    if (op == 6) {
        CHECK(ext2_write_stream(&ctx, target, input, size, offset) == (int)size);
        read_check(offset, size);
        for (i = 0; i < size; i++) input[i] ^= 0xa5;
        trace_reset(); /* retain the warm D1 slots */
    }
    original_target = target;
    blocks = (offset % EXT2_BLOCK_SIZE + size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    CHECK(blocks <= 70);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    old_size = inode.size;
    for (i = 0; i < blocks; i++)
        CHECK(ext2_bmap(&ctx, &inode, offset / EXT2_BLOCK_SIZE + i, &old_physical[i]) == 0);
    ext2_read_cache_reset();
    if (op == 6) CHECK(ext2_read_stream(&ctx, target, output, size, offset) == (int)size);
    trace_reset();
    kmemcpy(second_disk, g_disk, sizeof(g_disk)); saved_ctx = ctx;
    CHECK(run_operation(op) >= 0);
    count = event_count;
    if (op == 1) CHECK(ext2_lookup(&ctx, "/new", &target) == 0);
    CHECK(ext2_read_inode(&ctx, target, &inode) == 0);
    blocks = (offset % EXT2_BLOCK_SIZE + size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    CHECK(blocks <= 70);
    for (i = 0; i < blocks; i++)
        CHECK(ext2_bmap(&ctx, &inode, offset / EXT2_BLOCK_SIZE + i, &physical[i]) == 0);
    for (i = 0; i < count; i++) {
        expected[i].lba = events[i].lba; expected[i].count = events[i].count;
        expected[i].write = events[i].write; expected[i].data = 0;
        expected[i].prefix = prefix;
        if (!events[i].write) continue;
        for (j = 0; j < blocks; j++) if (events[i].lba == block_lba(physical[j])) break;
        if (j == blocks) continue;
        expected[i].data = 1;
        u32 end = (j + (u32)events[i].count / 2) * EXT2_BLOCK_SIZE - offset % EXT2_BLOCK_SIZE;
        if (end > size) end = size;
        prefix = end;
    }
    CHECK(prefix == size);
    sample(1);
    for (i = 0; i < count; i++) {
        for (j = 0; j <= (u32)expected[i].count; j++) {
            for (mode = 0; mode < 2; mode++) {
                int result;
                target = original_target;
                restore(); fault_at = (int)i; fault_cut = (int)j; persistent = mode;
                if (op == 6) { /* warm D1 on each restored image, before arming fault */
                    fault_at = -1;
                    CHECK(ext2_read_stream(&ctx, target, output, size, offset) == (int)size);
                    trace_reset(); fault_at = (int)i;
                }
                result = run_operation(op);
                CHECK(fault_fired && !ext2_write_work.active);
                if (ctx.fs_error) CHECK(result == EXT2_ERR_IO);
                if (expected[i].data) {
                    /* No later data request, even after some sectors or all of
                     * the failing run were copied. Metadata may commit prefix. */
                    for (k = i + 1; k < event_count; k++) {
                        u32 b;
                        if (!events[k].write) continue;
                        for (b = 0; b < blocks; b++)
                            CHECK(events[k].lba != block_lba(physical[b]));
                    }
                    if (!mode) {
                        CHECK(!ctx.fs_error);
                        if (op == 1 || op == 2 || !expected[i].prefix)
                            CHECK(result == EXT2_ERR_IO);
                        else CHECK(result == (int)expected[i].prefix);
                    }
                }
                fault_at = -1; persistent = 0;
                if (expected[i].data && !ctx.fs_error && op != 1 && op != 2) {
                    u32 wanted = old_size;
                    if (expected[i].prefix && offset + expected[i].prefix > wanted)
                        wanted = offset + expected[i].prefix;
                    CHECK(ext2_read_inode(&ctx, target, &inode) == 0 && inode.size == wanted);
                    for (k = 0; k < blocks; k++) {
                        u32 end = (k + 1) * EXT2_BLOCK_SIZE - offset % EXT2_BLOCK_SIZE;
                        u32 phys;
                        if (end > size) end = size;
                        if (end <= expected[i].prefix) continue;
                        CHECK(ext2_bmap(&ctx, &inode, offset / EXT2_BLOCK_SIZE + k, &phys) == 0);
                        CHECK(phys == old_physical[k]);
                        if (!old_physical[k]) {
                            u32 group = (physical[k] - ctx.sb_info.first_data_block) /
                                        ctx.sb_info.blocks_per_group;
                            u32 bit = (physical[k] - ctx.sb_info.first_data_block) %
                                      ctx.sb_info.blocks_per_group;
                            CHECK(!(media_block(ctx.gd_table[group].block_bitmap)[bit / 8] &
                                    (1u << (bit % 8))));
                        }
                    }
                }
                if (result > 0) read_check(offset, (u32)result);
                same(neighbour, raw_inode(guard), sizeof(neighbour));
                sample(expected[i].data && !ctx.fs_error && op != 1 && op != 2);
            }
        }
    }
    report("fault requests="); report_u(count); report("\n");
}

void start_c(long *sp)
{
    const char *name = ((char **)&sp[1])[1];
    CHECK(sp[0] >= 2);
    if (sp[0] >= 3) dump_dir = ((char **)&sp[1])[2];
    if (!kstrcmp(name, "lengths")) case_lengths();
    else if (!kstrcmp(name, "boundaries")) case_boundaries();
    else if (name[0] == 'f' && name[1] >= '0' && name[1] <= '7') case_run_faults(name[1] - '0');
    else CHECK(0);
    report("PASS\n"); die(0);
}
