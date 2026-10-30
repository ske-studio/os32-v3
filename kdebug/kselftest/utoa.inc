static void test_utoa(void)
{
    char buf[16];

    check(kutoa_dec(0, buf, 12) == 1 && kstrcmp(buf, "0") == 0,
          "kutoa_dec 0");
    check(kutoa_dec(4294967295UL, buf, 12) == 10 &&
          kstrcmp(buf, "4294967295") == 0, "kutoa_dec u32 max");
    check(kutoa_hex(0, buf, 12, 0) == 1 && kstrcmp(buf, "0") == 0,
          "kutoa_hex 0");
    check(kutoa_hex(0xDEADBEEFUL, buf, 12, 1) == 8 &&
          kstrcmp(buf, "DEADBEEF") == 0, "kutoa_hex upper");
    check(kutoa_hex(0xDEADBEEFUL, buf, 12, 0) == 8 &&
          kstrcmp(buf, "deadbeef") == 0, "kutoa_hex lower");
}
