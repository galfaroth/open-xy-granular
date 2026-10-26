#include "sample.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <climits>
using namespace granular;
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct Fixture {
    std::vector<i16> pcm;
    Voice voice{};
    i32 output[130]{};
    i32 parameters[8] = {257698038, 1073741824, 1073741824, 0, 1073741824, 1073741824, 0, 2147483647};
    u32 pitch[64];
    opxy::SampleBlock block{};
    Fixture(u32 frames = 88200, u32 channels = 1, u32 rate = 44100) : pcm(frames * channels) {
        for (u32 f = 0; f < frames; ++f)
            for (u32 c = 0; c < channels; ++c)
                pcm[f * channels + c] = static_cast<i16>(22000 * std::sin(f * 0.063 + c));
        initialize(voice, opxy::SampleView{pcm.data(), frames, rate, channels}); start(voice);
        for (auto& p : pitch) p = 25479657;
        block.output = output + 1; block.parameters = parameters; block.pitch = pitch;
        block.region = opxy::SampleRegion{0, frames, opxy::sample_unity_level, false, 0};
        block.output_channels = 2;
    }
    long long render(unsigned blocks) {
        long long energy = 0;
        for (unsigned b = 0; b < blocks; ++b) {
            for (auto& value : output) value = INT_MAX;
            render_sample(voice, block);
            const unsigned n = 64 * block.output_channels;
            check(output[0] == INT_MAX && output[n + 1] == INT_MAX, "output overrun");
            for (unsigned i = 1; i <= n; ++i) {
                check(output[i] >= -1073741824 && output[i] <= 1073741824, "output bound/unwritten");
                energy += std::abs(output[i]);
            }
        }
        return energy;
    }
};
int main() {
    for (u32 source = 1; source <= 2; ++source) {
        for (u32 out = 1; out <= 2; ++out) {
            Fixture f(88200, source); f.block.output_channels = out;
            check(f.render(200) > 0, "silent valid sample");
            check(f.voice.scan_position == 0, "center scan must freeze");
            f.parameters[4] = INT_MAX; f.render(1); const u32 forward = f.voice.scan_position;
            check(forward > 0 && forward < 88200u * kUnit / 2, "scan forward");
            f.parameters[4] = 0; f.render(1);
            check(f.voice.scan_position < forward || f.voice.scan_position > 88200u * kUnit - forward, "scan backward");
            f.block.region.reverse = true; check(f.render(200) > 0, "reverse silent");
            f.parameters[7] = 0; check(f.render(4) == 0, "volume mute");
        }
    }
    for (u32 frames : {0u, 1u, 2u, 8u, 64u, 882000u}) {
        Fixture f(frames, 2);
        for (bool reverse : {false, true}) {
            f.block.region.reverse = reverse;
            f.parameters[0] = INT_MAX; f.parameters[3] = INT_MAX;
            f.parameters[2] = INT_MAX; f.parameters[6] = INT_MAX;
            f.render(32);
            f.block.region.begin = frames; check(f.render(1) == 0, "empty trim");
            f.block.region.begin = 0;
        }
    }
    // An active grain outside newly moved trim must never leak old audio.
    Fixture trim; trim.render(100);
    for (auto& s : trim.pcm) s = 0;
    for (unsigned i = 1000; i < 2000; ++i) trim.pcm[i] = 30000;
    trim.block.region.begin = 5000; trim.block.region.end = 5100;
    check(trim.render(100) == 0, "audio outside new trim");
    for (u32 rate : {8000u, 22050u, 48000u, 96000u, 192000u}) {
        Fixture f(88200, 2, rate); check(f.render(200) > 0, "sample rate");
    }
    Fixture invalid; invalid.voice.sample.data = nullptr; check(invalid.render(2) == 0, "null sample");
    Fixture excessive; excessive.voice.sample.frames = 1u << 20; check(excessive.render(2) == 0, "position overflow");
    // Deterministic parameter/trim motion exercises live boundaries and extreme pitch.
    Fixture sweep(4096, 2); u32 seed = 42;
    for (unsigned b = 0; b < 4000; ++b) {
        for (auto& p : sweep.parameters) p = static_cast<i32>((seed = seed * 1664525u + 1013904223u) >> 1);
        sweep.block.region.begin = seed % 4096;
        sweep.block.region.end = (seed >> 16) % 4097;
        sweep.block.region.reverse = (seed & 1) != 0;
        sweep.block.region.pitch_cents = b & 1 ? INT_MAX : INT_MIN;
        sweep.render(1);
    }
    // Four grain slots must sustain the advertised maximum overlap.
    Fixture dense; dense.parameters[2] = INT_MAX; dense.render(100);
    unsigned active = 0; for (const auto& grain : dense.voice.grains) active += grain.window_step != 0;
    check(active == 4, "maximum density not reached");
    std::puts("PASS: sound, freeze/scan, stereo, reverse, rates, trims, bounds and density");
}
