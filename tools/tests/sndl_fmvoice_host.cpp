// Host-only access to the original fmgen rhythm sample slots.
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "fmgen_headers.h"
#define private public
#define protected public
#include "fmgen_opna.h"
#undef private
#undef protected
extern "C" {
#include "fmvoice.h"
void synth_panic() { std::abort(); }
void rust_eh_personality() { std::abort(); }
}

static unsigned allocated, rendered;
static std::vector<unsigned> events, positions;
static void *allocate(uint32_t n) { ++allocated; return std::malloc(n); }
static void release(void *p) { --allocated; std::free(p); }

extern "C" int __real_opna_render(void *, unsigned char *, unsigned);
extern "C" int __wrap_opna_render(void *s, unsigned char *p, unsigned n) {
    rendered += n;
    return __real_opna_render(s,p,n);
}
extern "C" int __real_opna_write(void *, unsigned, unsigned);
extern "C" int __wrap_opna_write(void *s, unsigned r, unsigned d) {
    events.push_back(r*256+d);
    positions.push_back(rendered);
    return __real_opna_write(s,r,d);
}
extern "C" int __real_opna_apply_csm(void *);
extern "C" int __wrap_opna_apply_csm(void *s) {
    events.push_back(512*256);
    positions.push_back(rendered);
    return __real_opna_apply_csm(s);
}

static void timing(uint32_t base) {
    auto t = [base](unsigned us) { return base+us; };
    FmVoice *v;
    assert(!fmv_new(&v,{allocate,release},t(0),0,nullptr));
    unsigned char pcm[4096];
    assert(!fmv_render(v,t(100),pcm,1));
    rendered = 0;
    positions.clear();
    events.clear();
    for (unsigned at : {201,150,99,100,150,200})
        assert(!fmv_write(v,t(at),8,at%16));
    assert(!fmv_render(v,t(200),pcm,4));
    assert((positions == std::vector<unsigned>{0,0,2,2,3}));
    assert(!fmv_render(v,t(201),pcm,1));
    assert(positions.back()==4 && positions.size()==6);
    fmv_reset(v,t(0));
    positions.clear();
    events.clear();
    assert(!fmv_write(v,t(19),0x27,0x10));
    assert(!fmv_write(v,t(0),0x24,255));
    assert(!fmv_write(v,t(0),0x25,3));
    assert(!fmv_write(v,t(0),0x27,0x85));
    unsigned x;
    assert(!fmv_status(v,t(18),0,&x) && x==0);
    assert(!fmv_status(v,t(19),0,&x) && x==0);
    assert(!fmv_status(v,t(19),0,&x) && x==0);
    assert(events.empty());
    assert(!fmv_write(v,t(5),0x108,43));
    assert(!fmv_write(v,t(24),8,13));
    assert(!fmv_read(v,t(20),8,&x) && x==0);
    assert((events == std::vector<unsigned>{0x2780,512*256,0x2700,0x1082b}));
    assert(!fmv_render(v,t(23),pcm,513));
    assert(events.size()==4);
    assert(!fmv_render(v,t(24),pcm,1));
    assert(events.size()==5 && events.back()==0x080d);
    fmv_delete(v);
}

static void synchronized_ram() {
    FmVoice *v;
    assert(!fmv_new(&v,{allocate,release},0,0,nullptr));
    unsigned x;
    unsigned char pcm[16];
    assert(!fmv_write(v,0,0x101,0));
    assert(!fmv_write(v,0,0x104,15));
    assert(!fmv_write(v,0,0x100,0x60));
    assert(!fmv_write(v,0,0x108,0x49));
    assert(!fmv_status(v,10,1,&x));
    assert(!fmv_write(v,3,0x108,0x82));
    assert(!fmv_render(v,11,pcm,4));
    assert(!fmv_write(v,12,0x108,0x37));
    assert(!fmv_write(v,12,0x102,0));
    assert(!fmv_write(v,12,0x100,0x20));
    // Two pipeline reads, then three consecutive RAM bytes.
    for (unsigned expected : {0,0,0x49,0x82,0x37,0}) {
        assert(!fmv_read(v,12,0x108,&x) && x==expected);
        assert(!fmv_render(v,12,pcm,4));
    }
    fmv_delete(v);
}

static void deadlines() {
    FmVoice *v;
    assert(!fmv_new(&v,{allocate,release},0,0,nullptr));
    unsigned x;
    unsigned char pcm[16];
    assert(!fmv_render(v,0,pcm,4));
    assert(!fmv_write(v,0,0x24,255));
    assert(!fmv_write(v,0,0x25,3));
    assert(!fmv_write(v,0,0x27,0x85));
    assert(!fmv_status(v,18,0,&x) && x==0);
    assert(!fmv_status(v,19,0,&x) && x==1);
    assert(!fmv_write(v,2,0x27,0x10)); // late stop after CSM
    assert(!fmv_status(v,19,0,&x) && x==0);
    assert(!fmv_write(v,3,0x27,0x85)); // late restart at tm_us=19
    assert(!fmv_status(v,37,0,&x) && x==0);
    events.clear();
    positions.clear();
    rendered = 0;
    assert(!fmv_render(v,38,pcm,4));
    assert((positions == std::vector<unsigned>{0,2,2,2,3}));
    assert((events == std::vector<unsigned>{0x2780,512*256,0x2700,0x2780,512*256}));
    assert(!fmv_status(v,38,0,&x) && x==1);
    fmv_delete(v);

    assert(!fmv_new(&v,{allocate,release},0,1,nullptr));
    assert(!fmv_write(v,0,0x27,0x85));
    assert(!fmv_csm(v,1));
    assert(!fmv_write(v,1,0x27,0x10));
    events.clear();
    assert(!fmv_render(v,100000,pcm,4));
    assert((events == std::vector<unsigned>{0x2780,512*256,0x2700}));
    fmv_delete(v);
}

static int short_at = -1;
static int read_at(void *file, uint32_t at, void *dst, uint32_t n) {
    if (int(at)==short_at) return n-1;
    std::memcpy(dst,static_cast<unsigned char *>(file)+at,n);
    return n;
}

static void waves() {
    static FM::OPNA chip;
    chip.adpcmbuf = new uint8[0x40000]();
    chip.SetRate(7987200,44100,false);
    chip.OPNABase::Init(7987200,44100,false);
    chip.Reset();
    unsigned char bytes[6][62] = {};
    FmvSample samples[6];
    for (unsigned i=0; i<6; ++i) {
        unsigned char header[] = {'R','I','F','F',54,0,0,0,'W','A','V','E',
            'J','U','N','K',1,0,0,0,42,0,'f','m','t',' ',16,0,0,0,
            1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',8,0,0,0};
        auto p = bytes[i];
        std::memcpy(p,header,sizeof header);
        auto &r = chip.rhythm[i];
        r.sample = new int16[4];
        for (unsigned j=0;j<4;++j) {
            int sample = (j%2 ? -1 : 1)*int(1000+i*2021+j*531);
            r.sample[j] = sample;
            p[54+2*j] = sample;
            p[55+2*j] = unsigned(sample)>>8;
        }
        r.size = r.pos = 4096;
        r.step = 1024;
        samples[i] = {p,62,read_at};
    }
    for (bool missing : {false,true}) {
        FmVoice *v;
        if (missing) samples[5].read_at = nullptr;
        assert(!fmv_new(&v,{allocate,release},0,0,samples));
        fmv_reset(v,0);
        auto wr = [&](unsigned r, unsigned d) {
            chip.SetReg(r,d);
            assert(!fmv_write(v,0,r,d));
        };
        for (unsigned i=0;i<6;++i) {
            wr(0x18+i,255);
            wr(0x11,63);
            wr(0x10,1u<<i);
            FM::Sample ref[8] = {};
            chip.Mix(ref,4);
            unsigned char pcm[16];
            assert(!fmv_render(v,0,pcm,4));
            assert(ref[0]!=0);
            for (unsigned j=0;j<8;++j)
                assert(int16_t(pcm[2*j]|pcm[2*j+1]<<8)==(missing ? 0 : ref[j]));
        }
        fmv_delete(v);
    }
    samples[5].read_at = read_at;
    auto invalid = [&](unsigned offset,unsigned value,int expected) {
        FmVoice *v = nullptr;
        auto &b = bytes[5][offset];
        auto saved = b;
        b = value;
        assert(fmv_new(&v,{allocate,release},0,0,samples)==expected && !v);
        assert(!allocated);
        b = saved;
    };
    invalid(8,'X',-9);
    invalid(4,13,-9); // odd JUNK ends at RIFF boundary without pad
    invalid(50,10,-9);
    invalid(32,2,-9);
    invalid(44,8,-9);
    short_at = 54;
    invalid(8,'W',-1);
}

int main() {
    timing(0);
    timing(~80u);
    synchronized_ram();
    deadlines();
    waves();
    assert(!allocated);
}
