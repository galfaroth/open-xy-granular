#include "sample.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace granular;
static void require(bool ok, const char* message) { if (!ok) { std::puts(message); std::exit(1); } }
int main() {
    i16 pcm[64] = {};
    Voice voice{};
    initialize(voice, opxy::SampleView{pcm, 64, 44100, 1});
    opxy::SampleVisual result{};
    voice.grains[0] = Grain{(12u << kFraction) + 123u, 4096, 0x12345678u, 17, 32767, 32767};
    voice.grains[1] = Grain{32u << kFraction, -4096, 0xffff0000u, 1, 32767, 32767};
    voice.grains[2] = Grain{20u << kFraction, 4096, 123, 0, 32767, 32767};
    voice.grains[3] = Grain{64u << kFraction, 4096, 123, 1, 32767, 32767};
    const Voice before = voice;
    sample_visual(voice, result);
    require(result.count == 2, "only active in-range grains");
    require(result.markers[0].frame == 12 && result.markers[0].phase_q16 == 0x1234 && result.markers[0].flags == 0,
        "forward actual grain position and phase");
    require(result.markers[1].frame == 32 && result.markers[1].phase_q16 == 65535 && result.markers[1].flags == 1,
        "reverse actual grain position and phase");
    require(!std::memcmp(&voice, &before, sizeof(voice)), "telemetry cannot mutate DSP");
    voice.grains[0].position += 4096;
    sample_visual(voice, result);
    require(result.markers[0].frame == 13, "telemetry follows actual state not elapsed time");
    for (u32 i = 0; i < 4; ++i) voice.grains[i] = Grain{i << kFraction, 1, i << 16, 1, 1, 1};
    sample_visual(voice, result); require(result.count == 4, "bounded four markers");
    start(voice); sample_visual(voice, result); require(!result.count && !result.markers[0].frame, "inactive clears old markers");
    voice.grains[0].window_step = 1;
    voice.sample.data = nullptr; sample_visual(voice, result); require(!result.count, "missing PCM rejected");
    voice.sample.data = pcm; voice.sample.frames = 1; sample_visual(voice, result); require(!result.count, "short sample rejected");
    voice.sample.frames = 1u << 20; sample_visual(voice, result); require(!result.count, "unrepresentable sample rejected");
    voice.sample.frames = 64; voice.sample.rate = 0; sample_visual(voice, result); require(!result.count, "invalid rate rejected");
}
