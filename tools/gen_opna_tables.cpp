// Host-only adapter. Expose fmgen's tables in this translation unit.
#include <cstdio>
#include <type_traits>
#include <string>
#include "fmgen_headers.h"
#define private public
#define protected public
#include "fmgen_opna.h"
#undef protected
#undef private

namespace FM {
extern int pmtable[2][8][256];
extern uint amtable[2][4][256];
void MakeLFOTable();
}

template<class T>
void values(const T& item) {
    if constexpr (std::is_array_v<T>) {
        for (const auto& child : item) values(child);
    } else {
        std::printf(" %lld", static_cast<long long>(item));
    }
}

template<class T>
void emit(const std::string& name, const T& data) {
    using Element = std::remove_all_extents_t<T>;
    std::printf("%s %c%zu", name.c_str(), std::is_signed_v<Element> ? 'i' : 'u',
                sizeof(Element) * 8);
    values(data);
    std::puts("");
}

int main() {
    using namespace FM;
    static OPNA chip;
    chip.OPNABase::SetRate(7987200, 44100, false);
    chip.SetVolumeFM(0);
    chip.SetVolumePSG(0);
    chip.SetVolumeADPCM(0);
    MakeLFOTable();
    emit("SINE", Operator::sinetable);
    emit("LOG_TO_LINEAR", Operator::cltable);
    emit("NOTE", Operator::notetable);
    emit("DETUNE", Operator::dttable);
    emit("ATTACK", Operator::attacktable);
    emit("DECAY", Operator::decaytable1);
    emit("DECAY_RATE", Operator::decaytable2);
    emit("SSG_ENVELOPE", Operator::ssgenvtable);
    emit("FEEDBACK", Channel4::fbtable);
    emit("KEY_FRACTION", Channel4::kftable);
    emit("FM_PM", FM::pmtable);
    emit("FM_AM", FM::amtable);
    emit("OPNA_PM", chip.pmtable);
    emit("OPNA_AM", chip.amtable);
    emit("TOTAL_LEVEL", chip.tltable);
    emit("PSG_VOLUME", PSG::EmitTable);
    emit("PSG_NOISE", PSG::noisetable);
    emit("PSG_ENVELOPE", PSG::enveloptable);
    emit("FM_GAIN", chip.fmvolume);
    const auto gain = chip.adpcmvol;
    emit("ADPCM_GAIN", gain);
    // At 0 dB the gain is unity in Q16. Register 0x10b scales the output.
    for (unsigned level : {0u, 1u, 128u, 255u}) {
        chip.SetReg(0x10b, level);
        if (gain != 65536 || chip.adpcmvol != gain ||
            chip.adpcmvolume != (gain * static_cast<int>(level)) / 4096) {
            std::fputs("ADPCM gain/level mismatch\n", stderr);
            return 1;
        }
    }
    emit("ADPCM_BASE", chip.adplbase);
    for (unsigned p = 0; p < 3; ++p) {
        chip.SetPrescaler(p);
        auto suffix = "_P" + std::to_string(p);
        emit("MULTIPLE" + suffix, chip.chip.multable_);
        emit("RATIO" + suffix, chip.chip.ratio_);
        emit("LFO_STEP" + suffix, chip.lfotable);
        emit("TIMER_STEP" + suffix, chip.timer_step);
        emit("TONE_BASE" + suffix, chip.psg.tperiodbase);
        emit("ENV_BASE" + suffix, chip.psg.eperiodbase);
        emit("NOISE_BASE" + suffix, chip.psg.nperiodbase);
    }
}
