/* Host-only field serializer, included once in each separate table TU.
 * No struct memcmp or pointer addresses: references carry names and contents. */
#define RT_BUFFER_SIZE 4096U
#define RT_SCALAR_SLOT 0U
static char rt_buffer[RT_BUFFER_SIZE];
static u32 rt_used;
static void rt_flush(void)
{
    u32 done = 0;
    while (done < rt_used) {
        long result;
        __asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1),
                         "c"(rt_buffer + done), "d"(rt_used - done) : "memory");
        if (result <= 0) {
            __asm__ volatile("int $0x80" : : "a"(1), "b"(1) : "memory");
            __builtin_unreachable();
        }
        done += result;
    }
    rt_used = 0;
}
static void rt_text(const char *s)
{
    while (*s) {
        if (rt_used == RT_BUFFER_SIZE) rt_flush();
        rt_buffer[rt_used++] = *s++;
    }
}
static void rt_number(u32 n)
{
    char digits[11];
    u32 count = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    while (count) {
        char c[2] = {digits[--count], 0};
        rt_text(c);
    }
}
static void rt_key(const char *table, u32 row, const char *field, u32 slot)
{
    rt_text(table); rt_text("\t"); rt_number(row); rt_text("\t");
    rt_text(field); rt_text("\t"); rt_number(slot); rt_text("\t");
}
static void rt_value(const char *table, u32 row, const char *field, u32 slot, u32 value)
{
    rt_key(table, row, field, slot); rt_number(value); rt_text("\n");
}
static void rt_reference(const char *table, u32 row, const char *field,
                         const void *pointer)
{
    rt_key(table, row, field, RT_SCALAR_SLOT);
    rt_text(rt_symbol(pointer)); rt_text("\n");
}
static void rt_array(const char *table, u32 row, const char *field,
                     const u32 *values, u32 count)
{
    rt_reference(table, row, field, values);
    if (values)
        for (u32 j = 0; j < count; j++)
            rt_value(table, row, field, j + 1, values[j]);
}
#define RT_FIELD(field) rt_value(table, i, #field, RT_SCALAR_SLOT, item->field)
extern u32 recover_profile_crc(const struct recover_profile *p);
void RT_DUMP(void)
{
#ifdef RT_EXPAND_PRODUCT
    static struct gfinit_plan buffer;
    struct recover_profile expanded;
    if (recover_gf_profile_expand(&expanded, &buffer)) {
        rt_text("expand failed\n"); rt_flush();
        return;
    }
    const struct recover_profile *p = &expanded;
#else
    const struct recover_profile *p = &recover_gf_template;
#endif
    rt_value("profile", 0, "target_count", 0, p->target_count);
    rt_value("profile", 0, "rule_count", 0, p->rule_count);
    rt_value("profile", 0, "layout_count", 0, p->layout_count);
    rt_value("profile", 0, "expr_count", 0, p->expr_count);
    rt_value("profile", 0, "crc", 0, recover_profile_crc(p));
    for (u32 i = 0; i < p->target_count; i++) {
        const char *table = "targets";
        const struct recover_target *item = &p->targets[i];
        RT_FIELD(space); RT_FIELD(bar); RT_FIELD(offset); RT_FIELD(head);
        RT_FIELD(port); RT_FIELD(index); RT_FIELD(width); RT_FIELD(access); RT_FIELD(read_safe);
    }
    for (u32 i = 0; i < p->rule_count; i++) {
        const char *table = "rules";
        const struct recover_rule *item = &p->rules[i];
        RT_FIELD(id); RT_FIELD(key); RT_FIELD(phase); RT_FIELD(main_id);
        RT_FIELD(pc); RT_FIELD(substep); RT_FIELD(write_mask); RT_FIELD(verify_mask);
        RT_FIELD(value.mode); RT_FIELD(value.mask); RT_FIELD(value.bits);
        RT_FIELD(value.min); RT_FIELD(value.max); RT_FIELD(value.expr_id);
        RT_FIELD(value.allowed_count); RT_FIELD(requires_count); RT_FIELD(guard_count); RT_FIELD(flags);
        rt_array(table, i, "value.allowed", item->value.allowed, item->value.allowed_count);
        rt_array(table, i, "requires", item->requires, item->requires_count);
        rt_reference(table, i, "guards", item->guards);
        if (item->guards)
            for (u32 j = 0; j < item->guard_count; j++) {
                const struct recover_guard *g = &item->guards[j];
                rt_value(table, i, "guards.key", j, g->key);
                rt_value(table, i, "guards.mask", j, g->mask);
                rt_value(table, i, "guards.expected", j, g->expected);
                rt_value(table, i, "guards.when", j, g->when);
            }
    }
    for (u32 i = 0; i < p->layout_count; i++) {
        const char *table = "layouts";
        const struct recover_opcode_layout *item = &p->layouts[i];
        RT_FIELD(opcode); RT_FIELD(substep); RT_FIELD(argc); RT_FIELD(dir_substep); RT_FIELD(flags);
        rt_array(table, i, "kinds", item->kinds, item->argc);
    }
    for (u32 i = 0; i < p->expr_count; i++) {
        const char *table = "exprs";
        const struct recover_expr *item = &p->exprs[i];
        RT_FIELD(id); RT_FIELD(argc);
        rt_array(table, i, "kinds", item->kinds, item->argc);
        rt_key(table, i, "eval", 0); rt_text(rt_eval_symbol(item->eval)); rt_text("\n");
    }
    rt_text("END\n");
    rt_flush();
}
#undef RT_FIELD
