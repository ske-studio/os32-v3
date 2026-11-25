// Same unsigned register trace drives the linked C ABI and the bundled fmgen.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "fmgen_headers.h"
#define private public
#define protected public
#include "fmgen_opna.h"
#undef private
#undef protected
#include "opna.h"

extern "C" void synth_panic() { std::abort(); }
extern "C" void rust_eh_personality() { std::abort(); }
static FM::OPNA chip;
alignas(OPNA_STATE_ALIGN) static unsigned char state[OPNA_STATE_BYTES];
static unsigned char ram[OPNA_ADPCM_BYTES];
static unsigned char waves[6][194];
static unsigned sequence;

static void write_reg(unsigned reg, unsigned data) {
    assert(opna_write(state, reg, data) == 0);
    chip.SetReg(reg, data);
}

static void render(unsigned frames) {
    unsigned char bytes[1024];
    FM::Sample reference[512] = {};
    std::memset(bytes, 0xef, sizeof bytes);
    chip.Mix(reference, frames);
    assert(opna_render(state, bytes, frames) == 0);
    for (unsigned i = 0; i < frames * 2; ++i) {
        int actual = int16_t(bytes[2*i] | bytes[2*i+1] << 8);
        int expected = std::clamp(int(reference[i]), -32768, 32767);
        if (actual != expected) {
            std::fprintf(stderr, "PCM block %u sample %u: %d != %d\n", sequence, i, actual, expected);
            std::exit(1);
        }
    }
    ++sequence;
}

static void count(unsigned elapsed) {
    unsigned irq, next;
    assert(opna_count(state, elapsed, &irq) == 0);
    while (elapsed) {
        unsigned deadline = chip.GetNextEvent();
        unsigned step = std::min({elapsed, deadline ? deadline : ~0u, 32767u});
        chip.Count(step); // real virtual OPNBase::TimerA toggles channel 3's keys
        elapsed -= step;
    }
    assert(opna_next_event(state, &next) == 0);
    unsigned expected = chip.GetNextEvent();
    if (next != (expected ? expected : ~0u)) {
        std::fprintf(stderr,"timer deadline %u != %u\n",next,expected);
        std::exit(1);
    }
    assert(irq == unsigned((chip.status & chip.reg29 & chip.stmask) != 0));
}

static void status() {
    for (unsigned bank : {0u,1u}) {
        unsigned value;
        assert(!opna_status(state, bank, &value));
        unsigned expected = bank ? chip.ReadStatusEx() : chip.ReadStatus();
        if (value != expected) {
            std::fprintf(stderr,"status %u != %u\n",value,expected);
            std::exit(1);
        }
    }
}

static void voice(unsigned addr, unsigned pan) {
    for (unsigned slot : {0u,4u,8u,12u}) {
        write_reg(0x30+addr+slot, 2+slot);
        write_reg(0x40+addr+slot, 5+slot);
        write_reg(0x50+addr+slot, 31);
        write_reg(0x60+addr+slot, 0x83);
        write_reg(0x70+addr+slot, 2);
        write_reg(0x80+addr+slot, 0x17);
    }
    write_reg(0xb0+addr, 0x1f);
    write_reg(0xb4+addr, pan|0x37);
    write_reg(0xa4+addr, 0x26);
    write_reg(0xa0+addr, 0xa1);
}

int main() {
    chip.adpcmbuf = new uint8[OPNA_ADPCM_BYTES]();
    chip.SetRate(7987200,44100,false);
    chip.OPNABase::Init(7987200,44100,false);
    chip.Reset();
    chip.SetVolumeADPCM(0);
    assert(!opna_init(state,sizeof state,ram,sizeof ram));
    assert(opna_init(state,sizeof state-1,ram,sizeof ram) == -9);
    assert(opna_write(state,512,0) == -9 && opna_write(state,8,256) == -9);
    assert(opna_render(state,nullptr,0) == 0 && opna_render(state,nullptr,257) == -9);
    write_reg(0x29, 0x9f);
    voice(2, 0x80);
    for (unsigned i=0; i<3; ++i) {
        write_reg(0xac+i, 0x26+i);
        write_reg(0xa8+i, 0x91+i);
    }
    write_reg(0x26, 0);
    write_reg(0x27, 10);
    count(32768);
    write_reg(0x24, 253);
    write_reg(0x25, 0);
    write_reg(0x27, 0x8f);
    for (unsigned delay : {1u, 222u, 10u, 217u, 65536u}) {
        count(delay);
        render(143);
        status();
    }
    voice(0x101, 0x40);
    write_reg(0x28, 0xf5);
    write_reg(0x22, 15);
    write_reg(0, 49);
    write_reg(7, 0x3e);
    write_reg(8, 12);
    for (unsigned r=0; r<512; ++r) {
        unsigned value;
        assert(!opna_read(state,r,&value) && value == chip.GetReg(r));
    }
    for (unsigned i=0; i<6; ++i) {
        auto &v = chip.rhythm[i];
        v.sample = new int16[97];
        for (unsigned j=0; j<97; ++j) {
            unsigned word = j*8123+i*777;
            v.sample[j] = int16(word);
            waves[i][2*j] = word;
            waves[i][2*j+1] = word >> 8;
        }
        v.size = v.pos = 97*1024;
        v.step = 1024;
        assert(!opna_rhythm(state,i,waves[i],97,44100));
        write_reg(0x18+i, 0xff-i);
    }
    write_reg(0x11,63);
    write_reg(0x10,63);
    write_reg(0x101,0xc0);
    write_reg(0x104,15);
    write_reg(0x100,0x60);
    for (unsigned i=0;i<64;++i) write_reg(0x108,(i*91+23)&255);
    write_reg(0x110,128);
    status(); // drain deferred status from RAM writes
    write_reg(0x110,128);
    write_reg(0x110,4); // mask EOS during playback, then unmask after completion
    write_reg(0x10a,95);
    write_reg(0x10b,175);
    write_reg(0x100,0xa0);
    for (unsigned p : {0u,1u,2u}) {
        write_reg(0x2d+p,0);
        write_reg(0x22,9+p);
        for (unsigned frames : {1u, 97u, 256u}) render(frames);
    }
    write_reg(0x110,0);
    status();
    // Standalone timer and audio callback follow the same real TimerA path.
    alignas(OPNA_TIMER_ALIGN) unsigned char timer[OPNA_TIMER_BYTES] = {};
    for (auto reg : {0x24, 0x25, 0x27}) {
        unsigned data = reg == 0x24 ? 253 : reg == 0x27 ? 0x85 : 0;
        assert(!opna_timer_write(timer, reg, data));
    }
    unsigned event;
    assert(!opna_timer_count(timer, 1, &event) && event == 0);
    assert(!opna_timer_count(timer, 217, &event) && event == 1);
    assert(!opna_apply_csm(state));
    chip.TimerA();
    render(37);

    chip.Reset();
    assert(!opna_reset(state));
    write_reg(0x10,1);
    render(27); // reset retains rhythm pan/levels and source
    write_reg(0x10,1);
    write_reg(0x18,255);
    write_reg(0x11,63);
    render(97);
    assert(!std::memcmp(ram,chip.adpcmbuf,sizeof ram));

    unsigned char fast_wave[] = {0xff, 0x7f, 0xff, 0x7f};
    unsigned char fast_out[8];
    assert(!opna_rhythm(state,0,fast_wave,2,4194304));
    write_reg(0x10,1);
    assert(!opna_render(state,fast_out,2));
    assert(fast_out[0] || fast_out[1]);
    assert(!fast_out[4] && !fast_out[5]);
    // Largest accepted voice, nonzero endpoints expose a wrapped playhead.
    const unsigned frames = ~0u / 1024;
    std::vector<unsigned char> wave(2*frames, 1);
    assert(!opna_rhythm(state,0,wave.data(),frames,88200));
    write_reg(0x10,1);
    unsigned char bytes[1024];
    for (unsigned played=0; played<(frames+1)/2; played+=256)
        assert(!opna_render(state,bytes,256));
    assert(!opna_render(state,bytes,256));
    for (auto v : bytes) if (v) { std::fprintf(stderr,"rhythm endpoint wrapped\n"); return 1; }
}
