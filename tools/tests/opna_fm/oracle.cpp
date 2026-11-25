// Host oracle. Static storage gives the original fmgen constructors zeroed RAM.
#include "fmgen_headers.h"
#include "fmgen_misc.h"
#define protected public
#include "fmgen_opna.h"
#undef protected
#include "fmgen_fmgeninl.h"

static FM::OPNA synth;
static FM::Channel4 *channel;
extern "C" void setup(unsigned p, unsigned algo, unsigned fb, unsigned shape) {
    synth.OPNABase::SetRate(7987200, 44100, false);
    synth.SetPrescaler(p);
    synth.SetVolumeFM(0);
    channel = &synth.ch[0];
    channel->SetChip(&synth.chip);
    channel->SetType(FM::typeN);
    channel->KeyControl(0);
    channel->Reset();
    channel->SetAlgorithm(algo);
    channel->SetFB(fb);
    channel->SetMS(0x37);
    channel->SetFNum(0x37e3);
    for (unsigned i = 0; i < 4; ++i) {
        auto &op = channel->op[i];
        op.SetDT(i+3); op.SetDT2(i); op.SetMULTI(i*5);
        op.SetTL(i*3, false); op.SetKS(i); op.SetAR(48+i*5);
        op.SetDR(53); op.SetSR(55); op.SetSL(12+i*4); op.SetRR(50);
        op.SetSSGEC(shape); op.SetAMON(i!=1);
    }
    channel->Prepare();
    channel->KeyControl(15);
}
extern "C" int tick(unsigned n, unsigned mode) {
    if (n==701) channel->KeyControl(5);
    if (n==812) channel->KeyControl(15);
    if (n==911) channel->SetKCKF(101,37);
    if (n==911) channel->Prepare();
    synth.chip.SetPML(n*7); synth.chip.SetAML(n*3);
    switch(mode) {
        case 0: return channel->Calc();
        case 1: return channel->CalcL();
        case 2: return channel->CalcN(n);
        default: return channel->CalcLN(n);
    }
}

static FM::OPNA mixer;
extern "C" void setup_mix() {
    mixer.OPNABase::SetRate(7987200, 44100, false);
    mixer.SetVolumeFM(0);
    mixer.BuildLFOTable();
    mixer.lfocount = 0xffff0000u;
    mixer.lfodcount = 45123;
    for (unsigned i = 0; i < 6; ++i) {
        auto &ch = mixer.ch[i];
        ch.SetChip(&mixer.chip);
        ch.SetType(FM::typeN);
        ch.Reset();
        ch.SetAlgorithm(7);
        ch.SetFB(i);
        ch.SetMS(0x37);
        mixer.fnum[i] = 0x2830 + i*11;
        mixer.pan[i] = (i+1)%4;
        ch.SetFNum(mixer.fnum[i]);
        for (auto &op : ch.op) {
            op.SetAR(63); op.SetTL(0,false); op.SetMULTI(4); op.SetAMON(true);
        }
        ch.Prepare();
        ch.KeyControl(15);
    }
    for (unsigned i=0; i<3; ++i) mixer.fnum3[i]=0x3800+i*51;
}
extern "C" void mix_block(unsigned mode, int *out) {
    mixer.regtc = mode&1 ? 0xc0 : 0;
    mixer.reg22 = mode&2 ? 8 : 0;
    mixer.reg29 = mode&4 ? 0x80 : 0;
    FM::Sample pcm[512] = {};
    mixer.FMMix(pcm,256);
    for (int i=0;i<512;++i) out[i]=pcm[i];
}
