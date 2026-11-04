// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TR-505 and TR-626: 8-bit sounds from the sound ROM at 25 kHz, each held until the
// next (as the DAC holds it), scaled by the accent measured on the owner's units
// (analysis/TR-505.md, TR-626.md). The analogue stages after the DAC follow in phase 3.
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

    // Render n internal samples into the stereo mix (TR-626: mono for now, both sides
    // the same).
    void render(int n, float* left, float* right);

    // The unit's own note map (measured, and the same in its manual): a sound, or -1.
    static int noteToSound(Model model, int note);

private:
    struct Voice {
        uint32_t offset = 0, length = 0;
        uint32_t flip = 0;            // sample n is read at n ^ flip (see kTr505Flip)
        double env = 1.0, decay = 1.0;   // envelope (see kTr505Tau): exp(-t / tau) per sample
        float v0 = 0.0f;
        uint32_t hold = 0, holdLeft = 0;  // internal samples before the decay starts
        float outL = 1.0f, outR = 1.0f;   // gain at the two outputs (kTr505Out)
        double pos = 0.0, step = 0.25;
        float gain = 0.0f;
        bool playing = false;
    };

    const Model model_;
    const int numSounds_;
    const uint8_t* rom_ = nullptr;
    std::array<Voice, kMaxSounds> voices_;
};

} // namespace ostrich
