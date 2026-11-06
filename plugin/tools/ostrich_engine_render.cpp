// SPDX-License-Identifier: GPL-3.0-or-later
/*
    Play a machine's engine without a plugin: every sound at three velocities, written as a
    stereo WAV at the host rate -- for listening and for checking the engines against the
    sound ROMs.

        ostrich_engine_render out.wav IC34 IC35 IC19 IC22 [rate] [channels.wav] [727]
        ostrich_engine_render 505 out.wav DUMP [rate]
        ostrich_engine_render 626 out.wav DUMP [rate]

    TR-707: IC34 ... IC22 are its sound ROM images (32 KiB each). `channels.wav` (optional,
    "-" for none) gets the ten mixer channels at the engine's internal 100 kHz, 32-bit
    float, for analysis. A last argument `727` plays the TR-727's board instead (with the
    same ROMs, as there are no TR-727 dumps yet). The environment's OSTRICH_HIGHS sets
    Tr707::setHighs.
    TR-505 / TR-626: DUMP is any known form of the sound ROM (roms/README.md); the sounds
    play in the order of their notes.
    `rate` defaults to 48000. Prints, for every hit, its time, peak and length (to -60 dB
    below its peak); the WAV is normalised to -1 dBFS (the factor is printed).
*/

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../ostrich/engine/PcmMachine.hpp"
#include "../ostrich/engine/Resampler.hpp"
#include "../ostrich/engine/Sha1.hpp"
#include "../ostrich/engine/SoundOrder.hpp"
#include "../ostrich/engine/Tr707.hpp"

using namespace ostrich;

namespace {

bool readFile(const char* path, std::vector<uint8_t>& out, size_t size = 0)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long have = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size && size_t(have) != size) {
        std::fprintf(stderr, "%s: %ld bytes, expected %zu\n", path, have, size);
        std::fclose(f);
        return false;
    }
    out.resize(size_t(have));
    const size_t n = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return n == out.size();
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

const char* const kTr707Names[Tr707::kNumSounds] = {
    "bass drum 1", "bass drum 2", "snare drum 1", "snare drum 2", "low tom", "mid tom",
    "high tom", "rim shot", "cowbell", "hand clap", "tambourine", "closed hi-hat",
    "open hi-hat", "crash", "ride",
};

struct Hit {
    double time;
    int sound, velocity;
    std::string name;
};

// What the render loop needs of an engine.
struct Engine {
    double rate;
    int channels;   // per-channel outputs (TR-707), else 0
    std::function<void(int, int)> trigger;
    std::function<void(int, float*, float*, float* const*)> render;
};

void addHits(std::vector<Hit>& hits, double& t, int sound, const std::string& name, double gap)
{
    for (int v : {127, 64, 24}) {
        hits.push_back({t, sound, v, name});
        t += gap;
    }
    t += 0.4;
}

} // namespace

int main(int argc, char** argv)
{
    const bool pcm = argc > 1 && (!std::strcmp(argv[1], "505") || !std::strcmp(argv[1], "626"));
    if ((pcm && argc < 4) || (!pcm && argc < 6)) {
        std::fprintf(stderr,
                     "usage: %s out.wav IC34 IC35 IC19 IC22 [rate] [channels.wav] [727]\n"
                     "       %s 505|626 out.wav DUMP [rate]\n",
                     argv[0], argv[0]);
        return 2;
    }

    std::vector<Hit> hits;
    double t = 0.25;
    std::string title;
    const char* outPath;
    const char* channelsPath = nullptr;
    int rate;
    Engine eng;

    // Engines and their data live for the whole run.
    std::vector<uint8_t> voices, crash, ride, rom;
    std::unique_ptr<Tr707> tr707;
    std::unique_ptr<PcmMachine> pcmMachine;

    if (pcm) {
        const bool is505 = !std::strcmp(argv[1], "505");
        outPath = argv[2];
        std::vector<uint8_t> dump;
        if (!readFile(argv[3], dump))
            return 1;
        const std::string sha = sha1Hex(dump.data(), dump.size());
        if (is505) {
            if (sha == "6441d3e7b53aff4511b23021dc854b7a5cc57689")
                rom = tr505SoundOrder(dump, true);
            else if (sha == "14e80b926aed1e78cb027752dfbbe51d9b3dcf4d")
                rom = tr505SoundOrder(dump, false);
            else if (sha == kTr505SoundOrderSha1)
                rom = dump;
        } else if (sha == "d7891edecc68a0652d2cad82a637430ee8568818") {
            rom = tr626SoundOrder(dump);
        }
        if (rom.empty()) {
            std::fprintf(stderr, "%s: not a known %s dump (sha1 %s)\n", argv[3], argv[1], sha.c_str());
            return 1;
        }
        const std::string want = is505 ? kTr505SoundOrderSha1 : kTr626SoundOrderSha1;
        const std::string got = sha1Hex(rom.data(), rom.size());
        std::printf("sound order sha1 %s (%s)\n", got.c_str(), got == want ? "as tools/*_rom.py" : "DIFFERS");
        rate = argc > 4 ? std::atoi(argv[4]) : 48000;

        const auto model = is505 ? PcmMachine::kTr505 : PcmMachine::kTr626;
        pcmMachine = std::make_unique<PcmMachine>(model);
        pcmMachine->setRom(rom.data());
        title = is505 ? "TR-505" : "TR-626";
        for (int note = 25; note < 100; ++note) {
            const int s = PcmMachine::noteToSound(model, note);
            if (s < 0)
                continue;
            const double len = pcmMachine->soundLength(s) / PcmMachine::kSampleRate;
            addHits(hits, t, s, "note " + std::to_string(note) + " (sound " + std::to_string(s) + ")",
                    std::fmax(0.8, len + 0.3));
        }
        PcmMachine* m = pcmMachine.get();
        eng = {PcmMachine::kRate, 0, [m](int s, int v) { m->trigger(s, v); },
               [m](int n, float* l, float* r, float* const*) { m->render(n, l, r); }};
    } else {
        outPath = argv[1];
        std::vector<uint8_t> ic34, ic35;
        if (!readFile(argv[2], ic34, 0x8000) || !readFile(argv[3], ic35, 0x8000) ||
            !readFile(argv[4], crash, 0x8000) || !readFile(argv[5], ride, 0x8000))
            return 1;
        voices = ic34;
        voices.insert(voices.end(), ic35.begin(), ic35.end());
        rate = argc > 6 ? std::atoi(argv[6]) : 48000;
        channelsPath = argc > 7 && std::strcmp(argv[7], "-") ? argv[7] : nullptr;
        const bool tr727 = argc > 8 && !std::strcmp(argv[8], "727");
        title = tr727 ? "TR-727" : "TR-707";
        tr707 = std::make_unique<Tr707>(tr727);
        if (const char* highs = std::getenv("OSTRICH_HIGHS"))
            tr707->setHighs(std::atof(highs));
        tr707->setRoms({voices.data(), crash.data(), ride.data()});
        tr707->reset();
        for (int s = 0; s < Tr707::kNumSounds; ++s)
            addHits(hits, t, s, kTr707Names[s], s >= Tr707::kCrash ? 2.5 : s == Tr707::kOH ? 1.2 : 0.8);
        Tr707* m = tr707.get();
        eng = {Tr707::kRate, Tr707::kNumChannels, [m](int s, int v) { m->trigger(s, v); },
               [m](int n, float* l, float* r, float* const* c) { m->render(n, l, r, c); }};
    }
    if (rate < 8000 || rate > 384000) {
        std::fprintf(stderr, "rate out of range\n");
        return 2;
    }
    const double seconds = t + 0.5;

    Resampler rs;
    rs.init(eng.rate, rate, 2);

    const size_t outFrames = size_t(seconds * rate);
    std::vector<float> outL(outFrames), outR(outFrames);
    std::vector<std::vector<float>> chan(eng.channels), blockChan(eng.channels);
    std::vector<float> inL, inR;
    long internalPos = 0;
    size_t nextHit = 0;

    constexpr int kBlock = 256;
    for (size_t pos = 0; pos < outFrames;) {
        const int nOut = int(std::min<size_t>(kBlock, outFrames - pos));
        const int nIn = rs.inputFor(nOut);
        inL.assign(nIn, 0.0f);
        inR.assign(nIn, 0.0f);
        for (auto& c : blockChan)
            c.assign(nIn, 0.0f);
        // Render in pieces between hits, so each starts on its own internal sample.
        int done = 0;
        while (done < nIn) {
            int len = nIn - done;
            while (nextHit < hits.size()) {
                const long at = long(hits[nextHit].time * eng.rate);
                if (at > internalPos + done) {
                    len = int(std::min<long>(len, at - (internalPos + done)));
                    break;
                }
                eng.trigger(hits[nextHit].sound, hits[nextHit].velocity);
                ++nextHit;
            }
            std::vector<float*> cp;
            for (auto& c : blockChan)
                cp.push_back(c.data() + done);
            eng.render(len, inL.data() + done, inR.data() + done, cp.empty() ? nullptr : cp.data());
            done += len;
        }
        internalPos += nIn;
        if (channelsPath)
            for (int c = 0; c < eng.channels; ++c)
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
    std::printf("%s, %d Hz, %.2f s, peak %.3f (%.1f dBFS)\n", title.c_str(), rate, seconds,
                maxPeak, 20.0 * std::log10(std::fmax(maxPeak, 1e-12)));
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
        std::printf("%7.3f s  %-22s vel %3d  peak %.3f (%6.1f dB)  length %4.0f ms\n",
                    hits[h].time, hits[h].name.c_str(), hits[h].velocity, peak,
                    20.0 * std::log10(std::fmax(peak, 1e-12)), 1000.0 * (last - a) / rate);
    }

    const double gain = maxPeak > 0.0 ? std::pow(10.0, -1.0 / 20.0) / maxPeak : 1.0;
    std::printf("gain to -1 dBFS: %.3f\n", gain);
    std::vector<int16_t> out16(outFrames * 2);
    for (size_t i = 0; i < outFrames; ++i) {
        out16[2 * i] = int16_t(std::lrint(std::fmax(-1.0, std::fmin(1.0, outL[i] * gain)) * 32767.0));
        out16[2 * i + 1] = int16_t(std::lrint(std::fmax(-1.0, std::fmin(1.0, outR[i] * gain)) * 32767.0));
    }
    if (!writeWav(outPath, rate, 2, 1, out16.data(), outFrames))
        return 1;

    if (channelsPath) {
        const size_t n = chan[0].size();
        std::vector<float> inter(n * eng.channels);
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < eng.channels; ++c)
                inter[i * eng.channels + c] = chan[c][i];
        if (!writeWav(channelsPath, int(eng.rate), eng.channels, 3, inter.data(), n))
            return 1;
    }
    return 0;
}
