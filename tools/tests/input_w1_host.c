#include "input_policy.h"
static u8 wire[IKM_HEADER_SIZE + IKM_MAX_ROWS * IKM_ROW_SIZE];
static int check(void)
{
    u32 count = 99;
    IkmRow row;
    wire[0]='I'; wire[1]='K'; wire[2]='M'; wire[3]='1';
    wire[4]=2; wire[6]=16; wire[8]=1;
    wire[16]=8; wire[17]=3; wire[18]=4; wire[19]=5;
    wire[20]=0x34; wire[21]=0x12; wire[22]=0x78; wire[23]=0x56;
    wire[24]=0xef; wire[25]=0xcd; wire[26]=0xab; wire[27]=0x89;
    wire[28]=0x67; wire[29]=0x45; wire[30]=0x23; wire[31]=1;
    if (ikm_wire_header(wire, 32, &count) || count != 1) return 1;
    if (ikm_wire_row(wire, 32, 0, &row) || row.kind != 8 || row.profile != 3 ||
        row.context != 4 || row.flags != 5 || row.index != 0x1234 ||
        row.selector != 0x5678 || row.value != 0x89abcdef || row.aux != 0x01234567) return 2;
    wire[4]=1; count=99;
    if (!ikm_wire_header(wire, 32, &count) || count != 99) return 3;
    wire[4]=2; wire[12]=1;
    if (!ikm_wire_header(wire, 32, &count)) return 4;
    wire[12]=0;
    if (!ikm_wire_header(wire, 31, &count) || !ikm_wire_row(wire, 32, 1, &row)) return 5;
    wire[8]=0; wire[9]=0x20;
    if (ikm_wire_header(wire, sizeof(wire), &count) || count != 8192) return 6;
    wire[8]=1;
    if (!ikm_wire_header(wire, sizeof(wire), &count)) return 7;
    return 0;
}
void _start(void) {
    int r = check();
#ifdef PPC_TEST
    register int call __asm__("r0") = 1;
    register int status __asm__("r3") = r;
    __asm__ volatile("sc" : : "r"(call), "r"(status) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(r));
#endif
    __builtin_unreachable();
}
