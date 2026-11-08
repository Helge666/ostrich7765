// SPDX-License-Identifier: GPL-3.0-or-later

#include "PcmMachine.hpp"

#include <algorithm>
#include <cmath>

namespace ostrich {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

// TR-505: the channel of the address counter each sound plays on (documented, service
// notes Table 1); a sound cuts off any other sound of its channel.
constexpr int kTr505Channel[16] = {2, 2, 2, 2, 3, 4, 5, 4, 7, 6, 7, 6, 1, 1, 0, 0};

// TR-505: the envelope. Flat for kTr505Hold, then g = (exp(-t / tau) - v0) / (1 - v0),
// silent from g = 0: a capacitor per channel discharging, the transistor VCA closing
// below a threshold. Measured (tools/measure/envelope.py, the recordings at velocity
// 127): one hold and one v0 for all sounds, tau per sound, within 0.2-0.8 dB rms. tau
// goes with the documented capacitors (C20-C27: 0.022, 0.047, 0.1 uF; about 110, 260 and
// 600 ms); the bass drum, too short to measure, takes the other 4 KiB sounds' value
// (derived).
constexpr double kTr505Hold = 0.035;
constexpr float kTr505V0 = 0.18f;
constexpr float kTr505Tau[16] = {
    0.258f, 0.264f, 0.264f, 0.264f,   // timbale, low, high, mid tom
    0.112f, 0.107f, 0.135f, 0.100f,   // bass drum, low conga, snare, high conga
    0.123f, 0.107f, 0.118f, 0.107f,   // hand clap, low cowbell, rim shot, high cowbell
    0.580f, 0.608f, 0.636f, 0.210f,   // crash, ride, open hi-hat, closed hi-hat
};

// TR-505: each sound's gain at the left and right outputs (relative to the loudest),
// measured (tools/measure/levels.py: the owner's unit, first 30 ms against the ROM data,
// broadband). The right output read 1.2 dB high on the sounds the service notes put on the
// centre bus (BD, SD, congas, cowbells, clap, rim: R102 = R103); not the recorder (its two
// inputs linked as a pair), most likely the two gangs of the volume pot VR2 on the
// owner's unit. Taken out: the table is the nominal unit.
constexpr float kTr505Out[16][2] = {
    {0.469f, 0.484f},   // timbale
    {0.872f, 0.493f},   // low tom
    {0.483f, 0.842f},   // high tom
    {0.873f, 0.887f},   // mid tom
    {0.785f, 0.747f},   // bass drum
    {0.840f, 0.818f},   // low conga
    {1.000f, 0.916f},   // snare
    {0.858f, 0.838f},   // high conga
    {0.489f, 0.502f},   // hand clap
    {0.585f, 0.599f},   // low cowbell
    {0.508f, 0.527f},   // rim shot
    {0.577f, 0.595f},   // high cowbell
    {0.364f, 0.482f},   // crash
    {0.674f, 0.441f},   // ride
    {0.288f, 0.628f},   // open hi-hat
    {0.281f, 0.608f},   // closed hi-hat
};

// TR-505: a pulse with every hit that follows the envelope (the VCA transistor's quiescent
// current), as a fraction of the sound's full scale at 47k: measured (tools/measure/
// vca_fit.py: the low frequencies most sounds have in the recordings beyond the ROM data,
// band shares 40 Hz-5 kHz from 3.7 to 1.8 dB rms on average). Larger against the signal
// the larger the channel's documented series resistor (kOhm), the signal at the
// transistor's base the smaller: scaled by (R + 2.2k) / (47k + 2.2k) (derived).
constexpr double kTr505Pulse = 0.05;
constexpr double kTr505Series[16] = {47, 47, 47, 47, 47, 47, 33, 47, 100, 82, 100, 82, 47, 47, 47, 47};
// TR-505: the centre bus. Channels 3-7 (BD, congas, SD, cowbells, clap / rim) share one
// summing amplifier (IC16b, documented), each in by its series resistor (busIn, relative to
// 47k); the sum is limited at kTr505BusClip, then goes to both outputs (R102 = R103) by
// kTr505BusOut. Measured (fit of the band shares against the recordings, hard limit
// better than one-sided or soft): the limit at 0.606 of a 47k channel's full scale (with
// its envelope) brings the bass drum from 4.7 to 0.6 dB rms, the snare from 3.7 to 1.1,
// the high conga 2.4 to 0.6; the quieter cowbells, clap and rim do not reach it.
// kTr505BusOut: set so that the bus sounds keep their measured levels against the other
// sounds (kTr505Out, which the bus sounds no longer use): checked, all within +-0.5 dB.
constexpr double kTr505BusClip = 1.0 / 1.65;
constexpr double kTr505BusOut = 0.973;
constexpr double kHpA = 1.0 / (1.0 + 2.0 * 3.14159265358979 * 0.34 / PcmMachine::kRate);

// TR-626: the envelope, as the TR-505's (same model). Measured (tools/measure/env_fit.py,
// the recordings at velocity 127): one hold and one v0 for all sounds (the TR-505's
// values fit within 0.03 dB rms as well), tau per sound, within 0.2-1.1 dB rms. tau falls
// into the TR-505's three groups (about 105, 260 and 590 ms) and the open hi-hat's 850 ms.
// tau mostly goes with the channel (kTr626Channel), as on the TR-505. Not for the rim shot
// (its channel's snare 3: 100 ms): it ends too soon for the fit to be sure (1.06 s over
// 95 ms), but the render's band shares (check_render.py) confirm it: 3.8 dB rms at 100 ms,
// 2.6 at 260, 1.5 at 600, 1.05 at 1000 ms. As the hi-hats' channel, with two envelopes.
constexpr double kTr626Hold = 0.030;
constexpr float kTr626V0 = 0.12f;
constexpr float kTr626Tau[30] = {
    0.318f, 0.260f, 0.260f, 0.260f, 0.098f,   // open high conga, low / high / mid tom 1, clap
    0.106f, 0.110f, 0.103f, 1.060f, 0.138f,   // cowbell, claves, tambourine, rim, bass drum 1
    0.846f, 0.582f, 0.599f, 0.275f, 0.245f,   // open hi-hat, crash, ride, snare 1, snare 2
    0.275f, 0.267f, 0.267f, 0.267f, 0.110f,   // low conga, low / high / mid tom 2, mute conga
    0.106f, 0.110f, 0.106f, 0.100f, 0.110f,   // low agogo, shaker, high agogo, snare 3, BD 2
    0.252f, 0.582f, 0.599f, 0.245f, 0.245f,   // closed hi-hat, china, cup, low / high timbale
};

// TR-626: each sound's gain at the left and right outputs (relative to the loudest),
// measured (tools/measure/levels.py, as the TR-505's). The right output read 0.28 dB high
// on all centred sounds (0.21-0.43); taken out, as on the TR-505.
constexpr float kTr626Out[30][2] = {
    {0.309f, 0.309f},   // open high conga
    {0.472f, 0.229f},   // low tom 1
    {0.233f, 0.483f},   // high tom 1
    {0.461f, 0.462f},   // mid tom 1
    {0.302f, 0.301f},   // hand clap
    {0.223f, 0.222f},   // cowbell
    {0.321f, 0.319f},   // claves
    {0.195f, 0.193f},   // tambourine
    {0.597f, 0.598f},   // rim shot
    {1.000f, 1.000f},   // bass drum 1
    {0.078f, 0.199f},   // open hi-hat
    {0.112f, 0.164f},   // crash
    {0.250f, 0.143f},   // ride
    {0.401f, 0.402f},   // snare drum 1
    {0.407f, 0.407f},   // snare drum 2
    {0.307f, 0.309f},   // low conga
    {0.477f, 0.231f},   // low tom 2
    {0.229f, 0.476f},   // high tom 2
    {0.468f, 0.469f},   // mid tom 2
    {0.324f, 0.323f},   // mute high conga
    {0.208f, 0.207f},   // low agogo
    {0.243f, 0.241f},   // shaker
    {0.207f, 0.209f},   // high agogo
    {0.578f, 0.578f},   // snare drum 3
    {0.987f, 0.987f},   // bass drum 2
    {0.076f, 0.193f},   // closed hi-hat
    {0.107f, 0.157f},   // china cymbal
    {0.255f, 0.146f},   // cup
    {0.366f, 0.372f},   // low timbale
    {0.361f, 0.367f},   // high timbale
};

// TR-626: the envelope pulse, as the TR-505's. Measured (tools/measure/vca_fit.py, one
// value for all sounds; no series resistors known, all taken as 47k): 0.06 brings the band
// shares from 2.4 to 1.0 dB rms on average (the TR-505: 0.05).
constexpr double kTr626Pulse = 0.06;

// TR-626: the channel each sound plays on; a sound cuts off any other sound of its
// channel. Measured (tools/measure/pairscan.py, cut_eval.py: all 435 pairs of the 30
// notes, the second 30 ms after the first): 55 pairs cut off (what is left of the first
// sound under 0.05), all others play on (0.9-1.1). They form eight groups, every pair
// within a group cutting, as the MB63H114's eight channels. The numbers are ours, chosen
// as the TR-505's where the sounds match (no service notes for the TR-626): 0 hi-hats,
// 1 cymbals, 2 toms and congas, 3 bass drums, 4 clap, mute conga, shaker, claves,
// 5 snares 1-2 and timbales, 6 tambourine, cowbell, agogos, 7 rim shot and snare 3.
constexpr int kTr626Channel[30] = {
    2, 2, 2, 2, 4,   // open high conga, low / high / mid tom 1, clap
    6, 4, 6, 7, 3,   // cowbell, claves, tambourine, rim, bass drum 1
    0, 1, 1, 5, 5,   // open hi-hat, crash, ride, snare 1, snare 2
    2, 2, 2, 2, 4,   // low conga, low / high / mid tom 2, mute conga
    6, 4, 6, 7, 3,   // low agogo, shaker, high agogo, snare 3, bass drum 2
    0, 1, 1, 5, 5,   // closed hi-hat, china, cup, low / high timbale
};

} // namespace

PcmMachine::PcmMachine(Model model) : model_(model), numSounds_(model == kTr505 ? 16 : 30)
{
    for (int s = 0; s < numSounds_; ++s) {
        const Region r = model == kTr505 ? kTr505Sounds[s] : kTr626BankSounds[s % 15];
        voices_[s].offset = r.offset + (model == kTr626 && s >= 15 ? 0x20000 : 0);
        voices_[s].length = r.length;
        voices_[s].flip = model == kTr505 ? kTr505Flip[s] : 0;
        if (model == kTr505) {
            voices_[s].decay = std::exp(-1.0 / (double(kTr505Tau[s]) * kRate));
            voices_[s].v0 = kTr505V0;
            voices_[s].outL = kTr505Out[s][0];
            voices_[s].outR = kTr505Out[s][1];
            voices_[s].hold = uint32_t(kTr505Hold * kRate);
            voices_[s].pulse = float(kTr505Pulse * (kTr505Series[s] + 2.2) / (47.0 + 2.2) * 128.0);
            voices_[s].bus = kTr505Channel[s] >= 3;
            voices_[s].busIn = float((47.0 + 2.2) / (kTr505Series[s] + 2.2));
        } else {
            voices_[s].decay = std::exp(-1.0 / (double(kTr626Tau[s]) * kRate));
            voices_[s].v0 = kTr626V0;
            voices_[s].outL = kTr626Out[s][0];
            voices_[s].outR = kTr626Out[s][1];
            voices_[s].hold = uint32_t(kTr626Hold * kRate);
            voices_[s].pulse = float(kTr626Pulse * 128.0);
        }
        voices_[s].step = kSampleRate / kRate;
        voices_[s].mono = std::sqrt((voices_[s].outL * voices_[s].outL + voices_[s].outR * voices_[s].outR) / 2.0f);
    }
}

void PcmMachine::reset()
{
    for (auto& v : voices_) {
        v.playing = false;
        v.pos = 0.0;
    }
    hpXL_ = hpYL_ = hpXR_ = hpYR_ = 0.0f;
    hpXColumn_.fill(0.0f);
    hpYColumn_.fill(0.0f);
}

void PcmMachine::trigger(int sound, int velocity)
{
    if (sound < 0 || sound >= numSounds_ || velocity <= 0)
        return;
    const int step = (velocity > 127 ? 127 : velocity) >> 1;
    Voice& v = voices_[sound];
    v.gain = (model_ == kTr505 ? kTr505Accent : kTr626Accent)[step];
    v.pos = 0.0;
    v.env = 1.0;
    v.holdLeft = v.hold;
    v.playing = true;
    const int* channel = model_ == kTr505 ? kTr505Channel : kTr626Channel;
    for (int s = 0; s < numSounds_; ++s)
        if (s != sound && channel[s] == channel[sound])
            voices_[s].playing = false;
}

void PcmMachine::setPan(int sound, double offset)
{
    Voice& v = voices_[sound];
    if (offset == v.panOffset)
        return;
    v.panOffset = offset;
    const double a0 = v.bus ? kPi / 4.0 : std::atan2(double(v.outR), double(v.outL));
    const double a = std::clamp(a0 + offset * kPi / 4.0, 0.0, kPi / 2.0);
    v.panL = float(std::cos(a) / std::cos(a0));
    v.panR = float(std::sin(a) / std::sin(a0));
}

void PcmMachine::render(int n, float* left, float* right, float* const* columns)
{
    std::array<float*, kMaxColumns> c{};
    if (columns)
        std::copy(columns, columns + kMaxColumns, c.begin());
    while (n > kBlock) {
        renderBlock(kBlock, left, right, c.data());
        left += kBlock;
        right += kBlock;
        for (float*& p : c)
            if (p)
                p += kBlock;
        n -= kBlock;
    }
    renderBlock(n, left, right, c.data());
}

void PcmMachine::renderBlock(int n, float* left, float* right, float* const* columns)
{
    for (int i = 0; i < n; ++i)
        left[i] = right[i] = bus_[size_t(i)] = busL_[size_t(i)] = busR_[size_t(i)] = 0.0f;
    for (int c = 0; c < kMaxColumns; ++c)
        if (columns[c])
            std::fill(columns[c], columns[c] + n, 0.0f);
    if (!rom_)
        return;
    for (int s = 0; s < numSounds_; ++s) {
        Voice& v = voices_[s];
        if (!v.playing)
            continue;
        const uint8_t* data = rom_ + v.offset;
        const float g = v.gain * v.level / 128.0f / (1.0f - v.v0);
        for (int i = 0; i < n; ++i) {
            const uint32_t k = uint32_t(v.pos);
            const float e = float(v.env) - v.v0;
            // With an envelope pulse the voice lasts as long as its envelope: after the
            // sound's end the DAC gives its centre and the pulse fades on (assumed).
            if (e <= 0.0f || (k >= v.length && v.pulse == 0.0f)) {
                v.playing = false;
                break;
            }
            const float d = k < v.length ? float(data[k ^ v.flip]) - 128.0f : 0.0f;
            const float x = (d + v.pulse) * g * e;
            if (v.holdLeft)
                --v.holdLeft;
            else
                v.env *= v.decay;
            if (v.column >= 0 && columns[v.column]) {
                if (v.bus) {
                    const float y = x * v.busIn, b = std::fabs(y);
                    columns[v.column][i] += y * float(kTr505BusOut) * (b > float(kTr505BusClip) ? float(kTr505BusClip) / b : 1.0f);
                } else {
                    columns[v.column][i] += x * v.mono;
                }
            } else if (v.bus) {
                bus_[size_t(i)] += x * v.busIn;
                busL_[size_t(i)] += x * v.busIn * v.panL;
                busR_[size_t(i)] += x * v.busIn * v.panR;
            } else {
                left[i] += x * v.outL * v.panL;
                right[i] += x * v.outR * v.panR;
            }
            v.pos += v.step;
        }
    }
    // The TR-505's centre bus (IC16b): its sum limited, then to both outputs alike. A pan of
    // ours leaves the limit where it is: the two sides get the limited sum's share.
    if (model_ == kTr505) {
        for (int i = 0; i < n; ++i) {
            const float b = std::fabs(bus_[size_t(i)]);
            const float c = float(kTr505BusOut) * (b > float(kTr505BusClip) ? float(kTr505BusClip) / b : 1.0f);
            left[i] += busL_[size_t(i)] * c;
            right[i] += busR_[size_t(i)] * c;
        }
    }
    // The output's coupling capacitors (TR-505: C52 / C50 10 uF into R111 / R110 47k:
    // 0.34 Hz, documented; the TR-626 taken as the same, derived).
    for (int i = 0; i < n; ++i) {
        const float l = left[i], r = right[i];
        hpYL_ = float(kHpA) * (hpYL_ + l - hpXL_);
        hpYR_ = float(kHpA) * (hpYR_ + r - hpXR_);
        hpXL_ = l;
        hpXR_ = r;
        left[i] = hpYL_;
        right[i] = hpYR_;
    }
    for (int c = 0; c < kMaxColumns; ++c) {
        if (!columns[c])
            continue;
        for (int i = 0; i < n; ++i) {
            const float x = columns[c][i];
            hpYColumn_[size_t(c)] = float(kHpA) * (hpYColumn_[size_t(c)] + x - hpXColumn_[size_t(c)]);
            hpXColumn_[size_t(c)] = x;
            columns[c][i] = hpYColumn_[size_t(c)];
        }
    }
}

// The TR-626's pitch (analysis/TR-626.md, "Pitch"). measured: a sound's rate is a clock
// of 1.6 MHz over a count, 64 at pitch 0 (25 kHz); each step down adds 4 to the count,
// each step up takes away a number that depends on the sound: 3 for low tom 1, 2 for the
// cowbell. assumed: 3 for the other sounds, until they are measured.
constexpr int kTr626PitchUp[30] = {
    3, 3, 3, 3, 3, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

double PcmMachine::tr626PitchFactor(int sound, int step)
{
    step = std::clamp(step, -7, 7);
    const int up = sound >= 0 && sound < 30 ? kTr626PitchUp[sound] : 3;
    return 64.0 / double(64 + (step < 0 ? -4 * step : -up * step));
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
