#!/usr/bin/env python3
"""Exercise the real GEM MAC against an independent register trace."""
import argparse
import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name="ppc-lan-p16", tier=2, budget=60,
             covers=["drivers/net/gem/gem_mac.c", "tools/ppc/test_lan_p16.py"],
             inputs=["drivers/net/gem/gem.h", "include/types.h", "include/callconv.h"])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "drivers/net/gem/gem_mac.c"
HARNESS = r'''
#include <stdio.h>
#include "gem.h"
static unsigned regs[0x10000/4], n;
static struct { unsigned reg, val; } trace[128];
static u32 rd(void *p, u32 r) { (void)p; return regs[r/4]; }
static void wr(void *p, u32 r, u32 v) { (void)p; regs[r/4]=v; trace[n].reg=r; trace[n++].val=v; }
static const unsigned expected[][2] = {
 {0x6040,0},{0x6044,8},{0x6048,4},{0x6050,64},{0x6054,0x200005f2},
 {0x6058,7},{0x605c,4},{0x6060,16},{0x6064,0x8088},{0x6130,0x234},
 {0x608c,0},{0x6090,0},{0x6094,0},{0x6098,1},{0x609c,0xc200},{0x60a0,0x180},
 {0x60a4,0},{0x60a8,0},{0x60ac,0},{0x60b0,0},{0x60b4,0},
 {0x6100,0},{0x6104,0},{0x6108,0},{0x610c,0},{0x6110,0},{0x6114,0},
 {0x6118,0},{0x611c,0},{0x6120,0},{0x6124,0},{0x6128,0},{0x6008,0x1bf0},
 {0x0004,0x1fff},{0x6080,0x3456},{0x6084,0x12},{0x6088,0x5254},
 {0x60c0,0},{0x60c4,0},{0x60c8,0},{0x60cc,0},{0x60d0,0},{0x60d4,0},{0x60d8,0},{0x60dc,0},
 {0x60e0,0},{0x60e4,0},{0x60e8,0},{0x60ec,0},{0x60f0,0},{0x60f4,0},{0x60f8,0},{0x60fc,0},
 {0x6034,0x120},{0x6030,0},{0x6030,0x3f},{0x603c,0x61},{0x6038,0x80},{0x6034,0x125},{0x4100,28}
};
int main(void) {
 struct gem g = {0}; const u8 mac[6]={0x52,0x54,0,0x12,0x34,0x56};
 g.io.read32=rd; g.io.write32=wr; g.io.tb_hz=10000;
 regs[0x6034/4]=0x138; regs[0x6038/4]=0x83;
 if (gem_mac_init(&g,mac)) return 2;
 puts("REACHED init");
 if (gem_mac_enable_step(&g,1000)!=GEM_AGAIN || gem_mac_enable_step(&g,1099)!=GEM_AGAIN || n!=55) { puts("FAIL wait"); return 1; }
 puts("REACHED wait");
 if (gem_mac_enable_step(&g,1100)!=GEM_OK) return 3;
 puts("REACHED enabled");
 if (n != sizeof(expected)/sizeof(expected[0])) { puts("FAIL count"); return 1; }
 for (unsigned i=0;i<n;i++) if (trace[i].reg!=expected[i][0] || trace[i].val!=expected[i][1]) { printf("FAIL trace %u\n",i); return 1; }
 puts("PASS trace");
 struct gem stopped = {0};
 stopped.io.read32=rd; stopped.io.write32=wr; stopped.io.tb_hz=10000;
 stopped.polls=999999;
 if (gem_mac_enable_step(&stopped,2000)!=GEM_AGAIN || stopped.polls!=0) { puts("FAIL stopped start"); return 1; }
 puts("REACHED stopped");
 for (unsigned i=0;i<999999;i++)
  if (gem_mac_enable_step(&stopped,2000)!=GEM_AGAIN) { puts("FAIL stopped early"); return 1; }
 if (gem_mac_enable_step(&stopped,2000)!=GEM_ETIME || stopped.state!=GEM_STATE_QUARANTINED) { puts("FAIL stopped timeout"); return 1; }
 puts("PASS stopped"); return 0;
}
'''
MUTANTS = [
 ("seed", "((mac[5] << 8) | mac[4]) & 0x3ff", "mac[5] & 0x3ff", "FAIL trace 9", "REACHED enabled"),
 ("address", "(mac[4] << 8) | mac[5]", "(mac[0] << 8) | mac[1]", "FAIL trace 34", "REACHED enabled"),
 ("promisc", "GEM_MAC_RX_HASH_FILTER | GEM_MAC_RX_PROMISCUOUS | GEM_MAC_RX_PROMISC_GRP", "GEM_MAC_RX_HASH_FILTER | GEM_MAC_RX_PROMISC_GRP", "FAIL trace 53", "REACHED enabled"),
 ("wait", "now - g->started < ticks", "now - g->started < ticks / 2", "FAIL wait", "REACHED init"),
 ("kick", "GEM_RX_KICK, GEM_RX_PUBLISHED", "GEM_RX_KICK, 32", "FAIL trace 59", "REACHED enabled"),
 ("poll-limit", "++g->polls >= GEM_WAIT_POLLS", "++g->polls > GEM_WAIT_POLLS", "FAIL stopped timeout", "REACHED stopped"),
 ("crc", "v | GEM_MAC_RX_ENABLE | GEM_MAC_RX_STRIP_CRC", "v | GEM_MAC_RX_ENABLE", "FAIL trace 58", "REACHED enabled"),
]
def check(source, directory):
    c = directory / 'mac.c'; h = directory / 'test.c'; exe = directory / 'test'
    c.write_text(source); h.write_text(HARNESS)
    subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'), '-I'+str(ROOT/'drivers/net/gem'),str(c),str(h),'-o',str(exe)], check=True, capture_output=True)
    return subprocess.run([str(exe)], text=True, capture_output=True)
def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true'); args = parser.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp); result = check(source, directory)
        if result.returncode or 'PASS stopped' not in result.stdout: raise RuntimeError(result.stdout + result.stderr)
        print('PASS baseline')
        if args.mutate:
            for name, old, new, failure, reached in MUTANTS:
                if source.count(old) != 1: raise RuntimeError('anchor ' + name)
                result = check(source.replace(old,new), directory)
                if result.returncode != 1 or failure not in result.stdout.splitlines() or reached not in result.stdout.splitlines():
                    raise RuntimeError(name + ': ' + result.stdout + result.stderr)
                print('RED ' + name)
if __name__ == '__main__': main()
