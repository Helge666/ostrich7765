// SPDX-License-Identifier: GPL-3.0-or-later

#include "PcmMachine.hpp"

namespace ostrich {

namespace {

struct Region {
    uint32_t offset, length;
};

// The sounds of one 128 KiB bank in sound order (tools/tr505_rom.py, tr626_rom.py).
constexpr Region kTr505Sounds[16] = {
    {0x0000, 0x2000}, {0x2000, 0x2000}, {0x4000, 0x2000}, {0x6000, 0x2000},
    {0x8000, 0x1000}, {0x9000, 0x1000}, {0xA000, 0x1000}, {0xB000, 0x1000},
    {0xC000, 0x1000}, {0xD000, 0x1000}, {0xE000, 0x1000}, {0xF000, 0x1000},
    {0x10000, 0x8000}, {0x18000, 0x4000}, {0x1C000, 0x2000}, {0x1E000, 0x2000},
};
constexpr Region kTr626BankSounds[15] = {
    {0x0000, 0x2000}, {0x2000, 0x2000}, {0x4000, 0x2000}, {0x6000, 0x2000},
    {0x8000, 0x1000}, {0x9000, 0x1000}, {0xA000, 0x1000}, {0xB000, 0x1000},
    {0xC000, 0x1000}, {0xD000, 0x1000}, {0xE000, 0x2000},
    {0x10000, 0x8000}, {0x18000, 0x4000}, {0x1C000, 0x2000}, {0x1E000, 0x2000},
};

// The order in which the unit reads the samples of a sound, against the sound order
// (SoundOrder.cpp, the known decoded dump): sample n at n ^ flip. Measured: with these the
// recordings follow the ROM sample for sample up to 20 kHz (coherence 0.97-0.99; without,
// 0.0-0.3 above 2 kHz). In pin order: the counter's A0 inverted (hi-hats, as for slots
// 0-3), and for the cymbals also the clocks D and 1/2 D on A13 and A14 (analysis/TR-505.md).
constexpr uint32_t kTr505Flip[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 3, 1, 1};

// Accent: level per step (velocity / 2) relative to velocity 127, measured on the bass
// drum (RMS of its first 60 ms); the other sounds follow within about 1 dB.
constexpr float kTr505Accent[64] = {
    0.014f, 0.014f, 0.013f, 0.013f, 0.014f, 0.014f, 0.014f, 0.013f, 0.013f, 0.014f, 0.032f,
    0.062f, 0.102f, 0.145f, 0.191f, 0.237f, 0.280f, 0.322f, 0.364f, 0.402f, 0.439f, 0.475f,
    0.508f, 0.538f, 0.564f, 0.590f, 0.612f, 0.631f, 0.652f, 0.669f, 0.687f, 0.704f, 0.717f,
    0.733f, 0.747f, 0.760f, 0.774f, 0.786f, 0.800f, 0.812f, 0.822f, 0.834f, 0.845f, 0.855f,
    0.867f, 0.874f, 0.884f, 0.893f, 0.901f, 0.909f, 0.917f, 0.925f, 0.932f, 0.939f, 0.947f,
    0.954f, 0.960f, 0.966f, 0.972f, 0.978f, 0.984f, 0.989f, 0.995f, 1.000f,
};
constexpr float kTr626Accent[64] = {
    0.021f, 0.021f, 0.018f, 0.018f, 0.018f, 0.018f, 0.018f, 0.018f, 0.019f, 0.019f, 0.039f,
    0.071f, 0.107f, 0.147f, 0.186f, 0.225f, 0.264f, 0.304f, 0.333f, 0.370f, 0.400f, 0.424f,
    0.451f, 0.476f, 0.503f, 0.531f, 0.549f, 0.572f, 0.592f, 0.613f, 0.633f, 0.655f, 0.668f,
    0.686f, 0.704f, 0.717f, 0.735f, 0.748f, 0.761f, 0.776f, 0.792f, 0.804f, 0.815f, 0.829f,
    0.838f, 0.850f, 0.860f, 0.871f, 0.883f, 0.890f, 0.901f, 0.911f, 0.920f, 0.929f, 0.936f,
    0.946f, 0.952f, 0.961f, 0.967f, 0.973f, 0.982f, 0.989f, 0.995f, 1.000f,
};

// Sounds that cut another one off: the closed hi-hat the open one. Not yet measured on
// the units (ROADMAP.md); the usual behaviour of Roland's drum machines.
constexpr int kTr505Cuts[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 14};
int tr626Cuts(int sound) { return sound == 25 ? 10 : -1; }

} // namespace

PcmMachine::PcmMachine(Model model) : model_(model), numSounds_(model == kTr505 ? 16 : 30)
{
    for (int s = 0; s < numSounds_; ++s) {
        const Region r = model == kTr505 ? kTr505Sounds[s] : kTr626BankSounds[s % 15];
        voices_[s].offset = r.offset + (model == kTr626 && s >= 15 ? 0x20000 : 0);
        voices_[s].length = r.length;
        voices_[s].flip = model == kTr505 ? kTr505Flip[s] : 0;
        voices_[s].step = kSampleRate / kRate;
    }
}

void PcmMachine::reset()
{
    for (auto& v : voices_) {
        v.playing = false;
        v.pos = 0.0;
    }
}

void PcmMachine::trigger(int sound, int velocity)
{
    if (sound < 0 || sound >= numSounds_ || velocity <= 0)
        return;
    const int step = (velocity > 127 ? 127 : velocity) >> 1;
    Voice& v = voices_[sound];
    v.gain = (model_ == kTr505 ? kTr505Accent : kTr626Accent)[step];
    v.pos = 0.0;
    v.playing = true;
    const int cut = model_ == kTr505 ? kTr505Cuts[sound] : tr626Cuts(sound);
    if (cut >= 0)
        voices_[cut].playing = false;
}

void PcmMachine::render(int n, float* left, float* right)
{
    for (int i = 0; i < n; ++i)
        left[i] = right[i] = 0.0f;
    if (!rom_)
        return;
    for (int s = 0; s < numSounds_; ++s) {
        Voice& v = voices_[s];
        if (!v.playing)
            continue;
        const uint8_t* data = rom_ + v.offset;
        const float g = v.gain / 128.0f;
        for (int i = 0; i < n; ++i) {
            const uint32_t k = uint32_t(v.pos);
            if (k >= v.length) {
                v.playing = false;
                break;
            }
            const float x = (float(data[k ^ v.flip]) - 128.0f) * g;
            left[i] += x;
            right[i] += x;
            v.pos += v.step;
        }
    }
}

int PcmMachine::noteToSound(Model model, int note)
{
    if (model == kTr505) {
        switch (note) {
        case 35: return 4;   // bass drum
        case 37: return 10;  // rim shot
        case 38: return 6;   // snare drum
        case 39: return 8;   // hand clap
        case 41: return 1;   // low tom
        case 42: return 15;  // closed hi-hat
        case 45: return 3;   // mid tom
        case 46: return 14;  // open hi-hat
        case 48: return 2;   // high tom
        case 49: return 12;  // crash
        case 51: return 13;  // ride
        case 62: return 7;   // high conga
        case 63: return 5;   // low conga
        case 65: return 0;   // timbale
        case 67: return 11;  // high cowbell
        case 68: return 9;   // low cowbell
        default: return -1;
        }
    }
    switch (note) {
    case 35: return 9;   // bass drum 1
    case 36: return 24;  // bass drum 2
    case 37: return 8;   // rim shot
    case 38: return 13;  // snare drum 1
    case 39: return 4;   // hand clap
    case 40: return 14;  // snare drum 2
    case 41: return 1;   // low tom 1
    case 42: return 25;  // closed hi-hat
    case 43: return 16;  // low tom 2
    case 45: return 3;   // mid tom 1
    case 46: return 10;  // open hi-hat
    case 47: return 18;  // mid tom 2
    case 48: return 2;   // high tom 1
    case 49: return 11;  // crash
    case 50: return 17;  // high tom 2
    case 51: return 12;  // ride
    case 52: return 26;  // china cymbal
    case 53: return 27;  // cup
    case 54: return 7;   // tambourine
    case 56: return 5;   // cowbell
    case 58: return 23;  // snare drum 3
    case 62: return 19;  // mute high conga
    case 63: return 0;   // open high conga
    case 64: return 15;  // low conga
    case 65: return 29;  // high timbale
    case 66: return 28;  // low timbale
    case 67: return 22;  // high agogo
    case 68: return 20;  // low agogo
    case 69: return 21;  // shaker
    case 75: return 6;   // claves
    default: return -1;
    }
}

} // namespace ostrich
