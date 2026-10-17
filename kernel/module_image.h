/* KMOD v1 validation; shared by the POST_ROOT loader and host tests. */
#ifndef MODULE_IMAGE_H
#define MODULE_IMAGE_H

#include "types.h"
#include "module_schema.h"

enum module_image_result {
    MODULE_IMAGE_OK = 0,
    MODULE_BAD_HEADER = 3,
    MODULE_PAIR_MISMATCH = 4,
    MODULE_NO_IMAGE_CRC = 5,
    MODULE_ABI_MISMATCH = 6,
    MODULE_BAD_CRC = 9,
    MODULE_RELOC_INVALID = 11,
    MODULE_IMPORT_INVALID = 12
};

/* Trusted, generated contracts. Export array index = ops slot minus one;
 * a zero version marks an unavailable export slot. Names use FNV-1a. */
struct module_symbol_contract {
    u32 name_hash;
    u16 version;
    u16 kind;
};
/* ID-specific format limits, generated from tools/module_schema.py. No row is
 * admitted merely because an image declares that ID. The caller chooses it. */
struct module_component_policy {
    u32 module_id, phase, required_flags, allowed_flags, stop_allowed;
    u32 ops_version, ops_slots, image_max, mem_max;
    u32 reloc_max, import_max, export_max;
};
struct module_image_policy {
    u32 kapi_abi_gen, layout_gen, module_abi_gen, pair_image_crc;
    u32 ops_version, img_valid, import_count;
    const struct module_symbol_contract *imports;
    const struct module_symbol_contract *exports; /* component's ops_slots entries */
    u32 module_id; /* expected ID from the caller, never from the image */
};
struct module_image_plan {
    u32 word[MI_WORDS];
    u32 load_span;
};

/* Output is written only on success. Neither function modifies input, allocates,
 * resolves addresses, or invokes module code. The header function reads exactly
 * MODULE_HEADER_SIZE bytes, even when file_size describes a larger file. */
int module_image_header(const u8 *header, u32 header_bytes, u32 file_size,
                        const struct module_image_policy *policy,
                        struct module_image_plan *out);
int module_image_validate(const u8 *image, u32 size,
                          const struct module_image_policy *policy,
                          struct module_image_plan *out);

#endif
