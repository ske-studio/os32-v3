/* Product driver, independent port model: a DRQ phase can contain m sectors. */
static void multiple_sizes(void)
{
    static const unsigned maxima[] = { 0, 1, 2, 16, 128 };
    unsigned mi, wr, ci;
    strict_timing = 1;
    for (mi = 0; mi < sizeof(maxima) / sizeof(maxima[0]); mi++) {
        unsigned m = maxima[mi], step = m > 1 ? m : 1;
        unsigned counts[] = { 0, 1, step - 1, step, step + 1, 64, 255, 256, 257 };
        advertised_multiple = m;
        configure(IDE_AMODE_LBA28, 0, 1000);
        for (wr = 0; wr < 2; wr++) for (ci = 0; ci < sizeof(counts) / sizeof(counts[0]); ci++) {
            unsigned n = counts[ci], first = n > 256 ? 256 : n;
            unsigned expected = (first + step - 1) / step + (n > 256);
            reset_io();
            CHECK(request(wr, 19, n) == IDE_OK);
            printf("multiple m=%u %s n=%u commands=%u phases=%u words=%u\n",
                   m, wr ? "write" : "read", n, commands, phases, words);
            CHECK(commands == (n + 255) / 256);
            CHECK(phases == expected && words == n * IDE_PIO_WORDS);
            if (n > step) CHECK(setup_count[0] == (m > 1));
            if (n > step) CHECK(log_opcode[0] == (m > 1 ? (wr ? TEST_WRITE_MULTIPLE : TEST_READ_MULTIPLE) : (wr ? IDE_CMD_WRITE : IDE_CMD_READ)));
            if (!wr) verify_read(19, n);
        }
    }
    advertised_multiple = 0;
}

static void multiple_errors(void)
{
    unsigned wr, pos, kind;
    static const unsigned positions[] = { 0, 16, 32, 256 };
    advertised_multiple = 16;
    strict_timing = 1;
    configure(IDE_AMODE_LBA28, 0, 1000);
    for (wr = 0; wr < 2; wr++) for (pos = 0; pos < 4; pos++)
    for (kind = STUCK_BSY; kind <= END_DRQ; kind++) {
        unsigned p = positions[pos], n = p == 256 ? 257 : 33;
        unsigned k = p == 32 ? 1 : 16;
        unsigned drain = !wr && kind == ERROR_DRQ && p < 256 ? k : 0;
        unsigned i;
        int rc;
        if (kind == END_DRQ && p != 256) continue;
        if (kind == NO_DRQ && p == 256) continue;
        reset_io(); fault_kind = kind; fault_phase = p; fault_command = 1;
        rc = request(wr, 19, n);
        CHECK(rc == ((kind == STUCK_BSY || kind == NO_DRQ || kind == END_DRQ) ? IDE_ERR_TIMEOUT : IDE_ERR_IO));
        CHECK(commands == 1 && words == (p + drain) * IDE_PIO_WORDS);
        CHECK(phases == (p + 15) / 16 + !!drain);
        CHECK(setup_count[0] == 1); /* no retry/reconfiguration for data errors */
        if (wr) {
            for (i = 0; i < MAX_SECTORS; i++) CHECK(written_sectors[i] == (i < p));
        } else {
            for (i = 0; i < p * IDE_PIO_WORDS; i++)
                CHECK(buffer.data[i] == pattern(19 + i / IDE_PIO_WORDS, i % IDE_PIO_WORDS));
            for (; i < MAX_SECTORS * IDE_PIO_WORDS; i++) CHECK(buffer.data[i] == 0xDEAD);
        }
        CHECK(buffer.before == 0xC35A && buffer.after == 0x7E91);
    }
    advertised_multiple = 0;
}

static void multiple_boundaries(void)
{
    unsigned wr, mode, i;
    advertised_multiple = 16;
    for (wr = 0; wr < 2; wr++) {
        configure(IDE_AMODE_LBA28, 0, 1000);
        reset_io(); CHECK(request(wr, 999, 2) == IDE_ERR_RANGE);
        CHECK(!commands && !addr_writes && setup_count[0] == 1);
        for (mode = IDE_AMODE_CHS_CUR; mode <= IDE_AMODE_CHS_DEF; mode++) {
            unsigned spt = mode == IDE_AMODE_CHS_CUR ? 5 : 7;
            configure(mode, 0, 0);
            CHECK(request(wr, spt - 1, spt + 2) == IDE_OK);
            CHECK(commands == 3 && phases == 3);
            CHECK(log_count[0] == 1 && log_count[1] == spt && log_count[2] == 1);
            if (!wr) verify_read(spt - 1, spt + 2);
        }
        configure(IDE_AMODE_CHS_DEF, 0, 0);
        for (i = 0; i < IDE_PIO_WORDS; i++) buffer.data[i] = pattern(41, i);
        CHECK((wr ? ide_write_sector_chs(0, 1, 2, 7, buffer.data)
                  : ide_read_sector_chs(0, 1, 2, 7, buffer.data)) == IDE_OK);
        CHECK(commands == 1 && phases == 1 && log_lba[0] == 41);
        CHECK(log_opcode[0] == (wr ? TEST_WRITE_MULTIPLE : TEST_READ_MULTIPLE));
        reset_io();
        for (i = 0; i < IDE_PIO_WORDS; i++) buffer.data[i] = pattern(41, i);
        CHECK((wr ? ide_write_sector(0, 41, buffer.data)
                  : ide_read_sector(0, 41, buffer.data)) == IDE_OK);
        CHECK(commands == 1 && phases == 1 && log_lba[0] == 41);
        CHECK(log_opcode[0] == (wr ? TEST_WRITE_MULTIPLE : TEST_READ_MULTIPLE));
        configure(IDE_AMODE_CHS_DEF, 1, 1000);
        CHECK(setup_count[0] == 0);
        CHECK(request(wr, 20, 4) == IDE_OK && commands == 4 && phases == 4);
        for (i = 0; i < 4; i++) CHECK(log_opcode[i] == (wr ? IDE_CMD_WRITE : IDE_CMD_READ));
    }
    advertised_multiple = 0;
}

static void multiple_identify(void)
{
    static const u16 currents[] = { 0, 16, 0x100, 0x102, 0x110 };
    unsigned i;
    IdeInfo info;
    advertised_multiple = 16;
    for (i = 0; i < sizeof(currents) / sizeof(currents[0]); i++) {
        unsigned mismatch = currents[i] != 0x110;
        configure(IDE_AMODE_LBA28, 0, 1000);
        identify_words[59] = currents[i];
        device_multiple[0] = (currents[i] & 0x100) ? currents[i] & 0xFF : 0;
        CHECK(ide_identify(0, &info) == IDE_OK);
        CHECK(setup_count[0] == 1 && commands == 0); /* observation only */
        reset_io();
        CHECK(request(0, 19, 33) == IDE_OK);
        CHECK(setup_count[0] == 1 + mismatch && phases == 3);
        verify_read(19, 33);
    }
    configure(IDE_AMODE_LBA28, 0, 1000);
    identify_fail = 1;
    CHECK(ide_identify(0, &info) == IDE_ERR_NO_DRIVE);
    identify_fail = 0;
    reset_io();
    CHECK(request(0, 19, 33) == IDE_OK && phases == 3 && setup_count[0] == 1);
    verify_read(19, 33);
    advertised_multiple = 0;
}

static void multiple_setup_errors(void)
{
    static const unsigned kinds[] = { ERROR, FAULT, STUCK_BSY, ERROR_DRQ, END_DRQ };
    unsigned i, wr;
    advertised_multiple = 16;
    for (wr = 0; wr < 2; wr++) for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        unsigned kind = kinds[i], clean = kind == ERROR || kind == FAULT;
        configure(IDE_AMODE_LBA28, 0, 1000);
        ide_multiple_reset_bank(0);
        device_multiple[0] = 0;
        reset_io(); setup_fault = kind;
        CHECK(request(wr, 19, 33) == (clean ? IDE_OK : kind == ERROR_DRQ ? IDE_ERR_IO : IDE_ERR_TIMEOUT));
        CHECK(setup_count[0] == 2);
        CHECK(commands == clean && phases == (clean ? 33 : 0));
        if (clean) CHECK(log_opcode[0] == (wr ? IDE_CMD_WRITE : IDE_CMD_READ));
        else {
            CHECK(words == 0);
            /* Residual BSY/DRQ blocks the next request as well. */
            CHECK(request(wr, 19, 33) < 0);
            CHECK(commands == 0 && setup_count[0] == 2);
        }
        /* Once hardware is idle, use legacy commands without repeating C6h. */
        reset_io(); setup_fault = NORMAL;
        CHECK(request(wr, 19, 33) == IDE_OK);
        CHECK(phases == 33 && setup_count[0] == 2);
        CHECK(log_opcode[0] == (wr ? IDE_CMD_WRITE : IDE_CMD_READ));
        if (!wr) verify_read(19, 33);
    }
    /* Rejection at boot also leaves an ordinary drive usable, without retry. */
    setup_fault = ERROR;
    configure(IDE_AMODE_LBA28, 0, 1000);
    setup_fault = NORMAL;
    CHECK(setup_count[0] == 1 && request(0, 19, 33) == IDE_OK && phases == 33);
    CHECK(setup_count[0] == 1);
    advertised_multiple = 0;
}

static void multiple_lifecycle(void)
{
    unsigned d, wr;
    IdeGeom geom;
    present_mask = 15; individual_caps = 1;
    advertised_per_drive[0] = 2; advertised_per_drive[1] = 16;
    advertised_per_drive[2] = 128; advertised_per_drive[3] = 16;
    configure(IDE_AMODE_LBA28, 0, 1000);
    for (wr = 0; wr < 2; wr++) for (d = 0; d < 4; d++) {
        unsigned m = advertised_per_drive[d];
        reset_io();
        CHECK(request_drive(d, wr, 19, 33) == IDE_OK);
        CHECK(phases == (33 + m - 1) / m && setup_count[d] == 1);
        if (!wr) verify_read(19, 33);
    }
    reset_io();
    ide_multiple_reset_bank(1);
    device_multiple[2] = device_multiple[3] = 0;
    /* Empty and invalid requests must not issue even a pending C6h. */
    CHECK(request_drive(2, 0, 19, 0) == IDE_OK && setup_count[2] == 1);
    CHECK(request_drive(2, 1, 999, 2) == IDE_ERR_RANGE && setup_count[2] == 1);
    for (d = 0; d < 4; d++) {
        reset_io();
        CHECK(request_drive(d, 0, 19, 33) == IDE_OK);
        CHECK(setup_count[d] == (d < 2 ? 1 : 2));
        CHECK(phases == (33 + advertised_per_drive[d] - 1) / advertised_per_drive[d]);
        verify_read(19, 33);
    }
    /* Reinitialization loses no reset and forgets removed devices/settings. */
    reset_io();
    CHECK(ide_init() == 4);
    for (d = 0; d < 4; d++) CHECK(setup_count[d] == (d < 2 ? 2 : 3));
    reset_io();
    memset(device_multiple, 0, sizeof(device_multiple));
    CHECK(ide_init() == 4);
    for (d = 0; d < 4; d++) CHECK(setup_count[d] == (d < 2 ? 3 : 4));
    reset_io();
    present_mask = 1; advertised_per_drive[0] = 0;
    CHECK(ide_init() == 1);
    CHECK(!ide_drive_present(2) && ide_get_geom(2, &geom) == IDE_ERR_NO_DRIVE);
    reset_io();
    CHECK(request(0, 19, 33) == IDE_OK && phases == 33);
    CHECK(log_opcode[0] == IDE_CMD_READ);
    individual_caps = 0; advertised_multiple = 0;
}
