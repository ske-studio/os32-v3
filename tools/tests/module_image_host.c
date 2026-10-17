/* ILP32 stdin fixture, no libc; protocol is private to test_module_image.py. */
#include "module_image.h"

static u8 image[1024 * 1024];
static u8 before[1024 * 1024];
static struct module_symbol_contract imports[MODULE_IMPORT_MAX];
static struct module_symbol_contract exports[MODULE_OPS_SLOTS];

static int read_exact(void *buf, u32 len)
{
    u8 *p = buf;
    while (len) {
        int rc;
        __asm__ volatile("int $0x80" : "=a"(rc)
                         : "0"(3), "b"(0), "c"(p), "d"(len) : "memory");
        if (rc <= 0) return 0;
        p += rc; len -= (u32)rc;
    }
    return 1;
}

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}

static void check_unchanged(const void *bytes, u32 size, u8 value)
{
    const u8 *p = bytes;
    for (u32 i = 0; i < size; i++) if (p[i] != value) finish(2);
}

void _start(void)
{
    u32 input[11];
    if (!read_exact(input, sizeof(input)) || input[6] > MODULE_IMPORT_MAX ||
        input[7] > sizeof(image)) finish(2);
    struct module_image_policy policy = {
        input[0], input[1], input[2], input[3], input[4], input[5], input[6], imports, exports,
        input[8]
    };
    if (!read_exact(imports, input[6] * sizeof(imports[0])) ||
        !read_exact(exports, sizeof(exports)) || !read_exact(image, input[7])) finish(2);
    for (u32 i = input[7]; i < sizeof(image); i++) image[i] = 0xa5;
    /* A boundary mutant must reach the explicit write/range observation rather
     * than being accidentally rejected by the value read across payload end. */
    if (input[7] <= sizeof(image) - 4)
        for (u32 i = 0; i < 4; i++) image[input[7] + i] = 0;
    for (u32 i = 0; i < sizeof(image); i++) before[i] = image[i];
    struct module_image_plan plan, header_plan;
    u8 *plan_bytes = (u8 *)&plan;
    for (u32 i = 0; i < sizeof(plan); i++) plan_bytes[i] = 0xa5;
    for (u32 i = 0; i < sizeof(header_plan); i++) ((u8 *)&header_plan)[i] = 0xa5;
    /* Only the fixed-size header is available to the early allocation check. */
    u8 header[MODULE_HEADER_SIZE];
    u32 header_bytes = input[7] < sizeof(header) ? input[7] : sizeof(header);
    for (u32 i = 0; i < header_bytes; i++) header[i] = image[i];
    int header_rc = module_image_header(header, header_bytes, input[7], &policy, &header_plan);
    for (u32 i = 0; i < header_bytes; i++) if (header[i] != image[i]) finish(2);
    if (header_rc) check_unchanged(&header_plan, sizeof(header_plan), 0xa5);
    if (module_image_header(header, header_bytes, input[7], &policy, 0) != header_rc) finish(2);
    int rc = module_image_validate(image, input[7], &policy, &plan);
    if (module_image_validate(image, input[7], &policy, 0) != rc) finish(2);
    for (u32 i = 0; i < input[7]; i++) if (before[i] != image[i]) finish(2);
    if (rc) {
        check_unchanged(&plan, sizeof(plan), 0xa5);
    } else {
        if (header_rc) finish(2);
    }
    if (!header_rc) {
        for (u32 i = 0; i < MI_WORDS; i++) {
            u32 at = i * 4;
            u32 want = (u32)image[at] | (u32)image[at + 1] << 8 |
                       (u32)image[at + 2] << 16 | (u32)image[at + 3] << 24;
            if (header_plan.word[i] != want || (!rc && plan.word[i] != want)) finish(2);
        }
        u32 want = header_plan.word[MI_PAYLOAD_OFF] + header_plan.word[MI_MEM_SIZE];
        if (header_plan.load_span != want || (!rc && plan.load_span != want)) finish(2);
    }
    if (input[9]) {
        for (u32 i = 0; i < sizeof(plan); i++) plan_bytes[i] = 0xa5;
        rc = module_image_relocate(image, input[7], &policy, input[10], &plan);
        if (rc) {
            check_unchanged(&plan, sizeof(plan), 0xa5);
        } else {
            const u32 *h = plan.word;
            u8 *body = before + h[MI_PAYLOAD_OFF];
            for (u32 i = h[MI_PAYLOAD_SIZE]; i < h[MI_MEM_SIZE]; i++) body[i] = 0;
            for (u32 i = 0; i < h[MI_RELOC_COUNT]; i++) {
                const u8 *p = before + h[MI_RELOC_OFF] + 4 * i;
                u32 off = p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
                p = body + off;
                u32 value = (p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24) + input[10];
                for (u32 j = 0; j < 4; j++) body[off + j] = (u8)(value >> (8 * j));
            }
        }
        /* Includes prefix, non-target payload, BSS, padding and tail guards.
         * Any rejected image must have produced literally zero writes. */
        for (u32 i = 0; i < sizeof(image); i++) if (before[i] != image[i]) finish(2);
    }
    char msg[] = "RED 00\n";
    const char *output = rc ? msg : "GREEN\n";
    msg[4] = '0' + rc / 10; msg[5] = '0' + rc % 10;
    int ignored;
    __asm__ volatile("int $0x80" : "=a"(ignored)
                     : "0"(4), "b"(1), "c"(output), "d"(rc ? 7 : 6) : "memory");
    finish(rc ? 1 : 0);
}
