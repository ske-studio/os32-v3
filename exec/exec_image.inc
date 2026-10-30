static int exec_load_image(AppSlot *ctx, const char *path, const OS32Header *hdr)
{
    /* Read directly into physical backing under the master PD. */
    int fd = vfs_open(path, 0);
    u32 pos = 0;
    if (fd < 0) return EXEC_ERR_NOT_FOUND;
    if (vfs_seek(fd, (int)hdr->header_size, 0) < 0) {
        vfs_close(fd);
        return EXEC_ERR_INVALID;
    }
    while (pos < hdr->text_size) {
        u32 pa, n = PAGE_SIZE - ((ctx->load_addr + pos) & (PAGE_SIZE - 1));
        if (n > hdr->text_size - pos) n = hdr->text_size - pos;
        if (ctx->cpl3) {
            if (as_va_to_pa(ctx->as->pd_phys, ctx->load_addr + pos, &pa)) break;
        } else pa = ctx->load_addr + pos;
        if (vfs_read_fd(fd, P2V(pa), n) != (int)n) break;
        pos += n;
    }
    vfs_close(fd);
    if (pos != hdr->text_size) return EXEC_ERR_INVALID;
    /* app_map_region zeroes all pages, including BSS and padding. */
    if (!ctx->cpl3) kmemset((u8 *)ctx->load_addr + hdr->text_size, 0, hdr->bss_size);
    return 0;
}
