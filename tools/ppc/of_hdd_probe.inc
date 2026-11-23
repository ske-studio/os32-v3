    if (argc == 2) {
        char value[9] = {0};
        if (!strcmp(argv[1], "save")) {
            int fd = g_api->sys_open("/etc/of-value", KAPI_O_CREAT | KAPI_O_WRONLY);
            g_api->kprintf(7, "save open=%d\n", fd);
            if (fd < 0) return 1;
            int written = g_api->sys_write(fd, "selected", 8);
            g_api->kprintf(7, "save write=%d\n", written);
            g_api->sys_close(fd);
            if (written != 8) return 1;
            g_api->ime_set_mode(IME_MODE_HIRAGANA);
            const char *roman = "neko";
            while (*roman) g_api->ime_feed_key(*roman++);
            g_api->ime_feed_key((0x34 << 8) | ' ');
            int commit = g_api->ime_feed_key((0x1c << 8) | '\r');
            while (g_api->ime_feed_key(-1) >= 0) {}
            g_api->ime_set_mode(IME_MODE_OFF);
            int sync = g_api->vfs_sync();
            g_api->kprintf(7, "learn commit=%d sync=%d\n", commit, sync);
            if (sync != 0) return 1;
        }
        int fd = g_api->sys_open("/etc/of-value", KAPI_O_RDONLY);
        if (fd < 0 || g_api->sys_read(fd, value, 8) != 8) return 1;
        g_api->sys_close(fd);
        int learned = g_api->exec_run("/bin/learnprb.bin");
        g_api->kprintf(7, "root=%s child=%d value=%s probe=%d\n",
            g_api->vfs_devname("/"), g_api->sys_is_mounted("/hd0"), value, learned);
        return 0;
    }
