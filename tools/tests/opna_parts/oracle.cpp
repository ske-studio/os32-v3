// Host-only adapter to the unmodified fmgen translation units.
#include <cstdio>
#include "fmgen_headers.h"
#define private public
#define protected public
#include "fmgen_opna.h"
#undef protected
#undef private

struct Oracle : FM::OPNA {
    bool csm;
    void TimerA() override { csm = (regtc & 128) != 0; }
};
int main() {
    static Oracle chip;
    chip.OPNABase::SetRate(7987200, 44100, false);
    chip.adpcmbuf = new uint8[0x40000]();
    chip.SetVolumeADPCM(0);
    chip.Reset();
    unsigned reg, value;
    char command;
    while (std::scanf(" %c %u %u", &command, &reg, &value) == 3) {
        switch (command) {
        case 'W': chip.SetReg(reg,value); break;
        case 'X': chip.Reset(); break;
        case 'Q': std::printf("%d\n",(chip.status & chip.stmask & chip.reg29) != 0); break;
        case 'D': std::printf("%u\n",chip.GetReg(0x108)); break;
        case 'S': std::printf("%u\n",chip.ReadStatusEx()); break;
        case 'B': std::printf("%u\n",chip.adpcmbuf[reg]); break;
        case 'T': {
            unsigned next = chip.GetNextEvent();
            unsigned elapsed = next && next < value ? next : value;
            chip.csm = false;
            bool event = chip.Count(elapsed);
            std::printf("%u %d %d %u\n",next ? next : ~0u,event,chip.csm,chip.ReadStatus());
            break;
        }
        case 'I': {
            auto &v = chip.rhythm[reg];
            v.sample = new int16[97];
            for (int j=0;j<97;++j) v.sample[j] = int16(j*9001+reg*601);
            v.pos = v.size = 97*1024;
            v.step = value*1024/44100;
            break;
        }
        case 'V': chip.SetVolumeRhythm(reg,int(value)-100); break;
        case 'G': chip.SetVolumeRhythmTotal(int(value)-100); break;
        case 'M': chip.SetChannelMask(value); break;
        case 'P': {
            FM::Sample dest[512] = {};
            chip.ADPCMBMix(dest,value);
            chip.RhythmMix(dest,value);
            for (unsigned i=0;i<value*2;++i) std::printf("%d\n",dest[i]);
            break;
        }
        }
    }
}
