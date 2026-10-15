/* KMOD v1 validation; shared by the POST_ROOT loader and host tests. */
#ifndef MODULE_IMAGE_H
#define MODULE_IMAGE_H

#include "types.h"

#define MODULE_HEADER_SIZE 144U
#define MODULE_PAGE_SIZE 4096U
#define MODULE_IMAGE_MAX 262144U
#define MODULE_RELOC_MAX 16384U
#define MODULE_IMPORT_MAX 1024U
#define MODULE_EXPORT_MAX 64U
#define MODULE_OPS_SLOTS 18U
#define MODULE_NAME_MAX 63U
#define MODULE_RECORD_SIZE 12U
#define MODULE_MAGIC 0x444f4d4bUL
#define MODULE_FORMAT_VERSION 1U
#define MODULE_ID_KDEBUG 1U
#define MODULE_PHASE_POST_ROOT 2U
#define MODULE_FLAG_PAIR 1U

/* Wire word numbers, not a structure cast of untrusted bytes. */
enum module_field {
    MI_MAGIC, MI_FORMAT, MI_HEADER_SIZE, MI_IMAGE_SIZE,
    MI_KAPI, MI_LAYOUT, MI_ABI, MI_ID, MI_PHASE, MI_FLAGS,
    MI_MEM_SIZE, MI_PAYLOAD_OFF, MI_PAYLOAD_SIZE,
    MI_TEXT_OFF, MI_TEXT_SIZE, MI_RO_OFF, MI_RO_SIZE,
    MI_DATA_OFF, MI_DATA_SIZE, MI_BSS_OFF, MI_BSS_SIZE,
    MI_RELOC_OFF, MI_RELOC_COUNT, MI_IMPORT_OFF, MI_IMPORT_COUNT,
    MI_EXPORT_OFF, MI_EXPORT_COUNT, MI_NAMES_OFF, MI_NAMES_SIZE,
    MI_INIT, MI_STOP, MI_PAIR_CRC, MI_IMAGE_CRC, MI_OPS_VERSION,
    MI_RESERVED0, MI_RESERVED1, MI_WORDS
};

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
struct module_image_policy {
    u32 kapi_abi_gen, layout_gen, module_abi_gen, pair_image_crc;
    u32 ops_version, img_valid, import_count;
    const struct module_symbol_contract *imports;
    const struct module_symbol_contract *exports; /* MODULE_OPS_SLOTS entries */
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
