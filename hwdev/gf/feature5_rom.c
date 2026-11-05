/* F5 resident ROM and prepared memory script ownership. */
#include "feature5.h"
#include "internal.h"
#define GF_ROM_BIT_ONLY
#include "rom.inc"

/* nouveau nvkm/subdev/fb/ramnv40.c:154-156: BIT M + 0 is a LE16
 * memory reset script offset. The full ROM preserves every referenced offset. */
#define GF5_M_SCRIPT_BYTES 2U

static i32 rom_error(struct gf5 *g, i32 rc, i32 source)
{
    g->source_rc = source;
    g->last_rc = rc;
    return rc;
}

i32 gf5_rom_acquire(struct gf5 *g, const u8 *rom, u32 size,
                    const struct gf_init_program *program)
{
    u32 data = 0, length = 0, pc = 0;
    u8 version = 0;
    void *bytes = 0, *copy = 0;
    i32 rc;
    if (!g) return GF5_EINVAL;
    if (!rom || !size || !g->links.alloc || !g->links.free)
        return rom_error(g, GF5_EINVAL, GF5_EINVAL);
    if (g->rom.bytes || g->rom.memory_program)
        return rom_error(g, GF5_EBUSY, GF5_EBUSY);
    rc = gf_rom_bit_entry(rom, size, 'M', &data, &length, &version);
    if (rc < 0 || (!rc && length < GF5_M_SCRIPT_BYTES))
        return rom_error(g, GF5_EBIOS, GF5_EBIOS);
    if (!rc) pc = (u32)rom[data] | (u32)rom[data + 1U] << 8;
    if ((pc != 0) != (program != 0) || (pc && pc >= size))
        return rom_error(g, GF5_EBIOS, GF5_EBIOS);
    rc = g->links.alloc(g->links.owner, size, &bytes);
    if (rc) return rom_error(g, gf5_rc_map(rc), rc);
    if (program) {
        rc = g->links.alloc(g->links.owner, sizeof(*program), &copy);
        if (rc) {
            g->links.free(g->links.owner, bytes);
            return rom_error(g, gf5_rc_map(rc), rc);
        }
    }
    for (u32 i = 0; i < size; ++i) ((u8 *)bytes)[i] = rom[i];
    if (program) *(struct gf_init_program *)copy = *program;
    g->rom = (struct gf5_rom_view){bytes, size, copy, pc};
    return GF5_OK;
}

void gf5_rom_release(struct gf5 *g)
{
    if (!g) return;
    if (g->rom.memory_program)
        g->links.free(g->links.owner, (void *)g->rom.memory_program);
    if (g->rom.bytes) g->links.free(g->links.owner, (void *)g->rom.bytes);
    g->rom = (struct gf5_rom_view){0};
}
