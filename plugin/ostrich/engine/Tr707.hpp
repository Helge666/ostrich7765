// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TR-707 / TR-727 voice board: the MB63H114 address counter and its eight multiplexed
// voices (one 8-bit multiplying DAC, the amplitude envelopes as its reference), the
// hi-hat's envelope and VCA, the two cymbal voices with their own 6-bit DACs, and the mixer
// (band pass, level, low pass, pan per channel; output band pass).
//
// A port of MAME's tr707_audio_device (src/mame/roland/roland_tr707.cpp, BSD-3-Clause,
// copyright-holders AJR and m1macrophage, after Roland's service notes); see THIRD-PARTY.md.
// The CPU side -- which voices fire with which accent -- follows the TR-727's firmware
// (analysis/TR-727.md) instead of an emulated CPU.

#pragma once

#include <array>
#include <cstdint>

#include "Analog.hpp"

namespace ostrich {

class Tr707
{
public:
    // Internal rate of the analogue part: four times the 25 kHz multiplex rate. The address
    // counter runs at 200 kHz, two steps per internal sample.
    static constexpr double kRate = 100000.0;

    // The 15 sounds, in the TR-707's names (the TR-727's sounds use the same slots).
    enum Sound {
        kBD1, kBD2, kSD1, kSD2, kLT, kMT, kHT, kRS, kCB, kHCP, kTAMB, kCH, kOH, kCrash, kRide,
        kNumSounds
    };

    // The mixer's ten channels; each has a level slider and an output of its own.
    enum Channel {
        kChBass, kChSnare, kChLowTom, kChMidTom, kChHiTom, kChRimshot, kChHandclap, kChHiHat,
        kChCrash, kChRide, kNumChannels
    };

    struct Roms {
        const uint8_t* voices = nullptr;  // 64 KiB: IC34, then IC35
        const uint8_t* crash = nullptr;   // 32 KiB: IC19
        const uint8_t* ride = nullptr;    // 32 KiB: IC22
    };

    explicit Tr707(bool tr727);

    void setRoms(const Roms& roms) { roms_ = roms; }
    bool hasRoms() const { return roms_.voices && roms_.crash && roms_.ride; }
    void reset();

    // Start a sound at the next internal sample with a MIDI velocity (1-127). Sounds
    // started in the same sample fire together and share one accent, as on the machine.
    void trigger(int sound, int velocity);

    // Render n internal samples. channels: kNumChannels buffers, or null; each carries its
    // channel after the low pass (as the machine's individual outputs). A channel taken out
    // of the mix (setInMix) still appears there.
    void render(int n, float* left, float* right, float* const* channels);

    void setLevel(int channel, double gain) { level_[channel] = gain; }
    void setInMix(int channel, bool inMix) { inMix_[channel] = inMix; }
    void setMasterVolume(double gain) { master_ = gain; }

    static int soundChannel(int sound);
    // The machine's own note map (analysis/TR-707.md, TR-727.md): a sound, or -1.
    static int noteToSound(bool tr727, int note);

private:
    enum { kMux = 8, kCymbals = 2 };

    void tick();
    void fire();
    void updateEgs(uint16_t oldTriggers);
    void updateHatEg();

    const bool tr727_;
    int xck2Input_ = 3;
    Roms roms_;

    // CPU side: pending sounds for the next fire, the voice-select latch, the accent DAC.
    uint16_t pendingMask_ = 0;
    uint8_t pendingSelect_ = 0, pendingSelectMask_ = 0;
    int pendingAccent_ = -1;
    bool pendingCymbal_ = false;
    int pulseTicks_ = 0;            // ticks left in the trigger pulse
    bool refire_ = false;           // a voice was retriggered within its pulse

    // Voice select ($7000): variation bits, hat closed.
    uint8_t bassVar_ = 0, snareVar_ = 0, rimCow_ = 0, clapTamb_ = 0;
    bool hatClosed_ = false, hatTriggering_ = false;
    double accentV_ = 0.0;

    // MB63H114 and the multiplexed DAC.
    uint16_t triggers_ = 0x3ff;     // active low, bits 0-7 mux voices, 8-9 cymbals
    std::array<uint16_t, kMux> counters_{};
    uint8_t xck_ = 0, macOffset_ = 0, macC_ = 0;
    uint8_t muxSample_ = 0, muxVoice_ = 0;
    std::array<uint8_t, kMux> muxDac_{};
    std::array<uint16_t, kCymbals> cymCounter_{};
    std::array<uint8_t, kCymbals> cymDac_{};

    // Analogue part.
    std::array<RcEg, kMux> muxEg_;
    std::array<RcEg, kCymbals> cymEg_;
    RcEg hatEg_;
    RcFilter hatHpf_;
    std::array<RcFilter, kCymbals> cymHpf_;
    std::array<Biquad, kNumChannels> bpf_;
    std::array<double, kNumChannels> bpfGain_{};
    std::array<RcFilter, kNumChannels> lpf_;
    std::array<double, kNumChannels> mixL_{}, mixR_{};
    std::array<double, kNumChannels> level_{};
    std::array<bool, kNumChannels> inMix_{};
    RcFilter dcL_, dcR_;
    Biquad outL_, outR_;
    double master_ = 0.5;
    double muxScale_ = 0.0;
    std::array<double, kMux> muxEgR_{};
    std::array<double, kCymbals> cymEgR_{};
};

} // namespace ostrich
