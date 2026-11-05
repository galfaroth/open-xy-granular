// Screen checks: the waveform follows the sample overview, empty and loading
// states say so, the header pages and shows Shift's stock controls, and the
// simulated grains stay bounded inside the played region and stop with notes.
#include "display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static opxy::draw::SampleOverview overview;
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }
static void require(bool ok, const char* message) { if (!ok) { std::puts(message); std::exit(1); } }

using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

static i32 at(double value) { return value >= 1 ? 2147483647 : static_cast<i32>(value * 2147483648.0); }
static i32 node_x(const granular::Frame& frame, u32 node) { return frame.nodes[node].x; }
static bool hidden(const granular::Frame& frame, u32 node) { return frame.nodes[node].x == hidden_x * unit; }
static u32 grains_shown(const granular::Frame& frame) {
    u32 n = 0;
    for (u32 i = 0; i < granular::grain_shown; ++i) n += !hidden(frame, granular::grain_first + i);
    return n;
}

int main() {
    using namespace granular;
    DisplayState state{};
    granular::Frame frame{};
    Input input{};
    const double defaults[8] = {0.12, 0.5, 0.5, 0.1, 0.5, 0.5, 0.3, 1};
    for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
    for (u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
    input.track = 1;
    input.elapsed = 17;
    display_reset(state);

    // No sample, then loading: explicit, nothing stale drawn.
    overview.gain = gain_unavailable;
    display(state, input, frame);
    require(valid(frame), "empty frame valid");
    require(!std::strcmp(frame.texts[status_text].chars, "NO SAMPLE LOADED"), "empty sample explicit");
    require(hidden(frame, wave_first) && hidden(frame, position_node), "no waveform or markers without a sample");
    require(!std::strcmp(frame.texts[0].chars, "position") && !std::strcmp(frame.header[1], "100"), "page one header");
    overview.assigned = 1;
    overview.gain = 0;
    display(state, input, frame);
    require(!std::strcmp(frame.texts[status_text].chars, "LOADING SAMPLE"), "loading explicit");
    require(hidden(frame, wave_first), "no waveform while loading");

    // Loaded: a full-scale waveform, markers within the trimmed region.
    overview.loaded = 1;
    overview.generation = 7;
    overview.frames = 88200;
    overview.rate = 44100;
    overview.channels = 1;
    overview.begin = 22050;
    overview.end = 66150;
    for (u32 i = 0; i < overview_bins; ++i) { overview.low[i] = -32768; overview.high[i] = 32767; }
    display(state, input, frame);
    require(valid(frame), "full-scale waveform valid");
    require(!std::strcmp(frame.texts[status_text].chars, ""), "status cleared when loaded");
    require(!hidden(frame, wave_first) && frame.paths[0].key == 15, "waveform keyed by overview generation");
    require(node_x(frame, trim_first) == 128 * unit && node_x(frame, trim_first + 1) == 352 * unit,
            "outer markers follow the overview's begin and end");
    const i32 position = node_x(frame, position_node);
    require(position >= 128 * unit && position <= 352 * unit, "position inside the region");
    require(!grains_shown(frame), "idle screen has no grains");

    // A held note: grains appear, stay in the region, never exceed four.
    input.held_notes = 1;
    input.note_serial = 1;
    u32 most = 0;
    for (u32 f = 0; f < 300; ++f) {
        display(state, input, frame);
        require(valid(frame), "animated frame valid");
        const u32 n = grains_shown(frame);
        most = n > most ? n : most;
        for (u32 i = 0; i < grain_shown; ++i) {
            if (hidden(frame, grain_first + i)) continue;
            const i32 x = node_x(frame, grain_first + i);
            require(x >= 128 * unit && x <= 352 * unit, "grain inside the played region");
        }
    }
    require(most >= 1 && most <= grain_shown, "grains shown while held, bounded to four");
    // Determinism: a fresh state over the same inputs draws the same frames.
    {
        DisplayState a{}, b{};
        granular::Frame fa{}, fb{};
        Input quiet = input;
        quiet.held_notes = 0;
        display_reset(a); display_reset(b);
        for (u32 f = 0; f < 120; ++f) {
            const Input& in = f < 2 ? quiet : input;
            display(a, in, fa); display(b, in, fb);
            for (u32 i = 0; i < granular::Frame::node_count; ++i)
                require(fa.nodes[i].x == fb.nodes[i].x && fa.nodes[i].scale == fb.nodes[i].scale, "deterministic grains");
        }
    }

    // Release: no new grains; the last ones finish within one grain length.
    input.held_notes = 0;
    input.note_off_serial = 1;
    for (u32 f = 0; f < 40; ++f) display(state, input, frame);
    require(!grains_shown(frame), "grains end after release");

    // Reverse playback colours grains differently.
    overview.reverse = 1;
    input.held_notes = 1;
    input.note_serial = 2;
    bool reverse_seen = false;
    for (u32 f = 0; f < 60; ++f) {
        display(state, input, frame);
        for (u32 i = 0; i < grain_shown; ++i)
            if (!hidden(frame, grain_first + i)) reverse_seen = reverse_seen || frame.nodes[grain_first + i].shade == shade_reverse;
    }
    require(reverse_seen, "reverse grains distinct");
    overview.reverse = 0;

    // Reduced motion: no moving grains.
    input.reduced_motion = 1;
    display(state, input, frame);
    require(!grains_shown(frame), "reduced motion hides the cloud");
    input.reduced_motion = 0;

    // Page two and its captions.
    for (u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i + 4);
    input.slots[4] = 1073741824;
    for (u32 f = 0; f < 20; ++f) display(state, input, frame);
    require(!std::strcmp(frame.header[0], "+0.0x"), "scan freeze has truthful caption");
    require(!std::strcmp(frame.texts[0].chars, "scan"), "page two edited row shows its labels");

    // Shift shows the stock sampler controls it edits (stockShift).
    input.shift = 1;
    overview.gain = -6;
    display(state, input, frame);
    require(!std::strcmp(frame.texts[0].chars, "start") && !std::strcmp(frame.header[0], "25"), "shift start");
    require(!std::strcmp(frame.header[1], "75") && !std::strcmp(frame.header[2], "50"), "shift end and window");
    require(!std::strcmp(frame.header[3], "-6"), "shift gain");
    require(frame.nodes[trim_first].shade == shade_bright, "outer markers bright under shift");
    input.shift = 0;

    // A sample replaced by an empty track hides everything again.
    overview = SampleOverview{};
    overview.gain = gain_unavailable;
    display(state, input, frame);
    require(hidden(frame, wave_first) && hidden(frame, position_node) && !grains_shown(frame), "stale overview never remains visible");
    require(valid(frame), "cleared frame valid");
    std::puts("display contract ok");
}
