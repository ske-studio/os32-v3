static void inspection_test(void)
{
    setup();core.token.session=5;
    CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(contains(serial_output,"GF inspect sid=5 rc=0 rom_len=512 crc=E0A29354"));
    CHECK(contains(serial_output," chip=44 expect=44 match=1\n"));
    CHECK(!writes && !begins && core.token.session==5 && core.seq==0);
    clear_output();CHECK(call(GF_OP_INSPECT,0,0)==0 && !serial_length);
    /* A request with different flags is not the cached response. */
    CHECK(call(GF_OP_INSPECT,GF_ALLOW_WRITE,0)==0 && serial_length);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && !writes && !gf_lease.active && !live_pages);
    setup();image[0x62]=0x43;image[511]++;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && !writes && !live_pages);
    CHECK(contains(serial_output," chip=43 expect=44 match=0\n"));
    setup();image[0x4c]='x';
    CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(contains(serial_output," chip=unknown expect=44 match=unknown\n"));
    setup();peek_fail=1;
    CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(equal(serial_output,"GF inspect sid=0 rc=-1 rom_len=unknown crc=unknown chip=unknown expect=44 match=unknown\n"));
    setup();decode_fail=1;
    CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(contains(serial_output,"GF inspect sid=0 rc=-1 rom_len=512 crc=E0A29354"));
    CHECK(contains(serial_output," chip=44 expect=44 match=1\n"));
    setup();image[0x50]=0xff;image[0x51]=0xff;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"chip=unknown"));
    setup();image[0x4e]=2;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"chip=unknown"));
    setup();image[0x49]=5;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"chip=unknown"));
    setup();image[511]++;
    CHECK(call(GF_OP_INSPECT,0,0)<0 && contains(serial_output,"match=1"));
    setup();rshell_active=0;CHECK(call(GF_OP_INSPECT,0,0)==0 && !serial_length);
}
