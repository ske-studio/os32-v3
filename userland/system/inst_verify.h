#ifndef OS32_INST_VERIFY_H
#define OS32_INST_VERIFY_H

#include "lib/crc32_core.inc"

/* Reopen the completed file and check its contents before boot deployment. */
static int inst_verify_file(KernelAPI *api, const char *path, u32 expected, u32 size)
{
    u8 buf[2048];
    u32 crc = CRC32_INIT, pos = 0;
    int fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return -1;
    while (pos < size) {
        u32 n = size-pos < sizeof(buf) ? size-pos : sizeof(buf);
        int got = api->sys_read(fd, buf, n);
        if (got <= 0 || (u32)got > n) break;
        crc = crc32_core_update(crc, buf, got);
        pos += got;
    }
    int ok = pos == size && crc32_core_final(crc) == crc32_core_final(expected) &&
             api->sys_read(fd, buf, 1) == 0;
    api->sys_close(fd);
    return ok ? 0 : -1;
}

#endif
