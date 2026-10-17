/* T4b-8b PM fixture. Negative cases intentionally fault-kill this process. */
#include "os32api.h"
#include "os32_kapi_slots.h"
#include <string.h>

/* A fixed argument frame can itself end at a mapped page boundary. */
extern u32 string_at(u32 slot, const u32 *frame);
__asm__(".text\n"
        ".globl string_at\n"
        "string_at:\n"
        "push %ebx\n"
        "mov 8(%esp), %eax\n"
        "mov 12(%esp), %edx\n"
        "mov %esp, %ebx\n"
        "mov %edx, %esp\n"
        "int $0x80\n"
        "mov %ebx, %esp\n"
        "pop %ebx\n"
        "ret\n");

int main(int argc, char **argv, KernelAPI *api)
{
    const char *mode = argc > 1 ? argv[1] : "normal";
    char *pages = api->mem_map(OS32_PAGE_SIZE * 2, 0, 0);
    if (!pages) return 1;
    char *end = pages + OS32_PAGE_SIZE;
    if (api->mem_unmap(end, OS32_PAGE_SIZE)) return 1;
    char *edge = end - 4;

    if (!strcmp(mode, "normal")) {
        memcpy(edge, "abc", 4);
        int fd = api->sys_open(edge, 0);
        if (fd >= 0) api->sys_close(fd);
        if (api->sys_is_mounted(edge)) return 1;
        if (api->dev_blk_read(edge, 0, 0, 0) >= 0) return 1;
        /* No write, mount or real boot in the successful guest smoke case. */
        api->serial_puts("KSTR serial UTF-8: \xe3\x81\x82\n");
        api->shell_print_utf8("KSTR console UTF-8: \xe3\x81\x82\n", 7);
        api->shell_print_utf8(edge, 7);
        api->shell_print_utf8("\n", 7);
        api->fm_play_mml(end - 1); /* empty MML, page-final NUL */
        api->snd_bgm_play(end - 1);
        api->snd_bgm_stop();
        /* Exact fixed-word copy: return address + two arguments, no padding. */
        u32 *frame = (u32 *)(end - 3 * sizeof(u32));
        frame[0] = 0; frame[1] = (u32)"KSTR fixed edge\n"; frame[2] = 7;
        string_at(KAPI_SLOT_SHELL_PRINT_UTF8, frame);
        if (api->exec_run("__t4b8b_missing_command__") >= 0) return 1;
        if (api->mem_unmap(pages, OS32_PAGE_SIZE)) return 1;
        api->kprintf(7, "kstr_boundary: PASS 1/1\n");
        return 0;
    }
    if (!strcmp(mode, "stop")) {
        /* Run under the GUI launcher; press CTRL+STOP during playback. */
        api->kprintf(7, "KSTR STOP armed\n");
        api->fm_play_mml("C1D1E1F1G1A1B1C1D1E1F1G1A1B1");
        api->kprintf(7, "KSTR STOP completed\n");
        return 0;
    }

    api->kprintf(7, "KSTR armed %s; expect FAULT before target\n", mode);
    char *bad = end - 16;
    memset(bad, '!', 16);
    memcpy(bad, "KSTR-PARTIAL", 12);
    if (!strcmp(mode, "path")) api->sys_open(bad, 0);
    else if (!strcmp(mode, "ls")) {
        OS32_LsPacket packet;
        api->sys_ls_window(bad, 0, &packet);
    }
    else if (!strcmp(mode, "rename")) api->sys_rename("/__t4b8b_missing__", bad);
    else if (!strcmp(mode, "mount")) api->sys_mount("/__t4b8b__", "__missing__", bad);
    else if (!strcmp(mode, "mount-dev")) api->sys_mount("/__t4b8b__", bad, "fat12");
    else if (!strcmp(mode, "device")) api->dev_blk_read(bad, 0, 0, 0);
    else if (!strcmp(mode, "command")) api->exec_run(bad);
    else if (!strcmp(mode, "launch")) api->launch_req(bad);
    else if (!strcmp(mode, "output")) api->shell_print_utf8(bad, 7);
    else if (!strcmp(mode, "serial")) api->serial_puts(bad);
    else if (!strcmp(mode, "fm")) api->fm_play_mml(bad);
    else if (!strcmp(mode, "bgm")) api->snd_bgm_play(bad);
    else if (!strcmp(mode, "unterminated")) {
        memset(pages, 'x', OS32_PAGE_SIZE);
        api->shell_print_utf8(pages, 7);
    } else if (!strcmp(mode, "word")) {
        u32 *frame = (u32 *)(end - 2 * sizeof(u32));
        frame[0] = 0; frame[1] = (u32)"KSTR-PARTIAL";
        string_at(KAPI_SLOT_SHELL_PRINT_UTF8, frame); /* attr is unmapped */
    } else return 1;
    api->kprintf(7, "kstr_boundary: FAIL survived\n");
    return 1;
}
