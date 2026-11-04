// SPDX-License-Identifier: GPL-3.0-or-later
//
// Streaming sample-rate conversion from the machines' internal rate to the host's: a
// Kaiser-windowed sinc, tabulated in phases and interpolated between them.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ostrich {

class Resampler
{
public:
    static constexpr int kHalf = 24;      // taps on each side
    static constexpr int kPhases = 512;

    // maxIn: the most input samples process() gets at once; the history is reserved for it,
    // so that process() does not allocate.
    void init(double inRate, double outRate, int channels, int maxIn = 16384)
    {
        maxIn_ = maxIn;
        step_ = inRate / outRate;
        // Cutoff (cycles per input sample) a little below the lower Nyquist frequency.
        const double fc = 0.5 * std::fmin(1.0, outRate / inRate) * 0.92;
        constexpr double beta = 8.0;
        const double i0b = bessel0(beta);
        table_.assign((kPhases + 1) * 2 * kHalf, 0.0f);
        for (int ph = 0; ph <= kPhases; ++ph) {
            const double frac = double(ph) / kPhases;
            double sum = 0.0;
            float* row = &table_[ph * 2 * kHalf];
            for (int k = 0; k < 2 * kHalf; ++k) {
                const double x = (k - kHalf + 1) - frac;           // distance in input samples
                const double t = x / kHalf;
                const double w = std::fabs(t) < 1.0 ? bessel0(beta * std::sqrt(1.0 - t * t)) / i0b : 0.0;
                const double a = 2.0 * fc * x;
                const double s = std::fabs(a) < 1e-12 ? 1.0 : std::sin(3.14159265358979323846 * a) / (3.14159265358979323846 * a);
                row[k] = float(2.0 * fc * s * w);
                sum += row[k];
            }
            for (int k = 0; k < 2 * kHalf; ++k)
                row[k] = float(row[k] / sum);
        }
        hist_.assign(channels, std::vector<float>());
        reset();
    }

    void reset()
    {
        for (auto& h : hist_) {
            h.reserve(size_t(maxIn_) + 4 * kHalf + 4);
            h.assign(kHalf, 0.0f);
        }
        pos_ = kHalf - 1;
    }

    // Input samples needed (per channel) to produce nOut output samples.
    int inputFor(int nOut) const
    {
        if (nOut <= 0)
            return 0;
        const long last = long(std::floor(pos_ + (nOut - 1) * step_)) + kHalf;
        const long have = hist_.empty() ? 0 : long(hist_[0].size());
        return int(std::max(0L, last + 1 - have));
    }

    // in: per channel nIn = inputFor(nOut) samples; out: per channel nOut samples. A null
    // output pointer skips that channel's filtering (its input is still taken).
    void process(const float* const* in, int nIn, float* const* out, int nOut)
    {
        const int ch = int(hist_.size());
        for (int c = 0; c < ch; ++c)
            hist_[c].insert(hist_[c].end(), in[c], in[c] + nIn);
        double p = pos_;
        for (int j = 0; j < nOut; ++j, p += step_) {
            const long base = long(std::floor(p));
            const double fp = (p - base) * kPhases;
            const int ph = int(fp);
            const float f = float(fp - ph);
            const float* r0 = &table_[ph * 2 * kHalf];
            const float* r1 = r0 + 2 * kHalf;
            const long first = base - kHalf + 1;
            for (int c = 0; c < ch; ++c) {
                if (!out[c])
                    continue;
                const float* x = hist_[c].data() + first;
                float a0 = 0.0f, a1 = 0.0f;
                for (int k = 0; k < 2 * kHalf; ++k) {
                    a0 += r0[k] * x[k];
                    a1 += r1[k] * x[k];
                }
                out[c][j] = a0 + (a1 - a0) * f;
            }
        }
        // Drop what the next output no longer needs.
        const long keepFrom = long(std::floor(p)) - kHalf + 1;
        if (keepFrom > 0) {
            for (auto& h : hist_)
                h.erase(h.begin(), h.begin() + std::min<long>(keepFrom, long(h.size())));
            p -= keepFrom;
        }
        pos_ = p;
    }

private:
    static double bessel0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    double step_ = 1.0, pos_ = 0.0;
    int maxIn_ = 16384;
    std::vector<float> table_;
    std::vector<std::vector<float>> hist_;
};

} // namespace ostrich
