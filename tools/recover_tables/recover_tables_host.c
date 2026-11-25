/* Separate TUs export the old oracle and the product using the same schema. */
void recover_tables_dump_oracle(void);
void recover_tables_dump_product(void);
void _start(void)
{
    recover_tables_dump_oracle();
    recover_tables_dump_product();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0) : "memory");
    __builtin_unreachable();
}
