/*
 * Copyright (c) 1996, 1998 Bill Studenmund
 * Copyright (c) 1995 Gordon W. Ross
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* See flow_fixture.h for sources and regeneration. No libc, native i386 or
 * qemu-i386; every scenario is isolated in a process. Exit codes identify the
 * failed invariant; 0 means the entire expected CI/MMIO stream was consumed. */
#include "ofw.h"
#include "kstring.h"
#include "flow_fixture.h"
#include "ppc_pmap.h"
#ifndef FLOW_STAGE
#define FLOW_STAGE 4
#endif
static struct flow_step steps[64];
static unsigned used, cursor, drain_polls, selected;
static int scenario;
static cell bar[5] = {0x02000000, 0, 0x80000000, 0, 1};
static cell reg[2] = {0x13020, 1};
static void finish(unsigned rc) __attribute__((noreturn));
static void finish(unsigned rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(x, rc) do { if (!(x)) finish(rc); } while (0)
static struct flow_step *step(const char *name, unsigned ni, unsigned no, int result)
{
    REQUIRE(used < sizeof(steps)/sizeof(steps[0]), 10);
    struct flow_step *s = &steps[used++];
    s->name = name; s->ni = ni; s->no = no; s->result = result;
    return s;
}
static int phandle(int value) { return value > 0 ? (int)(0xff000000U | value) : value; }
static void find(const char *path, int result)
{ struct flow_step *s = step("finddevice", 1, 1, phandle(result)); s->text = path; }
static void prop(int ph, const char *name, unsigned cap, int result,
                 const void *data, unsigned bytes)
{
    struct flow_step *s = step("getprop", 4, 1, result);
    s->arg[0] = phandle(ph); s->arg[3] = cap; s->text = name;
    s->payload = data; s->payload_len = bytes;
}
static void value(int ph, const char *name, int v, int result)
{
    struct flow_step *s;
    prop(ph, name, 4, result, 0, 0); s = &steps[used-1];
    s->payload_kind = 1; s->arg[4] = v;
}
static void unary(const char *name, int input, int output)
{ step(name, 1, 1, phandle(output))->arg[0] = phandle(input); }
static void opened(const char *path, int result, int rc)
{ struct flow_step *s = step("open", 1, 1, result); s->text = path; s->rc = rc; }
static void written(const char *text)
{ struct flow_step *s = step("write", 3, 1, kstrlen(text)); s->arg[0] = scenario == SCREEN ? 9 : 8; s->text = text; }
static void failure(const char *text, int exit_call)
{ written(text); if (exit_call) step("exit", 0, 0, 0); }
static int setup_script(void)
{
    find("/chosen", scenario == BAD_CHOSEN ? -1 : 7);
    if (scenario == BAD_CHOSEN) { step("exit", 0, 0, 0); return 0; }
    value(7, "stdin", 6, scenario == BAD_STDIN ? 3 : 4);
    if (scenario == BAD_STDIN) { step("exit", 0, 0, 0); return 0; }
    value(7, "stdout", scenario == SCREEN || scenario == SCREEN_FAIL ? 0 : 8,
          scenario == BAD_STDOUT ? 3 : 4);
    if (scenario == BAD_STDOUT) { step("exit", 0, 0, 0); return 0; }
    if (scenario == SCREEN || scenario == SCREEN_FAIL) {
        opened("screen", scenario == SCREEN_FAIL ? 0 : 9, 0);
        if (scenario == SCREEN_FAIL) { step("exit", 0, 0, 0); return 0; }
    }
    find("/", scenario == BAD_ROOT ? -1 : 10);
    find("/options", scenario == OPTIONS_ABSENT ? -1 : 11);
    find("/openprom", 12); find("/chosen", 7);
    if (scenario == BAD_ROOT) { failure("FAIL:environment:-2\r\n", 1); return 0; }
    if (scenario != OPTIONS_ABSENT)
        prop(11, "real-mode?", 32, scenario == REAL ? 4 : 5,
             scenario == REAL ? "true" : "false", scenario == REAL ? 4 : 5);
    value(10, "#address-cells", scenario == AC2 ? 2 : scenario == BAD_AC ? 3 : 1, 4);
    value(10, "#size-cells", scenario == BAD_SC ? 2 : 1, 4);
    if (scenario == BAD_SC || scenario == BAD_AC) { failure("FAIL:environment:-2\r\n", 1); return 0; }
    value(7, "memory", scenario == BAD_MEMORY ? -1 : 13, 4);
    value(7, "mmu", scenario == BAD_MMU ? -1 : 14, 4);
    if (scenario == BAD_MEMORY || scenario == BAD_MMU) { failure("FAIL:environment:-2\r\n", 1); return 0; }
    return 1;
}
static int tree_script(void)
{
    find("/", 10); unary("child", 10, scenario == TREE_EMPTY ? 0 : 20);
    if (scenario == TREE_EMPTY) return 0;
    prop(20, "name", 4096, 5, "escc", 5);
    unary("child", 20, 21); prop(21, "name", 4096, 5, "ch-a", 5);
    unary("parent", 21, 20);
    prop(20, "name", 4096, scenario == LEGACY_PARENT ? 12 : 5,
         scenario == LEGACY_PARENT ? "escc-legacy" : "escc",
         scenario == LEGACY_PARENT ? 12 : 5);
    if (scenario == LEGACY_PARENT) {
        unary("child", 21, 0); unary("peer", 21, 0); unary("peer", 20, 0); return 0;
    }
    prop(21, "reg", 4096, scenario == BAD_REG ? 3 : 8, reg, scenario == BAD_REG ? 3 : 8);
    if (scenario == BAD_REG) return 0;
    unary("parent", 21, 20); unary("parent", 20, 30);
    if (scenario == HIGH_BAR) bar[1] = 1;
    if (scenario == WRAP_ADDR) bar[2] = 0xfffffff0U;
    prop(30, "assigned-addresses", 4096, scenario == BAD_BAR ? 8 : 20, bar,
         scenario == BAD_BAR ? 8 : 20);
    if (scenario == BAD_BAR || scenario == HIGH_BAR || scenario == WRAP_ADDR) return 0;
    struct flow_step *s = step("package-to-path", 3, 1, scenario == BAD_PATH ? 1024 : 10);
    s->arg[0] = phandle(21); s->arg[2] = 1024; s->payload = "/escc/ch-a"; s->payload_len = 10;
    if (scenario == BAD_PATH) return 0;
    opened("/escc/ch-a", scenario == OPEN_ZERO ? 0 : 15, scenario == OPEN_CI ? -1 : 0);
    if (scenario == OPEN_ZERO || scenario == OPEN_CI) return -1;
    if (scenario != REAL) {
        s = step("call-method", scenario == AC2 ? 7 : 6, 1, scenario == MAP_CATCH ? 17 : 0);
        s->text = "map"; s->arg[1] = 14; s->arg[2] = 0x2a; s->arg[3] = 0x20;
        s->arg[4] = FX_CSR; s->arg[5] = FX_CSR; s->arg[6] = 0;
        if (scenario == MAP_CI) s->rc = -1;
        if (scenario == MAP_CI || scenario == MAP_CATCH) return -2;
    }
    return 1;
}
static int fake_ci(void *frame)
{
    cell *a = frame;
    REQUIRE(cursor < used, 20);
    struct flow_step *s = &steps[cursor++];
    const char *name = ppc_cell_ptr(a[0]);
    REQUIRE(kstrcmp(name, s->name) == 0 && a[1] == s->ni && a[2] == s->no, 21);
    cell *v = a+3;
    if (!kstrcmp(name, "finddevice") || !kstrcmp(name, "open")) {
        REQUIRE(!kstrcmp(ppc_cell_ptr(v[0]), s->text), 22);
    } else if (!kstrcmp(name, "getprop")) {
        REQUIRE(v[0] == s->arg[0] && v[3] == s->arg[3], 23);
        REQUIRE(!kstrcmp(ppc_cell_ptr(v[1]), s->text), 24);
        if (s->payload_kind) *(cell *)ppc_cell_ptr(v[2]) = s->arg[4];
        else if (s->payload_len) kmemcpy(ppc_cell_ptr(v[2]), s->payload, s->payload_len);
    } else if (!kstrcmp(name, "package-to-path")) {
        REQUIRE(v[0] == s->arg[0] && v[2] == s->arg[2], 25);
        kmemcpy(ppc_cell_ptr(v[1]), s->payload, s->payload_len);
    } else if (!kstrcmp(name, "write")) {
        REQUIRE(v[0] == s->arg[0] && v[2] == kstrlen(s->text), 26);
        REQUIRE(!memcmp(ppc_cell_ptr(v[1]), s->text, v[2]), 27);
    } else if (!kstrcmp(name, "call-method")) {
        REQUIRE(!kstrcmp(ppc_cell_ptr(v[0]), s->text), 28);
        for (unsigned i=1; i<s->ni; i++) REQUIRE(v[i] == s->arg[i], 29);
    } else if (s->ni) REQUIRE(v[0] == s->arg[0], 30);
    if (s->no) v[s->ni] = s->result;
    return s->rc;
}

unsigned char pm_image_start[1],pm_image_end[1];
static unsigned testcase;
unsigned int ppc_load8(cell va)
{
    REQUIRE(va==FX_CSR && selected,70);
    selected=0; drain_polls++;
    return testcase==1?0:1;
}
void ppc_store8(cell va,unsigned v)
{
    REQUIRE(va==FX_CSR && v==1 && !selected,71); selected=1;
}
/* ofw.c masks EE before its detached stop (D3); never reached in this flow. */
void ppc_disable(void) { }
void ppc_stop(void)
{
    REQUIRE(testcase!=0 && cursor==used && !selected,72);
    if (testcase==1) REQUIRE(drain_polls==FX_LIMIT+1,73);
    finish(0);
}
void ppc_takeover(const struct pm_boot_info *i)
{
    REQUIRE((testcase==0 || testcase==5) && cursor==used && drain_polls==1,74);
    REQUIRE(i->hpt_pa==0x100000 && i->hpt_mask==0x3fff && i->tb_hz==100000000,75);
    REQUIRE(i->escc_pa==FX_CSR && i->ram->count==1,76);
    finish(0);
}
static const unsigned char tb[]={5,245,225,0};
static const unsigned char mem[]={0,0,0,0,8,0,0,0};
static const unsigned char mem_sc2[]={0,0,0,0,0,0,0,0,8,0,0,0};
static const unsigned char small[]={0,0,0,0,0,0,16,0};
static void mmu_main(int argc,char **argv) __attribute__((used,noreturn));
static void mmu_main(int argc,char **argv)
{
    REQUIRE(argc==2,77); testcase=(unsigned)(argv[1][0]-'0'); REQUIRE(testcase<6,78);
    scenario=0;
    REQUIRE(setup_script(),79);
    if (testcase==5)
        for (unsigned i=0;i<used;i++)
            if (steps[i].text && !kstrcmp(steps[i].text,"#size-cells")) steps[i].arg[4]=2;
    REQUIRE(tree_script()==1,80);
    find("/",10); unary("child",10,40);
    prop(40,"timebase-frequency",512,testcase==4?-1:4,tb,testcase==4?0:4);
    if (testcase==4) {
        unary("child",40,0); unary("peer",40,0); written("FAIL:TB\r\n");
    } else {
        find("/memory",13);
        prop(13,"reg",testcase==5?384:256,testcase==3?-1:testcase==5?12:8,
             testcase==5?mem_sc2:mem,testcase==3?0:testcase==5?12:8);
        if (testcase==3) written("FAIL:REG\r\n");
        else {
            prop(10,"compatible",512,8,"MacRISC",8);
            prop(13,"available",256,8,testcase==2?small:mem,8);
            written(testcase==2?"FAIL:HPT\r\n":"TAKEOVER\r\n");
            if (testcase==1) written("FAIL:DRAIN\r\n");
        }
    }
    powermac_mmu_boot(fake_ci); finish(81);
}
__asm__(".globl _start\n_start:\n mov (%esp),%eax\n lea 4(%esp),%edx\n and $-16,%esp\n sub $8,%esp\n push %edx\n push %eax\n call mmu_main\n");
