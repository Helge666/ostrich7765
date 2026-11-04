// SPDX-License-Identifier: GPL-3.0-or-later
/*
    Play the TR-707 engine without a plugin: every sound at three velocities, written as a
    stereo WAV at the host rate -- for listening and for checking the engine against the
    sound ROMs.

        ostrich_engine_render out.wav IC34 IC35 IC19 IC22 [rate] [channels.wav] [727]

    IC34 ... IC22 are the TR-707's sound ROM images (32 KiB each). `rate` defaults to 48000.
    `channels.wav` (optional) gets the ten mixer channels at the engine's internal 100 kHz,
    32-bit float, for analysis. A last argument `727` plays the TR-727's board instead
    (with the same ROMs, as there are no TR-727 dumps yet).
    Prints, for every hit, its time, peak and length (to -60 dB below its peak).
*/

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../ostrich/engine/Resampler.hpp"
#include "../ostrich/engine/Tr707.hpp"

using ostrich::Resampler;
using ostrich::Tr707;

namespace {

bool readFile(const char* path, std::vector<uint8_t>& out, size_t size)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    out.resize(size);
    const size_t n = std::fread(out.data(), 1, size, f);
    std::fclose(f);
    if (n != size) {
        std::fprintf(stderr, "%s: expected %zu bytes\n", path, size);
        return false;
    }
    return true;
}

void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }

// format 1: 16-bit PCM, 3: 32-bit float; data interleaved.
bool writeWav(const char* path, int rate, int channels, int format, const void* data,
              size_t frames)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    const int bytes = format == 3 ? 4 : 2;
    const uint32_t dataSize = uint32_t(frames * channels * bytes);
    std::fwrite("RIFF", 1, 4, f);
    put32(f, 36 + dataSize);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, uint16_t(format));
    put16(f, uint16_t(channels));
    put32(f, uint32_t(rate));
    put32(f, uint32_t(rate * channels * bytes));
    put16(f, uint16_t(channels * bytes));
    put16(f, uint16_t(bytes * 8));
    std::fwrite("data", 1, 4, f);
    put32(f, dataSize);
    std::fwrite(data, 1, dataSize, f);
    std::fclose(f);
    return true;
}

const char* const kNames[Tr707::kNumSounds] = {
    "bass drum 1", "bass drum 2", "snare drum 1", "snare drum 2", "low tom", "mid tom",
    "high tom", "rim shot", "cowbell", "hand clap", "tambourine", "closed hi-hat",
    "open hi-hat", "crash", "ride",
};

struct Hit {
    double time;
    int sound, velocity;
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s out.wav IC34 IC35 IC19 IC22 [rate] [channels.wav] [727]\n",
                     argv[0]);
        return 2;
    }
    const char* outPath = argv[1];
    std::vector<uint8_t> ic34, ic35, voices, crash, ride;
    if (!readFile(argv[2], ic34, 0x8000) || !readFile(argv[3], ic35, 0x8000) ||
        !readFile(argv[4], crash, 0x8000) || !readFile(argv[5], ride, 0x8000))
        return 1;
    voices = ic34;
    voices.insert(voices.end(), ic35.begin(), ic35.end());
    const int rate = argc > 6 ? std::atoi(argv[6]) : 48000;
    const char* channelsPath = argc > 7 && std::strcmp(argv[7], "-") ? argv[7] : nullptr;
    const bool tr727 = argc > 8 && !std::strcmp(argv[8], "727");
    if (rate < 8000 || rate > 384000) {
        std::fprintf(stderr, "rate out of range\n");
        return 2;
    }

    // The hits: each sound at 127, 64, 24.
    std::vector<Hit> hits;
    double t = 0.25;
    for (int s = 0; s < Tr707::kNumSounds; ++s) {
        const double gap = s >= Tr707::kCrash ? 2.5 : s == Tr707::kOH ? 1.2 : 0.8;
        for (int v : {127, 64, 24}) {
            hits.push_back({t, s, v});
            t += gap;
        }
        t += 0.4;
    }
    const double seconds = t + 0.5;

    Tr707 engine(tr727);
    engine.setRoms({voices.data(), crash.data(), ride.data()});
    engine.reset();

    Resampler rs;
    rs.init(Tr707::kRate, rate, 2);

    const size_t outFrames = size_t(seconds * rate);
    std::vector<float> outL(outFrames), outR(outFrames);
    std::vector<std::vector<float>> chan(Tr707::kNumChannels);
    std::vector<float> inL, inR;
    std::vector<std::vector<float>> blockChan(Tr707::kNumChannels);
    long internalPos = 0;
    size_t nextHit = 0;

    constexpr int kBlock = 256;
    for (size_t pos = 0; pos < outFrames;) {
        const int nOut = int(std::min<size_t>(kBlock, outFrames - pos));
        const int nIn = rs.inputFor(nOut);
        inL.assign(nIn, 0.0f);
        inR.assign(nIn, 0.0f);
        float* chPtr[Tr707::kNumChannels];
        for (int c = 0; c < Tr707::kNumChannels; ++c) {
            blockChan[c].assign(nIn, 0.0f);
            chPtr[c] = blockChan[c].data();
        }
        // Render in pieces between hits, so each starts on its own internal sample.
        int done = 0;
        while (done < nIn) {
            int len = nIn - done;
            while (nextHit < hits.size()) {
                const long at = long(hits[nextHit].time * Tr707::kRate);
                if (at > internalPos + done) {
                    len = int(std::min<long>(len, at - (internalPos + done)));
                    break;
                }
                engine.trigger(hits[nextHit].sound, hits[nextHit].velocity);
                ++nextHit;
            }
            float* cp[Tr707::kNumChannels];
            for (int c = 0; c < Tr707::kNumChannels; ++c)
                cp[c] = chPtr[c] + done;
            engine.render(len, inL.data() + done, inR.data() + done, cp);
            done += len;
        }
        internalPos += nIn;
        if (channelsPath)
            for (int c = 0; c < Tr707::kNumChannels; ++c)
                chan[c].insert(chan[c].end(), blockChan[c].begin(), blockChan[c].end());
        const float* in[2] = {inL.data(), inR.data()};
        float* out[2] = {outL.data() + pos, outR.data() + pos};
        rs.process(in, nIn, out, nOut);
        pos += nOut;
    }

    // Statistics per hit, from the mix.
    double maxPeak = 0.0;
    for (size_t i = 0; i < outFrames; ++i)
        maxPeak = std::fmax(maxPeak, std::fmax(std::fabs(outL[i]), std::fabs(outR[i])));
    std::printf("%s, %d Hz, %.2f s, peak %.3f (%.1f dBFS)\n", tr727 ? "TR-727" : "TR-707", rate,
                seconds, maxPeak, 20.0 * std::log10(std::fmax(maxPeak, 1e-12)));
    for (size_t h = 0; h < hits.size(); ++h) {
        const size_t a = size_t(hits[h].time * rate);
        const size_t b = h + 1 < hits.size() ? size_t(hits[h + 1].time * rate) : outFrames;
        double peak = 0.0;
        for (size_t i = a; i < b; ++i)
            peak = std::fmax(peak, std::fmax(std::fabs(outL[i]), std::fabs(outR[i])));
        size_t last = a;
        for (size_t i = a; i < b; ++i)
            if (std::fmax(std::fabs(outL[i]), std::fabs(outR[i])) > peak * 1e-3)
                last = i;
        std::printf("%7.3f s  %-14s vel %3d  peak %.3f (%6.1f dB)  length %4.0f ms\n",
                    hits[h].time, kNames[hits[h].sound], hits[h].velocity, peak,
                    20.0 * std::log10(std::fmax(peak, 1e-12)), 1000.0 * (last - a) / rate);
    }

    // Normalise to -1 dBFS for listening; the factor is printed.
    const double gain = maxPeak > 0.0 ? std::pow(10.0, -1.0 / 20.0) / maxPeak : 1.0;
    std::printf("gain to -1 dBFS: %.3f\n", gain);
    std::vector<int16_t> pcm(outFrames * 2);
    for (size_t i = 0; i < outFrames; ++i) {
        pcm[2 * i] = int16_t(std::lrint(std::fmax(-1.0, std::fmin(1.0, outL[i] * gain)) * 32767.0));
        pcm[2 * i + 1] = int16_t(std::lrint(std::fmax(-1.0, std::fmin(1.0, outR[i] * gain)) * 32767.0));
    }
    if (!writeWav(outPath, rate, 2, 1, pcm.data(), outFrames))
        return 1;

    if (channelsPath) {
        const size_t n = chan[0].size();
        std::vector<float> inter(n * Tr707::kNumChannels);
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < Tr707::kNumChannels; ++c)
                inter[i * Tr707::kNumChannels + c] = chan[c][i];
        if (!writeWav(channelsPath, int(Tr707::kRate), Tr707::kNumChannels, 3, inter.data(), n))
            return 1;
    }
    return 0;
}
