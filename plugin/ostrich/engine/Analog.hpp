// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building blocks for analogue circuits, discrete-time at a fixed rate. They follow MAME's
// devices of the same purpose (BSD-3-Clause, see THIRD-PARTY.md): va_rc_eg (an RC envelope),
// filter_rc (one-pole RC low/high pass), filter_biquad (band passes from RC networks).

#pragma once

#include <cmath>

namespace ostrich {

constexpr double kPi = 3.14159265358979323846;

inline double parallel(double a, double b) { return a * b / (a + b); }
inline double divider(double rTop, double rBottom) { return rBottom / (rTop + rBottom); }

// A capacitor charging towards a target voltage through a resistor.
class RcEg
{
public:
    void init(double rate, double r, double c) { dt_ = 1.0 / rate; set(r, c, 0.0); }

    void set(double r, double c, double target)
    {
        target_ = target;
        if (r != r_ || c != c_) {
            r_ = r;
            c_ = c;
            k_ = 1.0 - std::exp(-dt_ / (r * c));
        }
    }

    double step()
    {
        v_ += (target_ - v_) * k_;
        return v_;
    }

    double v() const { return v_; }
    void reset() { v_ = 0.0; }

private:
    double dt_ = 1.0;
    double r_ = -1.0, c_ = -1.0, k_ = 0.0;
    double target_ = 0.0, v_ = 0.0;
};

// One-pole RC filter, as MAME's filter_rc (LOWPASS, HIGHPASS).
class RcFilter
{
public:
    enum Type { kLowpass, kHighpass };

    void init(double rate, Type type, double r, double c)
    {
        type_ = type;
        k_ = 1.0 - std::exp(-1.0 / (r * c) / rate);
        mem_ = 0.0;
    }

    double step(double x)
    {
        if (type_ == kLowpass) {
            mem_ += (x - mem_) * k_;
            return mem_;
        }
        const double y = x - mem_;
        mem_ += (x - mem_) * k_;
        return y;
    }

    void reset() { mem_ = 0.0; }

private:
    Type type_ = kLowpass;
    double k_ = 1.0, mem_ = 0.0;
};

// Biquad in MAME's direct form II, with its bilinear coefficients.
class Biquad
{
public:
    void bandpass(double rate, double fc, double q, double gain)
    {
        const double K = std::tan(kPi * fc / rate), K2 = K * K, KQ = K / q;
        const double n = 1.0 / (1.0 + KQ + K2);
        b0_ = KQ * n * gain;
        b1_ = 0.0;
        b2_ = -b0_;
        a1_ = 2.0 * (K2 - 1.0) * n;
        a2_ = (1.0 - KQ + K2) * n;
        reset();
    }

    // Band pass of an op-amp stage with R1, C1 in series and R2 || C2 in feedback
    // (MAME rc_cc_bandpass_calc).
    void rcCcBandpass(double rate, double r1, double r2, double c1, double c2)
    {
        const double x = std::sqrt(r1 * r2 * c1 * c2);
        const double y = r1 * c1 + r2 * c2 + r1 * c2;
        bandpass(rate, 1.0 / (2.0 * kPi * x), x / y, r2 * c2 / y);
    }

    // MAME rc_rr_bandpass_calc.
    void rcRrBandpass(double rate, double r1, double r2, double c1, double c2)
    {
        const double x = std::sqrt(r1 * r2 * c1 * c2);
        const double y = r1 * c1 + r2 * c2 + r2 * c1;
        bandpass(rate, 1.0 / (2.0 * kPi * x), x / y, r2 * c1 / y);
    }

    double step(double x)
    {
        w2_ = w1_;
        w1_ = w0_;
        w0_ = -a1_ * w1_ - a2_ * w2_ + x;
        return b0_ * w0_ + b1_ * w1_ + b2_ * w2_;
    }

    void reset() { w0_ = w1_ = w2_ = 0.0; }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double w0_ = 0.0, w1_ = 0.0, w2_ = 0.0;
};

} // namespace ostrich
