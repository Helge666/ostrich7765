// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TR-505 and TR-626: 8-bit sounds from the sound ROM at 25 kHz, each held until the
// next (as the DAC holds it), scaled by the accent measured on the owner's units
// (analysis/TR-505.md, TR-626.md), with each unit's envelopes, output levels and pan, and
// what its analogue stages add (envelope pulse; the TR-505's centre bus limit).
//
// Runs at 100 kHz, as the TR-707 engine, so that both share the resampler and the analogue
// models to come.

#pragma once

#include <array>
#include <cstdint>

namespace ostrich {

class PcmMachine
{
public:
    enum Model { kTr505, kTr626 };

    static constexpr double kRate = 100000.0;
    static constexpr double kSampleRate = 25000.0;   // measured, both units (within 0.5 %)
    static constexpr int kMaxSounds = 30;

    explicit PcmMachine(Model model);

    Model model() const { return model_; }
    int numSounds() const { return numSounds_; }
    // A sound's length in ROM samples (its region; the end may be silence).
    int soundLength(int sound) const { return int(voices_[sound].length); }

    // The ROM in sound order (SoundOrder.hpp): 128 KiB (TR-505) or 256 KiB (TR-626).
    void setRom(const uint8_t* soundOrder) { rom_ = soundOrder; }
    bool hasRom() const { return rom_ != nullptr; }
    void reset();

    // Start a sound at the next internal sample with a MIDI velocity (1-127).
    void trigger(int sound, int velocity);

    // Pitch of a sound as a factor of its rate (1 = the ROM's 25 kHz).
    void setPitch(int sound, double factor) { voices_[sound].step = factor * kSampleRate / kRate; }
    // The TR-626's pitch setting of a sound (-7..+7, its manual) as such a factor.
    static double tr626PitchFactor(int step);
    // A level of our own per sound (the units have none): a gain, 1 = as measured.
    void setLevel(int sound, float gain) { voices_[sound].level = gain; }

    // Render n internal samples into the stereo mix.
    void render(int n, float* left, float* right);

    // The unit's own note map (measured, and the same in its manual): a sound, or -1.
    static int noteToSound(Model model, int note);

private:
    struct Voice {
        uint32_t offset = 0, length = 0;
        uint32_t flip = 0;            // sample n is read at n ^ flip (see kTr505Flip)
        double env = 1.0, decay = 1.0;   // envelope (see kTr505Tau, kTr626Tau): exp(-t / tau) per sample
        float v0 = 0.0f;
        uint32_t hold = 0, holdLeft = 0;  // internal samples before the decay starts
        float outL = 1.0f, outR = 1.0f;   // gain at the two outputs (kTr505Out, kTr626Out)
        float pulse = 0.0f;               // envelope pulse in sample units (kTr505Pulse, kTr626Pulse)
        bool bus = false;                 // on the TR-505's centre bus (kTr505BusClip)
        float busIn = 1.0f;
        double pos = 0.0, step = 0.25;
        float gain = 0.0f;
        float level = 1.0f;               // setLevel
        bool playing = false;
    };

    const Model model_;
    const int numSounds_;
    const uint8_t* rom_ = nullptr;
    std::array<Voice, kMaxSounds> voices_;
    float hpXL_ = 0.0f, hpYL_ = 0.0f, hpXR_ = 0.0f, hpYR_ = 0.0f;   // output high pass
    static constexpr int kBlock = 512;
    std::array<float, kBlock> bus_{};                                 // centre bus, one block
    void renderBlock(int n, float* left, float* right);
};

} // namespace ostrich
