// Host timing only; never a Blackfin cycle count or a catalog CPU record.
#include "sample.cpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace granular;
int main(int argc, char** argv) {
    const i32 density = argc > 1 ? static_cast<i32>(std::atof(argv[1]) * 2147483647.0) : 2147483647;
    for (unsigned channels = 1; channels <= 2; ++channels) {
        std::vector<i16> pcm(88200 * channels);
        for (unsigned i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<i16>((i * 1597) & 65535);
        Voice voice{}; initialize(voice, opxy::SampleView{pcm.data(), 88200, 44100, channels});
        i32 output[128], parameters[8] = {257698038, 1073741824, density, 214748364, 1073741824, 1073741824, 644245094, 2147483647};
        u32 pitch[64]; for (auto& p : pitch) p = 25479657;
        opxy::SampleBlock block{}; block.output = output; block.parameters = parameters; block.pitch = pitch;
        block.region = opxy::SampleRegion{0, 88200, opxy::sample_unity_level, false, 0}; block.output_channels = 2;
        volatile i32 sink = 0; double times[5];
        for (unsigned run = 0; run < 5; ++run) {
            start(voice);
            const auto begin = std::chrono::steady_clock::now();
            for (unsigned b = 0; b < 50000; ++b) {
                render_sample(voice, block); sink = output[b & 127];
            }
            times[run] = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / 50000;
        }
        std::sort(times, times + 5);
        std::printf("source_channels=%u density_control=%.3f host_ns_per_block=%.1f\n", channels, double(density)/2147483647, times[2]);
        (void)sink;
    }
}
