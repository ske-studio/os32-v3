/* バッファの先頭を奇数番地にし、偶数・奇数 DRQ と余剰データをガードで確認。 */
static void t_pio_bounds(void)
{
    static const u32 chunks[] = { 8, 7, 2, 3 };
    static const u32 sizes[] = { 0, 3, 8, 9 };
    u8 cdb[ATAPI_CDB_BYTES] = { SCSI_CMD_READ_CAPACITY };
    u16 storage[8];
    u8 *bytes = (u8 *)storage;
    setup();
    atapi_select_bank(1);
    M.cap_len = 9;
    for (unsigned j = 0; j < sizeof(chunks) / sizeof(chunks[0]); j++) {
        M.drq_max = chunks[j];
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            u32 actual = 0, size = sizes[i];
            memset(storage, 0xCD, sizeof(storage));
            CHECK(atapi_packet_read(cdb, bytes + 1, size, &actual) == ATAPI_OK);
            CHECK(actual == 9);
            CHECK(memcmp(bytes + 1, M.cap, size) == 0);
            CHECK(bytes[0] == 0xCD);
            for (unsigned k = size + 1; k < sizeof(storage); k++) CHECK(bytes[k] == 0xCD);
        }
    }
}

/* Check the port word stream, CDB, actual, diagnostics and every guard byte.
 * The same cases also run against the L2 sources for the move comparison. */
static void t_pio_trace(void)
{
    static const u32 lengths[] = {1, 7, 8, 9, SEC, 16 * SEC};
    static const u32 capacities[] = {0, 1, 7, 8, 9, SEC, 16 * SEC};
    static const u32 chunks[] = {0, 7, 8, 9};
    u8 *storage = malloc(16 * SEC + 4);
    CHECK(storage);
    setup(); atapi_select_bank(1);
    for (unsigned l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
        for (unsigned k = 0; k < sizeof(chunks) / sizeof(chunks[0]); k++) {
            for (unsigned z = 0; z < sizeof(capacities) / sizeof(capacities[0]); z++) {
                u8 cdb[ATAPI_CDB_BYTES] = {SCSI_CMD_READ_10};
                cdb[5] = BIG_LBA_A; cdb[8] = 16;
                /* Use READ CAPACITY for small response lengths. */
                if (lengths[l] < SEC) { cdb[0] = SCSI_CMD_READ_CAPACITY; M.cap_len = lengths[l]; }
                else cdb[8] = (u8)(lengths[l] / SEC);
                M.drq_max = chunks[k];
                u32 size = capacities[z], actual = U32_MAX;
                memset(storage, 0xCD, 16 * SEC + 4);
                g_pio_n = g_pio_out = 0;
                CHECK(atapi_packet_read(cdb, storage + 1, size, &actual) == ATAPI_OK);
                CHECK(actual == lengths[l] && s_diag_got == lengths[l]);
                CHECK(!(s_diag_st & IDE_ST_ERR) && s_diag_err == 0);
                CHECK(g_pio_out == 6);
                CHECK(memcmp(M.cdb, cdb, ATAPI_CDB_BYTES) == 0);
                for (u32 i = 0; i < 6; i++)
                    CHECK(g_pio_cdb[i] == (u16)(cdb[2*i] | (u16)cdb[2*i+1] << 8));
                const u8 *src = lengths[l] < SEC ? M.cap : g_media_a.img + BIG_LBA_A * SEC;
                u32 copied = size < lengths[l] ? size : lengths[l];
                CHECK(memcmp(storage + 1, src, copied) == 0);
                CHECK(storage[0] == 0xCD);
                for (u32 i = copied + 1; i < 16 * SEC + 4; i++) CHECK(storage[i] == 0xCD);
                u32 pos = 0, wi = 0;
                while (pos < lengths[l]) {
                    u32 n = M.bcl;
                    if (chunks[k] && chunks[k] < n) n = chunks[k];
                    if (n > lengths[l] - pos) n = lengths[l] - pos;
                    for (u32 i = 0; i < n; i += 2) {
                        u16 w = src[pos+i];
                        if (i + 1 < n) w |= (u16)src[pos+i+1] << 8;
                        CHECK(g_pio_words[wi++] == w);
                    }
                    pos += n;
                }
                CHECK(g_pio_n == wi);
            }
        }
    }
    /* A data error after a short first DRQ preserves partial diagnostics and
     * leaves actual at zero, exactly as the L2 packet entry does. */
    u8 cdb[ATAPI_CDB_BYTES] = { SCSI_CMD_READ_CAPACITY };
    u32 actual = 99;
    M.cap_len = 9; M.drq_max = 7; M.fail_after_block = 1;
    memset(storage, 0xCD, 16 * SEC + 4);
    CHECK(atapi_packet_read(cdb, storage + 1, 9, &actual) == ATAPI_ERR_IO);
    CHECK(actual == 0 && s_diag_got == 7);
    CHECK((s_diag_st & IDE_ST_ERR) && s_diag_err == (TEST_MEDIUM_ERROR << 4));
    CHECK(memcmp(storage + 1, M.cap, 7) == 0);
    CHECK(storage[0] == 0xCD && storage[8] == 0xCD);
    free(storage);
    puts("pio_trace: L2 CDB/word stream/actual/diagnostics/guards PASS");
}
