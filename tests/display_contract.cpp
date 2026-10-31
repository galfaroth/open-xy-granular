#include "display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static bool available;
static opxy::SampleDisplaySnapshot snapshot;
namespace opxy { bool sample_display_snapshot(u32 track, SampleDisplaySnapshot& out) { out = snapshot; out.track = track; return available; } }
static void require(bool ok, const char* message) { if (!ok) { std::puts(message); std::exit(1); } }
int main() {
    granular::DisplayState state{};
    granular::Frame frame{};
    opxy::draw::Input input{};
    granular::display_reset(state);
    for (opxy::u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
    granular::display(state, input, frame);
    require(opxy::draw::valid(frame), "pending frame valid");
    require(!std::strcmp(frame.texts[9].chars, "SAMPLE DATA PENDING"), "pending explicit");
    require(!std::strcmp(frame.texts[11].chars, "PAGE 1: CONTROLS / TRIM"), "physical page toggle help");
    available = true;
    granular::display(state, input, frame);
    require(!std::strcmp(frame.texts[9].chars, "NO SAMPLE ASSIGNED"), "empty sample explicit");
    snapshot.frames = 1000;
    snapshot.flags = opxy::sample_waveform_ready | opxy::sample_voice_live | opxy::sample_waveform_approximate;
    for (opxy::u32 i = 0; i < 128; ++i) { snapshot.minimum[i] = -32768; snapshot.maximum[i] = 32767; }
    snapshot.marker_count = 2;
    snapshot.markers[0] = opxy::SampleMarker{250, 32768, 0};
    snapshot.markers[1] = opxy::SampleMarker{750, 65535, 1};
    granular::display(state, input, frame);
    require(opxy::draw::valid(frame), "fullscale waveform valid");
    require(frame.nodes[4].x == 128 * opxy::draw::unit && frame.nodes[5].x == 352 * opxy::draw::unit,
        "marker coordinates follow actual frames");
    require(frame.nodes[5].shade == 3, "reverse marker distinct");
    require(frame.nodes[6].x == opxy::draw::hidden_x * opxy::draw::unit, "unused marker hidden");
    for (opxy::u32 i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i + 4);
    granular::display(state, input, frame);
    require(!std::strcmp(frame.texts[0].chars, "SCAN") && !std::strcmp(frame.texts[8].chars, "GRAINS / PAGE 2 OF 2"), "page two shows alternate labels");
    input.slots[4] = 1073741824;
    granular::display(state, input, frame);
    require(!std::strcmp(frame.header[0], "+0.0x"), "scan freeze has truthful caption");
    available = false;
    granular::display(state, input, frame);
    require(frame.nodes[4].x == opxy::draw::hidden_x * opxy::draw::unit && frame.nodes[0].x == opxy::draw::hidden_x * opxy::draw::unit,
        "stale snapshots never remain visible");
    require(opxy::draw::valid(frame), "cleared frame valid");
}
