    if (argc == 2) {
        char value[9] = {0};
        if (!strcmp(argv[1], "save")) {
            int fd = g_api->sys_open("/etc/of-value", KAPI_O_CREAT | KAPI_O_WRONLY);
            if (fd < 0 || g_api->sys_write(fd, "selected", 8) != 8) return 1;
            g_api->sys_close(fd);
            g_api->ime_set_mode(IME_MODE_HIRAGANA);
            const char *roman = "neko";
            while (*roman) g_api->ime_feed_key(*roman++);
            g_api->ime_feed_key((0x34 << 8) | ' ');
            g_api->ime_feed_key((0x1c << 8) | '\r');
            while (g_api->ime_feed_key(-1) >= 0) {}
            g_api->ime_set_mode(IME_MODE_OFF);
            if (g_api->vfs_sync() != 0) return 1;
        }
        int fd = g_api->sys_open("/etc/of-value", KAPI_O_RDONLY);
        if (fd < 0 || g_api->sys_read(fd, value, 8) != 8) return 1;
        g_api->sys_close(fd);
        IME_UserEntry rows[4];
        int n = g_api->ime_user_list("ねこ", rows, 4);
        int freq = 0;
        for (int i = 0; i < n; ++i)
            if (!strcmp(rows[i].kanji, "猫")) freq = rows[i].freq;
        g_api->kprintf(7, "root=%s child=%d value=%s learned=%d\n",
            g_api->vfs_devname("/"), g_api->sys_is_mounted("/hd0"), value, freq);
        return 0;
    }
