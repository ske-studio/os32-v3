#include <cstdio>
#include "fmgen_headers.h"
#define private public
#define protected public
#include "fmgen_opna.h"
#undef private
#undef protected
int main() {
    static FM::OPNA chip;
    chip.adpcmbuf = new uint8[0x40000]();
    chip.SetRate(7987200,44100,false);
    chip.OPNABase::Init(7987200,44100,false);
    chip.Reset();
    const int regs[][2]={{0x30,1},{0x40,12},{0x50,31},{0x80,15},{0xb0,7},
                         {0xb4,0x80},{0xa4,0x22},{0xa0,0x69},{0x28,0x10}};
    for (auto &r:regs) chip.SetReg(r[0],r[1]);
    for (int i=0;i<44100;i++) {
        FM::Sample sample[2]={};
        chip.Mix(sample,1);
        // sndl_test mixes a synchronization marker into the right channel.
        unsigned left = sample[0], right = sample[1] + (i % 256 == 0 ? 12345 : 0);
        const unsigned char frame[] = {
            (unsigned char)left, (unsigned char)(left >> 8),
            (unsigned char)right, (unsigned char)(right >> 8)
        };
        if (fwrite(frame, 1, sizeof frame, stdout) != sizeof frame) return 1;
    }
}
