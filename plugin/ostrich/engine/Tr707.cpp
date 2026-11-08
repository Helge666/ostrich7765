// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of MAME's tr707_audio_device (BSD-3-Clause, copyright-holders AJR, m1macrophage);
// component values and comments on the circuit are theirs, after Roland's service notes.

#include "Tr707.hpp"

#include <algorithm>

namespace ostrich {

namespace {

constexpr double VCC = 5.0;   // volts
constexpr double VBE = 0.6;   // a BJT's base-emitter drop

constexpr double K = 1e3, M = 1e6, U = 1e-6, P = 1e-12;

constexpr uint16_t kMaxCount = 0x1fff;          // MB63H114 counters (13 bit)
constexpr uint16_t kMaxCymbalCount = 0x8000;    // the cymbals' counters stop here
constexpr int kPulseTicks = 280;                // 1.4 ms of 200 kHz (firmware: phase 0 to 2)

// Values that differ between the two machines.
struct Components {
    int xck2Input;  // IC30: which of A-D clocks counter 2 (the low tom)
    double R95, R102, R82, R91, R73;
    double R203, R205, R206, R207, R208, R211, R214;
    double R255, C213, R256, R233, C216, R237, C220, R242, C221, C222, R246, C226, C230,
        R249, C231;
};

constexpr Components kTr707 = {
    3,
    4.7 * M, 2.2 * M, 2.2 * M, 4.7 * M, 2.2 * M,
    22 * K, 33 * K, 33 * K, 47 * K, 22 * K, 33 * K, 47 * K,
    10 * K, 0.01 * U, 2.2 * K, 33 * K, 470 * P, 47 * K, 0.01 * U, 47 * K, 0.01 * U, 1 * U,
    33 * K, 0.01 * U, 0.01 * U, 22 * K, 0.01 * U,
};

constexpr Components kTr727 = {
    2,
    1 * M, 1 * M, 4.7 * M, 2.2 * M, 4.7 * M,
    47 * K, 47 * K, 22 * K, 22 * K, 47 * K, 22 * K, 33 * K,
    22 * K, 0.0047 * U, 47 * K, 47 * K, 0.0047 * U, 22 * K, 0.012 * U, 33 * K, 0.012 * U,
    10 * U, 47 * K, 0.0022 * U, 0.01 * U, 10 * K, 0.0022 * U,
};

// Mux voices, in the counters' order.
enum { MV_BASS, MV_SNARE, MV_LOW_TOM, MV_MID_TOM, MV_HI_TOM, MV_HI_HAT, MV_RIMSHOT, MV_HANDCLAP };

// The EGs of the multiplexed voices. MAME reads 0.047 uF in the service notes (hat 1 uF), which
// makes their decay far faster than a recording of a real unit (analysis/TR-707.md, "Against a
// recording"): there snare, toms, cowbell, clap and tambourine follow their ROM data within 1-3 dB
// for as long as they can be seen. Ten times that fits; the sample does the decay, the EG
// the accent. Not checked against a board.
constexpr double kMuxEgC[8] = {0.47 * U, 0.47 * U, 0.47 * U, 0.47 * U, 0.47 * U, 1 * U,
                               0.47 * U, 0.47 * U};
constexpr double kCymbalEgC[2] = {1 * U, 1 * U};
constexpr double R79 = 100.0;   // the EGs' common charge resistor

// The mux DAC circuit's output voltage for an EG voltage and a data value (MAME
// mux_dac_v): only its span is used, as in MAME.
double muxDacV(double vEg, int data)
{
    constexpr double R156 = 12 * K, R153 = 2.2 * K, R147 = 2.2 * K, R148 = 2.2 * K;
    const double vIn = vEg - VBE;
    const double iRef = VCC / R156 + vIn / R153;
    const double iOut = double(data) / 256.0 * iRef;
    return vIn - (R147 + R148) * iOut - VBE - VBE;
}

// The hat's single-transistor VCA, measured (a recording of a real unit over the ROM, about
// 600 hits; analysis/TR-707.md, "Hi-hat"): its gain saturates, x / (1 + x) of the EG voltage
// over a knee. The open hat, on the slow EG, stays near full gain for all of its sample; the
// closed hat falls only once its EG nears the knee, after some 30 ms. 1 at the EG's top.
constexpr double kHatKnee = 0.18;   // volts
double hatVca(double vEg)
{
    return vEg / (vEg + kHatKnee) * (VCC + kHatKnee) / VCC;
}

// The highs of that recording over the circuit as documented, at the mix: a zero at
// kHighsZero, a pole at kHighsPole (fit, analysis/TR-707.md, "Highs").
constexpr double kHighsZero = 3500.0, kHighsPole = 40000.0;

struct SoundWiring {
    int trigger;      // trigger bit
    int select;       // 0 none, 1 bass, 2 snare, 3 rim/cow, 4 clap/tamb, 5 hat
    int value;        // select value (for the hat: 1 closed)
};

constexpr SoundWiring kWiring[Tr707::kNumSounds] = {
    {MV_BASS, 1, 0}, {MV_BASS, 1, 1}, {MV_SNARE, 2, 0}, {MV_SNARE, 2, 1},
    {MV_LOW_TOM, 0, 0}, {MV_MID_TOM, 0, 0}, {MV_HI_TOM, 0, 0},
    {MV_RIMSHOT, 3, 0}, {MV_RIMSHOT, 3, 1}, {MV_HANDCLAP, 4, 0}, {MV_HANDCLAP, 4, 1},
    {MV_HI_HAT, 5, 1}, {MV_HI_HAT, 5, 0}, {8, 0, 0}, {9, 0, 0},
};

constexpr int kSoundChannel[Tr707::kNumSounds] = {
    Tr707::kChBass, Tr707::kChBass, Tr707::kChSnare, Tr707::kChSnare, Tr707::kChLowTom,
    Tr707::kChMidTom, Tr707::kChHiTom, Tr707::kChRimshot, Tr707::kChRimshot,
    Tr707::kChHandclap, Tr707::kChHandclap, Tr707::kChHiHat, Tr707::kChHiHat,
    Tr707::kChCrash, Tr707::kChRide,
};

} // namespace

int Tr707::soundChannel(int sound) { return kSoundChannel[sound]; }

int Tr707::noteToSound(bool tr727, int note)
{
    if (tr727) {
        // TR-727 manual and firmware (receive map after reset): notes 60-74.
        static constexpr int map[15] = {kBD1, kBD2, kSD1, kSD2, kLT, kMT, kHT, kRS, kCB,
                                        kHCP, kTAMB, kCH, kOH, kCrash, kRide};
        return note >= 60 && note <= 74 ? map[note - 60] : -1;
    }
    // TR-707 manual (and the firmware's MIDI out table for the first of each pair).
    switch (note) {
    case 35: return kBD1;
    case 36: return kBD2;
    case 37: return kRS;
    case 38: return kSD1;
    case 39: return kHCP;
    case 40: return kSD2;
    case 41: case 43: return kLT;
    case 42: case 44: return kCH;
    case 45: case 47: return kMT;
    case 46: return kOH;
    case 48: case 50: return kHT;
    case 49: return kCrash;
    case 51: return kRide;
    case 54: return kTAMB;
    case 56: return kCB;
    default: return -1;
    }
}

Tr707::Tr707(bool tr727) : tr727_(tr727)
{
    const Components& c = tr727 ? kTr727 : kTr707;
    xck2Input_ = c.xck2Input;
    muxEgR_ = {c.R95, c.R102, 4.7 * M, 4.7 * M, 4.7 * M, 4.7 * M, c.R82, c.R91};
    // MAME: R58 is 47K in the schematic, but decays too fast; 470K, as R61.
    cymEgR_ = {470 * K, parallel(470 * K, c.R73)};

    for (int i = 0; i < kMux; ++i)
        muxEg_[i].init(kRate, muxEgR_[i], kMuxEgC[i]);
    for (int i = 0; i < kCymbals; ++i) {
        cymEg_[i].init(kRate, cymEgR_[i], kCymbalEgC[i]);
        cymHpf_[i].init(kRate, RcFilter::kHighpass, 22 * K + 470.0, 1 * U);   // ~7 Hz
    }
    hatEg_.init(kRate, 1 * M, 1 * U);
    hatHpf_.init(kRate, RcFilter::kHighpass, 10 * K + 220.0, 1 * U);          // ~15.6 Hz

    const double vpp = muxDacV(VCC, 0) - muxDacV(VCC, 0xff);
    muxScale_ = -(vpp / 2.0) / VCC;

    // Band passes before the level sliders: {R1, R2, C1, C2}. Bass, snare and toms have a
    // diode path parallel to R1 (kDiodeR).
    const double kDiodeR[kNumDiodeChannels] = {c.R255, c.R256, 4.7 * K, 4.7 * K, 4.7 * K};
    const double bpf[kNumChannels][4] = {
        {22 * K, 33 * K, c.C213, 1 * U},
        {22 * K, c.R233, c.C216, 1 * U},
        {22 * K, 47 * K, 0.01 * U, 1 * U},
        {22 * K, c.R237, c.C220, 1 * U},
        {22 * K, c.R242, c.C221, c.C222},
        {22 * K, 47 * K, 470 * P, 0.01 * U},
        {22 * K, c.R246, 470 * P, c.C226},
        {22 * K, 22 * K, 470 * P, 0.01 * U},
        {10 * K, 22 * K, 470 * P, c.C230},
        {10 * K, c.R249, 470 * P, c.C231},
    };
    const double rLeft[kNumChannels] = {22 * K, c.R205, c.R208, c.R211, c.R214,
                                        33 * K, 33 * K, 47 * K, 33 * K, 22 * K};
    const double rRight[kNumChannels] = {c.R203, c.R206, c.R207, 33 * K, 22 * K,
                                         33 * K, 33 * K, 22 * K, 22 * K, 47 * K};
    constexpr double kMaxMaster = 50 * K, kMaxChannel = 50 * K;
    for (int i = 0; i < kNumChannels; ++i) {
        if (i < kNumDiodeChannels)
            diodeBpf_[i].init(kRate, bpf[i][0], kDiodeR[i], bpf[i][1], bpf[i][2], bpf[i][3]);
        else
            bpf_[i].rcCcBandpass(kRate, bpf[i][0], bpf[i][1], bpf[i][2], bpf[i][3]);
        bpfGain_[i] = -kMaxChannel / bpf[i][1];          // inverting op-amp
        lpf_[i].init(kRate, RcFilter::kLowpass, 1 * K, 0.01 * U);   // ~15.9 kHz
        mixL_[i] = -kMaxMaster / rLeft[i];
        mixR_[i] = -kMaxMaster / rRight[i];
        panL_[i] = panR_[i] = 1.0;
        level_[i] = 1.0;
        inMix_[i] = true;
        // The same power as its two sides together; in the centre, the level of one side.
        monoGain_[i] = -std::sqrt((mixL_[i] * mixL_[i] + mixR_[i] * mixR_[i]) / 2.0);
        dcMono_[i].init(kRate, RcFilter::kHighpass, 1.0 / (2.0 * kPi * 0.05), 1.0);
        outMono_[i].rcRrBandpass(kRate, 1 * K, 47 * K, 10 * U, 0.01 * U);
        highsMono_[i].init(kRate, kHighsZero, kHighsPole);
    }
    // DC blocking caps on the summing amps (C211, C212); MAME uses a 0.05 Hz high pass.
    dcL_.init(kRate, RcFilter::kHighpass, 1.0 / (2.0 * kPi * 0.05), 1.0);
    dcR_.init(kRate, RcFilter::kHighpass, 1.0 / (2.0 * kPi * 0.05), 1.0);
    // Output band passes, about 0.35 Hz to 12.4 kHz.
    outL_.rcRrBandpass(kRate, 1 * K, 47 * K, 10 * U, 0.01 * U);
    outR_.rcRrBandpass(kRate, 1 * K, 47 * K, 10 * U, 0.01 * U);
    highsL_.init(kRate, kHighsZero, kHighsPole);
    highsR_.init(kRate, kHighsZero, kHighsPole);
    reset();
}

void Tr707::setPan(int channel, double offset)
{
    if (offset == panOffset_[channel])
        return;
    panOffset_[channel] = offset;
    const double a0 = std::atan2(std::fabs(mixR_[channel]), std::fabs(mixL_[channel]));
    const double a = std::clamp(a0 + offset * kPi / 4.0, 0.0, kPi / 2.0);
    panL_[channel] = std::cos(a) / std::cos(a0);
    panR_[channel] = std::sin(a) / std::sin(a0);
}

void Tr707::reset()
{
    pendingMask_ = 0;
    pendingSelect_ = pendingSelectMask_ = 0;
    pendingAccent_ = -1;
    pendingCymbal_ = false;
    pulseTicks_ = 0;
    refire_ = false;
    bassVar_ = snareVar_ = rimCow_ = clapTamb_ = 0;
    hatClosed_ = hatTriggering_ = false;
    accentV_ = 0.0;
    triggers_ = 0x3ff;
    counters_.fill(kMaxCount);
    xck_ = macOffset_ = macC_ = 0;
    muxSample_ = muxVoice_ = 0;
    muxDac_.fill(0x80);
    cymCounter_.fill(kMaxCymbalCount);
    cymDac_.fill(0x20);
    for (auto& e : muxEg_) e.reset();
    for (auto& e : cymEg_) e.reset();
    hatEg_.reset();
    hatHpf_.reset();
    for (auto& f : cymHpf_) f.reset();
    for (auto& f : bpf_) f.reset();
    for (auto& f : diodeBpf_) f.reset();
    highsL_.reset();
    highsR_.reset();
    for (auto& f : lpf_) f.reset();
    dcL_.reset();
    dcR_.reset();
    outL_.reset();
    outR_.reset();
    for (auto& f : dcMono_) f.reset();
    for (auto& f : outMono_) f.reset();
    for (auto& f : highsMono_) f.reset();
    updateEgs(0x3ff);
    updateHatEg();
}

void Tr707::trigger(int sound, int velocity)
{
    if (sound < 0 || sound >= kNumSounds || velocity <= 0)
        return;
    const SoundWiring& w = kWiring[sound];
    pendingMask_ |= uint16_t(1u << w.trigger);
    if (w.select) {
        pendingSelectMask_ |= uint8_t(1u << w.select);
        if (w.value)
            pendingSelect_ |= uint8_t(1u << w.select);
        else
            pendingSelect_ &= uint8_t(~(1u << w.select));
    }
    // Firmware: the accent DAC gets velocity / 2 of the last note; at least 15 while a
    // cymbal is pending.
    if (w.trigger >= 8)
        pendingCymbal_ = true;
    int accent = std::min(velocity, 127) >> 1;
    if (pendingCymbal_)
        accent = std::max(accent, 15);
    pendingAccent_ = accent;
}

void Tr707::fire()
{
    // Voice select latch.
    auto sel = [&](int field, uint8_t& var) {
        if (pendingSelectMask_ & (1u << field))
            var = (pendingSelect_ >> field) & 1;
    };
    sel(1, bassVar_);
    sel(2, snareVar_);
    sel(3, rimCow_);
    sel(4, clapTamb_);
    uint8_t closed = hatClosed_;
    sel(5, closed);
    hatClosed_ = closed;

    // Accent DAC (D0-D5 into a resistor array, buffered by an emitter follower). Below
    // VBE the follower is off: clamped at 0 (MAME lets it go negative).
    accentV_ = std::max(0.0, VCC * pendingAccent_ / 63.0 - VBE);

    const uint16_t old = triggers_;
    triggers_ &= uint16_t(~pendingMask_);
    pulseTicks_ = kPulseTicks;
    pendingMask_ = 0;
    pendingSelectMask_ = 0;
    pendingAccent_ = -1;
    pendingCymbal_ = false;
    updateEgs(old);
    updateHatEg();
}

void Tr707::updateEgs(uint16_t oldTriggers)
{
    // Counter starts: falling edge of a trigger.
    for (int i = 0; i < kMux; ++i)
        if ((oldTriggers >> i & 1) && !(triggers_ >> i & 1))
            counters_[i] = 0;

    // All triggered EGs charge through R79 together: their capacitors and discharge
    // resistors act in parallel.
    double chargeC = 0.0, dischargeInv = 0.0;
    for (int i = 0; i < kMux; ++i)
        if (!(triggers_ >> i & 1)) {
            chargeC += kMuxEgC[i];
            dischargeInv += 1.0 / muxEgR_[i];
        }
    for (int i = 0; i < kCymbals; ++i)
        if (!(triggers_ >> (8 + i) & 1)) {
            chargeC += kCymbalEgC[i];
            dischargeInv += 1.0 / cymEgR_[i];
        }
    const double rDis = dischargeInv > 0.0 ? 1.0 / dischargeInv : 1e30;
    const double rAttack = parallel(R79, rDis);
    const double vAttack = accentV_ * divider(R79, rDis);

    for (int i = 0; i < kMux; ++i) {
        if (!(triggers_ >> i & 1))
            muxEg_[i].set(rAttack, chargeC, vAttack);
        else
            muxEg_[i].set(muxEgR_[i], kMuxEgC[i], 0.0);
    }
    hatTriggering_ = !(triggers_ >> MV_HI_HAT & 1);

    for (int i = 0; i < kCymbals; ++i) {
        const int bit = 8 + i;
        if (!(triggers_ >> bit & 1))
            cymEg_[i].set(rAttack, chargeC, vAttack);
        else
            cymEg_[i].set(cymEgR_[i], kCymbalEgC[i], 0.0);
        // The counter's reset input sees the trigger through 100 pF: it restarts on the
        // trigger's rising edge, i.e. when the pulse ends.
        if (!(oldTriggers >> bit & 1) && (triggers_ >> bit & 1))
            cymCounter_[i] = 0;
    }
}

void Tr707::updateHatEg()
{
    // MAME's reading of the hat EG (see its update_hat_eg for the departures from the
    // schematic): one capacitor, a faster discharge for the closed hat.
    double rDis = parallel(220 * K, 1 * M);          // R124, R126
    if (hatClosed_)
        rDis = parallel(rDis, 10 * K);               // R123
    rDis += 4.7 * K;                                 // R127
    if (hatTriggering_) {
        const double rCharge = 100.0;                // R128
        hatEg_.set(parallel(rCharge, rDis), 1 * U, VCC * divider(rCharge, rDis));
    } else {
        hatEg_.set(rDis, 1 * U, 0.0);
    }
}

void Tr707::tick()
{
    const uint8_t o = macOffset_;
    const uint16_t data = counters_[o & 7];   // the active counter, before this step's clocks

    // The sample fetched in the previous step goes to its voice's DAC.
    muxDac_[muxVoice_] = muxSample_;

    // Counter clocks: B (50 kHz) for the interleaved voices, C (25 kHz) for the hat,
    // D (12.5 kHz) for mid and high tom, D or C for the low tom (TR-707 / TR-727).
    const uint8_t b = o >> 1 & 1, c = o >> 2 & 1, d = o >> 3 & 1;
    const uint8_t x2 = o >> xck2Input_ & 1;
    const uint8_t xck = uint8_t(b << 7 | b << 6 | c << 5 | d << 4 | d << 3 | x2 << 2 | b << 1 | b);
    for (int i = 0; i < kMux; ++i)
        if ((xck_ >> i & 1) && !(xck >> i & 1) && counters_[i] < kMaxCount)
            ++counters_[i];
    xck_ = xck;

    // Next sample, latched in the next step.
    muxVoice_ = o & 7;
    uint16_t addr = data;
    switch (muxVoice_) {
    case MV_BASS: addr = (data & 0x1ffe) | bassVar_; break;
    case MV_SNARE: addr = (data & 0x1ffe) | snareVar_; break;
    case MV_RIMSHOT: addr = (data & 0x1ffe) | rimCow_; break;
    case MV_HANDCLAP: addr = (data & 0x1ffe) | clapTamb_; break;
    default: break;
    }
    muxSample_ = roms_.voices[(muxVoice_ << 13) | addr];

    // Cymbals: address on C's falling edge, latch on its rising edge, until bit 15.
    const uint8_t* cym[kCymbals] = {roms_.crash, roms_.ride};
    for (int i = 0; i < kCymbals; ++i) {
        if (cymCounter_[i] >= kMaxCymbalCount)
            continue;
        if (macC_ && !c)
            ++cymCounter_[i];
        else if (!macC_ && c)
            cymDac_[i] = cym[i][cymCounter_[i] & 0x7fff] >> 2;
    }
    macC_ = c;
    macOffset_ = (o + 1) & 15;

    // Trigger pulse.
    if (pulseTicks_ > 0 && --pulseTicks_ == 0) {
        const uint16_t old = triggers_;
        triggers_ = 0x3ff;
        updateEgs(old);
        updateHatEg();
    }
}

void Tr707::render(int n, float* left, float* right, float* const* channels, float* const* outs)
{
    constexpr double HAT_HPF_SCALE = 220.0 / (10 * K + 220.0);
    constexpr double HAT_VCA_SCALE = -0.0084 * 4.7 * K;
    constexpr double CYM_HPF_SCALE = 470.0 / (22 * K + 470.0);
    constexpr double CYM_VCA_SCALE = -0.0043 * 10 * K;
    constexpr double kVoltsToAudio = 0.2;

    for (int s = 0; s < n; ++s) {
        if (refire_) {
            refire_ = false;
            fire();
        } else if (pendingMask_) {
            if (pulseTicks_ > 0 && (~triggers_ & pendingMask_ & 0x3ff)) {
                // Retriggered within its pulse: end the pulse, fire in the next sample.
                const uint16_t old = triggers_;
                triggers_ = 0x3ff;
                pulseTicks_ = 0;
                updateEgs(old);
                updateHatEg();
                refire_ = true;
            } else {
                fire();
            }
        }

        if (hasRoms()) {
            tick();
            tick();
        }

        double v[kNumChannels];
        double mux[kMux];
        for (int i = 0; i < kMux; ++i) {
            const double dac = -1.0 + 2.0 * muxDac_[i] / 256.0;
            mux[i] = dac * muxEg_[i].step() * muxScale_;
        }
        hatEg_.step();
        const double hat = hatHpf_.step(mux[MV_HI_HAT] * HAT_HPF_SCALE) * HAT_VCA_SCALE *
                           hatVca(hatEg_.v());
        double cymb[kCymbals];
        for (int i = 0; i < kCymbals; ++i) {
            const double dac = VCC * cymDac_[i] / 64.0;
            cymb[i] = cymHpf_[i].step(dac * CYM_HPF_SCALE) * CYM_VCA_SCALE *
                      (cymEg_[i].step() / VCC);
        }
        v[kChBass] = mux[MV_BASS];
        v[kChSnare] = mux[MV_SNARE];
        v[kChLowTom] = mux[MV_LOW_TOM];
        v[kChMidTom] = mux[MV_MID_TOM];
        v[kChHiTom] = mux[MV_HI_TOM];
        v[kChRimshot] = mux[MV_RIMSHOT];
        v[kChHandclap] = mux[MV_HANDCLAP];
        v[kChHiHat] = hat;
        v[kChCrash] = cymb[0];
        v[kChRide] = cymb[1];

        double l = 0.0, r = 0.0;
        for (int i = 0; i < kNumChannels; ++i) {
            const double band = i < kNumDiodeChannels ? diodeBpf_[i].step(v[i]) : bpf_[i].step(v[i]);
            const double x = lpf_[i].step(band * bpfGain_[i] * level_[i]);
            if (channels)
                channels[i][s] = float(x * kVoltsToAudio);
            if (outs && outs[i]) {
                const double m = outMono_[i].step(dcMono_[i].step(x * monoGain_[i]) * master_);
                outs[i][s] = float(highsMono_[i].step(m, highs_) * kVoltsToAudio);
            }
            if (inMix_[i]) {
                l += x * mixL_[i] * panL_[i];
                r += x * mixR_[i] * panR_[i];
            }
        }
        l = outL_.step(dcL_.step(l) * master_);
        r = outR_.step(dcR_.step(r) * master_);
        l = highsL_.step(l, highs_);
        r = highsR_.step(r, highs_);
        left[s] = float(l * kVoltsToAudio);
        right[s] = float(r * kVoltsToAudio);
    }
}

} // namespace ostrich
