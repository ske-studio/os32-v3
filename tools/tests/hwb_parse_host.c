/* libc-free ILP32: source is included, backend is the a0 fake only. */
#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
static struct hwb_program program;
static struct hwb_state state;
static struct hwb_fake fake;
static char buffer[HWB_FILE_BYTES + 1U];
static u32 length(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void parse_ok(const char *s)
{
    u32 line = 999;
    CHECK(hwb_parse(&program, s, length(s), &line) == HWB_OK);
    CHECK(line == 0);
}
static void parse_bad(const char *s, u32 expected)
{
    u32 line = 999;
    CHECK(hwb_parse(&program, s, length(s), &line) == HWB_ARG);
    CHECK(line == expected);
}
static void eval_case(const char *s, enum hwb_rc rc, u32 expected)
{
    program.source = s; program.source_bytes = length(s);
    state.program = &program;
    u32 value = 0x13579BDFUL; /* Failure-output sentinel, not a hardware value. */
    CHECK(hwb_eval(&state, (struct hwb_span){0, length(s)}, &value) == rc);
    CHECK(value == (rc == HWB_OK ? expected : 0x13579BDFUL));
    CHECK(fake.event_count == 0);
}
static void expressions(void)
{
    HWB_TEST("expressions");
    hwbf_init(&fake); state.backend = &fake.backend;
    eval_case("0", HWB_OK, 0);
    eval_case("4294967295", HWB_OK, U32_MAX);
    eval_case("0xFFFFFFFF", HWB_OK, U32_MAX);
    eval_case("2 + 3 << 2 & 31 ^ 1 | 32", HWB_OK, 53);
    eval_case("32 >> 1 + 2", HWB_OK, 4);
    eval_case("9 - 3 - 2", HWB_OK, 4);
    eval_case("(1 | 2) + 4", HWB_OK, 7);
    eval_case("0xffffffff << 1", HWB_OK, 0xfffffffeUL);
    eval_case("1 << 31", HWB_OK, 0x80000000UL);
    eval_case("17 >> 0", HWB_OK, 17);
    HWB_TEST("shift");
    eval_case("1 << 32", HWB_ARG, 0);
    eval_case("1 >> 33", HWB_ARG, 0);
    HWB_TEST("overflow");
    eval_case("4294967295 + 1", HWB_ARG, 0);
    eval_case("0x100000000", HWB_ARG, 0);
    eval_case("4294967296", HWB_ARG, 0);
    eval_case("0 - 1", HWB_ARG, 0);
    eval_case("-1", HWB_ARG, 0);
    HWB_TEST("variables");
    eval_case("missing", HWB_ARG, 0);
    state.var_count = 1;
    state.vars[0] = (struct hwb_variable){.name="x", .kind=HWB_SCALAR, .value=7};
    eval_case("x + 1", HWB_OK, 8);
    state.vars[0].kind = HWB_DEVICE;
    eval_case("x", HWB_ARG, 0);
    state.vars[0].kind = HWB_WINDOW_REF;
    eval_case("x", HWB_ARG, 0);
    state.var_count = 0;
    HWB_TEST("bad-expressions");
    const char *bad[] = {"", "()", "1 +", "1 * 2", "1 / 2", "1.0", "0x", "(1", "1)",
                         "1 2", "1 < 2", "1 && 2", "1 || 2", "~1", "1 << -1"};
    for (u32 k = 0; k < sizeof(bad)/sizeof(bad[0]); k++) eval_case(bad[k], HWB_ARG, 0);
    u32 v = 123;
    CHECK(hwb_eval(&state, (struct hwb_span){2, 1}, &v) == HWB_ARG && v == 123);
    CHECK(hwb_eval(&state, (struct hwb_span){0, program.source_bytes + 1}, &v) == HWB_ARG && v == 123);
}
static void grammar(void)
{
    HWB_TEST("grammar");
    parse_ok("\t# comment\r\n\r\n let x = (1 + 2) << 3 # end\r\n log \"# text\"\r\nend");
    CHECK(program.instructions == 3 && program.label_count == 0);
    struct hwb_instruction i;
    u32 pc = 0;
    while (!(program.starts[pc / 8] & (1U << (pc % 8)))) pc++;
    CHECK(hwb_parse_instruction(&program, pc, &i) == HWB_OK);
    CHECK(i.op == HWB_LET && i.line == 3 && i.pc == pc);
    CHECK(hp_equal(program.source, i.name, "x"));
    CHECK(hp_equal(program.source, i.value, "(1 + 2) << 3"));
    CHECK(hwb_parse_instruction(&program, i.next_pc, &i) == HWB_OK && i.line == 4 && i.op == HWB_LOG);
    CHECK(hp_equal(program.source, i.text, "# text"));
    CHECK(hwb_parse_instruction(&program, i.next_pc, &i) == HWB_OK && i.line == 5 && i.op == HWB_END && i.next_pc == HWB_NO_PC);
    CHECK(hwb_parse_instruction(&program, 0, &i) == HWB_ARG);
    CHECK(hwb_parse_instruction(&program, HWB_NO_PC, &i) == HWB_ARG);
    parse_ok(""); CHECK(program.instructions == 0);
    parse_ok("wait us 0"); /* §3 value range belongs to exec, like a variable. */
    parse_ok("let x = missing + 1"); /* Initial variables/control flow resolve at runtime. */
    const char *bad[] = {"include foo", "let x = 1; end", "let x = 1 end", "let x = 1.2", "let x = 0x100000000",
        "let x =", "wait us", "wait ms 1", "wait us 1 + 2", "print tag 1", "log \"x", "log \"x\" 1",
        "return 1", "end end", "label 1x", "goto", "if 1 = 1 goto x", "device d = bdf 0:1",
        "device d = id gg:1234", "window v = phys d 1", "ioread8 phys 1 -> x", "mmioread8 port 1 -> x",
        "mmioread8 pci d 0 -> x", "pciread8 d (1 + 2 -> x", "iowrite8 port 1", "iowrite8 port 1(2)", "iowrite8 port (1)2", "ioread8 port 1 x",
        "poll 64 phys 0 1 1 2 1 -> x", "poll 8 phys 0 1 1 2 -> x", "let x = 1\rend"};
    for (u32 k = 0; k < sizeof(bad)/sizeof(bad[0]); k++) parse_bad(bad[k], 1);
    parse_bad("label x\nif 1 == 2goto x", 2);
    parse_bad("print \"tag\"1", 1);
    parse_bad("# hi\n\n\tlet x = 1\n bad", 4);
    parse_bad("end\nlet x = 1\n\r", 3);
    char nul[] = {'e','n','d',0,'\n'}; u32 line;
    CHECK(hwb_parse(&program, nul, sizeof(nul), &line) == HWB_ARG && line == 1);
}
static void instructions(void)
{
    HWB_TEST("instructions");
    /* §2 table: independent command/space/width expectations. */
    const char *commands[] = {
        "pciread8 d (4 + 2) -> x", "pciread16 d 4 -> x", "pciread32 d 4 -> x",
        "pciwrite8 d 4 x | 1", "pciwrite16 d 4 1", "pciwrite32 d 4 1",
        "ioread8 port 0x60 -> x", "ioread16 bar d 1 + 0 -> x", "ioread32 v + 0 -> x",
        "iowrite8 port 0x68 14", "iowrite16 bar d 1 + 0 1", "iowrite32 v + 0 1",
        "mmioread8 phys 0xA0000 -> x", "mmioread16 bar d 1 + (x + 2) -> x", "mmioread32 v + x -> x",
        "mmiowrite8 phys 0xA0000 65", "mmiowrite16 bar d 1 + 0 1", "mmiowrite32 v + 0 1"};
    for (u32 k = 0; k < sizeof(commands)/sizeof(commands[0]); k++) {
        parse_ok(commands[k]); struct hwb_instruction i;
        CHECK(hwb_parse_instruction(&program, 0, &i) == HWB_OK);
        const u32 widths[] = {1,2,4};
        CHECK(i.width == widths[k % 3]);
        CHECK(i.op == ((k / 3) % 2 ? HWB_WRITE : HWB_READ));
        CHECK(i.space == (k < 6 ? HW_PCI : k < 12 ? HW_IO : HW_PHYS));
        CHECK(i.next_pc == HWB_NO_PC && program.instructions == 1);
    }
    const char *badwidth[] = {"pciread64 d 0 -> x", "pciwrite08 d 0 1", "ioread0x8 port 0 -> x",
        "iowrite1 port 0 1", "mmioread8x phys 0 -> x", "mmiowrite 8 phys 0 1"};
    for (u32 k = 0; k < sizeof(badwidth)/sizeof(badwidth[0]); k++) parse_bad(badwidth[k], 1);
    parse_ok("device d = bdf 0x0:0x8.0x0");
    struct hwb_instruction i;
    CHECK(hwb_parse_instruction(&program, 0, &i) == HWB_OK && i.op == HWB_DEVICE_BDF);
    CHECK(hp_equal(program.source, i.dev, "0x8"));
    parse_ok("device d = id abCD:0x9660");
    CHECK(hwb_parse_instruction(&program, 0, &i) == HWB_OK && i.op == HWB_DEVICE_ID);
    CHECK(hp_equal(program.source, i.vendor, "abCD") && hp_equal(program.source, i.device, "0x9660"));
    parse_ok("window v = bar d (1 + 1)");
    CHECK(hwb_parse_instruction(&program, 0, &i) == HWB_OK && i.op == HWB_WINDOW_BAR);
    CHECK(hp_equal(program.source, i.address.device, "d") && hp_equal(program.source, i.address.bar, "(1 + 1)"));
    const char *polls[] = {"poll 8 pci d 0 255 1 100 1 -> x", "poll 16 port 0 1 1 2 1 -> x",
        "poll 32 phys (1 + 3) (1 << 2) 4 (10 + 2) 1 -> x", "poll 8 bar d 1 + 0 1 1 2 1 -> x", "poll 8 v + 0 1 1 2 1 -> x"};
    for (u32 k = 0; k < sizeof(polls)/sizeof(polls[0]); k++) {
        parse_ok(polls[k]); CHECK(hwb_parse_instruction(&program, 0, &i) == HWB_OK);
        CHECK(i.op == HWB_POLL && i.space == (k == 0 ? HW_PCI : k == 1 ? HW_IO : HW_PHYS));
        CHECK(hp_equal(program.source, i.name, "x") && hp_equal(program.source, i.interval, "1"));
    }
    parse_ok("label target\nif 1 + 2 != 4 goto target\ngoto target\ncall target\nreturn\nend");
    CHECK(program.instructions == 6 && program.label_count == 1 && program.labels[0].pc == 0);
    CHECK(hwb_parse_instruction(&program, 13, &i) == HWB_OK && i.op == HWB_IF && i.not_equal);
    parse_ok("label target\nif (1 + 2) == 3 goto target\nprint \"tag\" 1 + 2");
}
static void labels(void)
{
    HWB_TEST("undefined-label");
    parse_bad("# hi\ngoto missing\nend", 2);
    parse_bad("end\ncall missing", 2);
    parse_bad("\nif 1 == 2 goto missing", 2);
    parse_ok("goto forward\ncall forward\nlabel forward\nend");
    HWB_TEST("duplicate-label"); parse_bad("label x\n\nlabel x", 3);
    parse_ok("label x\nlabel X\nend");
}
static void limits(void)
{
    HWB_TEST("limits");
    parse_ok("let abcdefghijklmnop = 1");
    parse_bad("let abcdefghijklmnopq = 1", 1);
    parse_ok("label abcdefghijklmnop\ngoto abcdefghijklmnop");
    parse_bad("label abcdefghijklmnopq", 1);
    parse_bad("let x = abcdefghijklmnopq", 1);
    parse_bad("goto abcdefghijklmnopq", 1);
    parse_ok("log \"\"");
    /* Maximum nesting permitted by a 256-byte source line, no smaller cap. */
    u32 pos = 0;
    const char *head = "let x = ";
    while (head[pos]) { buffer[pos] = head[pos]; pos++; }
    const u32 pairs = (HWB_LINE_BYTES - pos - 1U) / 2U;
    for (u32 j = 0; j < pairs; j++) buffer[pos++] = '(';
    buffer[pos++] = '1';
    for (u32 j = 0; j < pairs; j++) buffer[pos++] = ')';
    buffer[pos] = 0; parse_ok(buffer);
    buffer[0] = '#';
    for (u32 n = 1; n < HWB_LINE_BYTES + 1; n++) buffer[n] = 'x';
    u32 line;
    CHECK(hwb_parse(&program, buffer, HWB_LINE_BYTES, &line) == HWB_OK);
    CHECK(hwb_parse(&program, buffer, HWB_LINE_BYTES + 1, &line) == HWB_ARG && line == 1);
    buffer[HWB_LINE_BYTES] = '\r'; buffer[HWB_LINE_BYTES+1] = '\n';
    CHECK(hwb_parse(&program, buffer, HWB_LINE_BYTES + 2, &line) == HWB_OK);
    for (u32 n = 0; n <= HWB_FILE_BYTES; n++) buffer[n] = '\n';
    CHECK(hwb_parse(&program, buffer, HWB_FILE_BYTES, &line) == HWB_OK);
    CHECK(hwb_parse(&program, buffer, HWB_FILE_BYTES + 1, &line) == HWB_ARG && line == HWB_FILE_BYTES + 1);
    /* Executable starts in the first/last bitmap byte; no instruction-count cap. */
    for (u32 n = 0; n < HWB_FILE_BYTES; n += 4) {
        buffer[n] = 'e'; buffer[n+1] = 'n'; buffer[n+2] = 'd'; buffer[n+3] = '\n';
    }
    CHECK(hwb_parse(&program, buffer, HWB_FILE_BYTES, &line) == HWB_OK);
    CHECK(program.instructions == HWB_FILE_BYTES / 4);
    struct hwb_instruction i;
    CHECK(hwb_parse_instruction(&program, HWB_FILE_BYTES - 4, &i) == HWB_OK && i.line == HWB_FILE_BYTES / 4 && i.next_pc == HWB_NO_PC);
    u32 n = 0;
    for (u32 k = 0; k <= HWB_LABELS; k++) {
        const char *prefix = "label l";
        for (u32 j = 0; prefix[j]; j++) buffer[n++] = prefix[j];
        buffer[n++] = '0' + k / 10; buffer[n++] = '0' + k % 10; buffer[n++] = '\n';
        if (k + 1 == HWB_LABELS) {
            CHECK(hwb_parse(&program, buffer, n, &line) == HWB_OK && program.label_count == HWB_LABELS);
        }
    }
    CHECK(hwb_parse(&program, buffer, n, &line) == HWB_ARG && line == HWB_LABELS + 1);
}

/* Literal copy of the finalized design §8, including comments. */
static const char first_script[] =
"# /test/hw/trident.hwb \u2014 vgaHWProtect\u672a\u5c55\u958b\u306e\u8abf\u67fb\u7528\u8ee2\u8a18\n"
" device d = bdf 0:8.0\n"
" window v = bar d 1\n"
" if mode == 1 goto enable\n"
" if mode == 2 goto disable\n"
" mmiowrite8 v + 0x3C4 0x09\n"
" mmioread8 v + 0x3C5 -> chip\n"
" print \"SR09\" chip\n"
" end\n"
" label enable\n"
" iowrite8 port 0x68 0x0E\n"
" iowrite8 port 0x6A 0x07\n"
" iowrite8 port 0x6A 0x8F\n"
" iowrite8 port 0x6A 0x06\n"
" # \u5143\u30bd\u30fc\u30b9704: vgaHWProtect(TRUE) \u5c55\u958b\u4f4d\u7f6e\n"
" mmiowrite8 v + 0x3D4 0x23\n"
" mmioread8 v + 0x3D5 -> t\n"
" mmiowrite16 v + 0x3D4 0x23 | ((t & 0xDF) << 8)\n"
" mmiowrite8 v + 0x3D4 0x29\n"
" mmioread8 v + 0x3D5 -> t\n"
" mmiowrite16 v + 0x3D4 0x29 | ((t | 4) << 8)\n"
" let mask = 0xFF\n"
" let bits = 6\n"
" call dac\n"
" let bits = 8\n"
" call dac\n"
" mmiowrite8 v + 0x3CE 0x23\n"
" mmioread8 v + 0x3CF -> t\n"
" mmiowrite16 v + 0x3CE 0x23 | ((t & 0xFC) << 8)\n"
" let bits = 1\n"
" call dac\n"
" mmiowrite8 v + 0x3C4 1\n"
" mmioread8 v + 0x3C5 -> t\n"
" mmiowrite16 v + 0x3C4 1 | ((t & 0xEF) << 8)\n"
" # \u5143\u30bd\u30fc\u30b9727: vgaHWProtect(FALSE) \u5c55\u958b\u4f4d\u7f6e\n"
" iowrite8 port 0xFAC 2\n"
" end\n"
" label disable\n"
" iowrite8 port 0xFAC 0\n"
" # \u5143\u30bd\u30fc\u30b9740: vgaHWProtect(TRUE) \u5c55\u958b\u4f4d\u7f6e\n"
" mmiowrite8 v + 0x3C4 1\n"
" mmioread8 v + 0x3C5 -> t\n"
" mmiowrite16 v + 0x3C4 1 | ((t | 0x10) << 8)\n"
" let bits = 0\n"
" let mask = 0xFE\n"
" call dac\n"
" mmiowrite8 v + 0x3CE 0x23\n"
" mmioread8 v + 0x3CF -> t\n"
" mmiowrite16 v + 0x3CE 0x23 | (((t & 0xFC) | 1) << 8)\n"
" let mask = 0xFD\n"
" call dac\n"
" let mask = 0xCF\n"
" call dac\n"
" let mask = 0xF7\n"
" call dac\n"
" let mask = 0xFB\n"
" call dac\n"
" mmiowrite8 v + 0x3D4 0x29\n"
" mmioread8 v + 0x3D5 -> t\n"
" mmiowrite16 v + 0x3D4 0x29 | ((t & 0xFB) << 8)\n"
" mmiowrite8 v + 0x3D4 0x23\n"
" mmioread8 v + 0x3D5 -> t\n"
" mmiowrite16 v + 0x3D4 0x23 | ((t | 0x20) << 8)\n"
" # \u5143\u30bd\u30fc\u30b9769: vgaHWProtect(FALSE) \u5c55\u958b\u4f4d\u7f6e\n"
" iowrite8 port 0x6A 0x07\n"
" iowrite8 port 0x6A 0x8E\n"
" iowrite8 port 0x6A 0x06\n"
" iowrite8 port 0x68 0x0F\n"
" end\n"
" label dac\n"
" mmiowrite8 v + 0x83C8 4\n"
" mmioread8 v + 0x83C6 -> t\n"
" mmiowrite8 v + 0x83C8 4\n"
" mmiowrite8 v + 0x83C6 (t & mask) | bits\n"
" return\n"
;
static void design_script(void)
{
    HWB_TEST("design-script"); parse_ok(first_script);
    /* §8 independent tally: preamble 8, enable 26, disable 30, dac 6.
     * Label byte positions checked using physical lines, not product indexing. */
    CHECK(program.instructions == 70 && program.label_count == 3);
    const char *names[] = {"enable", "disable", "dac"};
    const u32 physical_lines[] = {10, 38, 70};
    for (u32 k = 0; k < 3; k++) {
        CHECK(hwbf_equal(program.labels[k].name, names[k]));
        u32 pc = program.labels[k].pc, line = 1;
        for (u32 n = 0; n < pc; n++) if (first_script[n] == '\n') line++;
        CHECK(line == physical_lines[k]);
        struct hwb_instruction i;
        CHECK(hwb_parse_instruction(&program, pc, &i) == HWB_OK && i.op == HWB_LABEL && i.line == line);
    }
}
void _start(void)
{
    expressions(); grammar(); instructions(); labels(); limits(); design_script();
    CHECK(fake.event_count == 0);
    hwb_test_write("PASS parse/eval/grammar/instructions/labels/limits/design-script; no effects\n");
    hwb_test_exit(0);
}
