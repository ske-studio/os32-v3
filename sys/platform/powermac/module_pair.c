#include "platform_module_pair.h"
#include "bootinfo.h"
#include "endian_le.h"
#include "crc32_core.inc"

extern const u8 ppc_kernel_entry[], __kernel_image_start[], __kernel_file_end[];
extern const u8 __kernel_load_mem_end[], __root_image_start[], __root_file_end[];
extern const u8 __root_load_end[], __root_record_start[], __root_record_end[];
static u32 verified_pair;
static int entry_valid;

static u32 zeros(u32 state, u32 length)
{
    static const u8 pad[64] = {0};
    while (length) {
        u32 chunk = length < sizeof(pad) ? length : sizeof(pad);
        state = crc32_core_update(state, pad, chunk);
        length -= chunk;
    }
    return state;
}

int pm_verify_entry_pair(void)
{
    u32 low = (u32)__kernel_image_start, high = (u32)__root_image_start;
    u32 frame = (u32)__root_file_end - high;
    u32 record = (u32)__root_record_start;
    u32 record_size = (u32)__root_record_end - record;
    const u32 fields[] = {
        (u32)ppc_kernel_entry, low, (u32)__kernel_file_end - low,
        (u32)__kernel_load_mem_end - low, high, frame, (u32)__root_load_end - high,
        high, frame, record, record_size
    };
    u32 state = CRC32_INIT;
    for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        u8 word[4];
        le32_wr(word, fields[i]);
        state = crc32_core_update(state, word, sizeof(word));
    }
    state = crc32_core_update(state, __kernel_image_start, record - low);
    state = zeros(state, record_size);
    state = crc32_core_update(state, __root_record_end,
                              (u32)__kernel_file_end - (u32)__root_record_end);
    verified_pair = crc32_core_final(zeros(state, frame));
    entry_valid = verified_pair == le32_rd(__root_record_start);
    return entry_valid;
}

int platform_module_pair(u32 *out)
{
    const struct boot_common *boot = boot_common_get();
    if (!entry_valid || !boot->image_valid ||
        boot->image_crc != le32_rd(__root_record_start + 4) ||
        boot->image_size != le32_rd(__root_record_start + 12)) return 0;
    *out = verified_pair;
    return 1;
}
