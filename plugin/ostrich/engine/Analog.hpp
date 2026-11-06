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

// A first-order shelf: a zero and a pole (bilinear, both frequencies exact). `amount` blends
// from the input (0) to the shelf (1).
class Shelf
{
public:
    void init(double rate, double zero, double pole)
    {
        const double z = 1.0 / std::tan(kPi * zero / rate), p = 1.0 / std::tan(kPi * pole / rate);
        b0_ = (1.0 + z) / (1.0 + p);
        b1_ = (1.0 - z) / (1.0 + p);
        a1_ = (1.0 - p) / (1.0 + p);
        reset();
    }

    double step(double x, double amount)
    {
        const double y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x;
        y1_ = y;
        return x + amount * (y - x);
    }

    void reset() { x1_ = y1_ = 0.0; }

private:
    double b0_ = 1.0, b1_ = 0.0, a1_ = 0.0, x1_ = 0.0, y1_ = 0.0;
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

// The band pass of the TR-707's bass, snare and toms: Vin -> R1 -> N, C1 from N to ground,
// C2 from N through R2 to the op-amp's virtual ground, as Biquad::rcCcBandpass, with a second
// path parallel to R1: a resistor and two anti-parallel diodes. Loud signals see R1 || Rd,
// quiet ones R1 alone: their highs fall earlier. Trapezoidal, piecewise linear (a diode
// conducts above kVf); step() returns the voltage across R2.
class DiodeBandpass
{
public:
    void init(double rate, double r1, double rd, double r2, double c1, double c2)
    {
        h_ = 0.5 / rate;
        g1_ = 1.0 / r1;
        gd_ = 1.0 / rd;
        g2_ = 1.0 / r2;
        c1_ = c1;
        c2_ = c2;
        reset();
    }

    double step(double vin)
    {
        double n, c;
        solve(vin, g1_, 0.0, n, c);
        double g = g1_, j = 0.0;
        const double d = vin - n;
        if (std::fabs(d) > kVf) {
            g += gd_;
            j = (d > 0.0 ? kVf : -kVf) * gd_;
            solve(vin, g, j, n, c);
        }
        dn_ = (g * (vin - n) - j - (n - c) * g2_) / c1_;
        dc_ = (n - c) * g2_ / c2_;
        n_ = n;
        c_ = c;
        return n - c;
    }

    void reset() { n_ = c_ = dn_ = dc_ = 0.0; }

private:
    static constexpr double kVf = 0.6;

    // The next state, if the current into N is g * (vin - n) - j.
    void solve(double vin, double g, double j, double& n, double& c) const
    {
        const double a = h_ / c1_, b = h_ / c2_;
        const double r1 = n_ + h_ * dn_ + a * (g * vin - j), r2 = c_ + h_ * dc_;
        const double p11 = 1.0 + a * (g + g2_), p12 = -a * g2_;
        const double p21 = -b * g2_, p22 = 1.0 + b * g2_;
        const double det = p11 * p22 - p12 * p21;
        n = (r1 * p22 - p12 * r2) / det;
        c = (p11 * r2 - p21 * r1) / det;
    }

    double h_ = 0.0, g1_ = 0.0, gd_ = 0.0, g2_ = 0.0, c1_ = 1.0, c2_ = 1.0;
    double n_ = 0.0, c_ = 0.0, dn_ = 0.0, dc_ = 0.0;
};

} // namespace ostrich
