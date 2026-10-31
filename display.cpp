#include "display.h"
#include "granular.h"
#include <opxy/sample_display.hpp>

namespace granular {
namespace {
void integer(char* out, u32 value) {
    char reversed[10]; u32 n = 0;
    do { reversed[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    u32 i = 0; while (n) out[i++] = reversed[--n]; out[i] = 0;
}
void caption(char* out, u32 slot, i32 raw) {
    const u32 value = unsigned_q31(raw);
    if (slot == 1) { integer(out, anchor(value, kSizeFrames, 7) * 1000 / 44100); return; }
    if (slot == 2) {
        const u32 tenths = anchor(value, kDensityQ8, 5) * 10 / 256;
        out[0] = static_cast<char>('0' + tenths / 10); out[1] = '.';
        out[2] = static_cast<char>('0' + tenths % 10); out[3] = 'x'; out[4] = 0; return;
    }
    if (slot == 3) { integer(out, mul_shift(value, 2000, 31)); return; }
    if (slot == 4 || slot == 5) {
        const i32 centered = static_cast<i32>(value >> 15) - 32768;
        const u32 drift = static_cast<u32>(centered < 0 ? -centered : centered);
        const i32 scan_speed = drift < 3277 ? 0 : static_cast<i32>((drift - 3276) * 200 / 29492);
        const i32 mapped = slot == 4 ? (centered < 0 ? -scan_speed : scan_speed) :
            static_cast<i32>(mul_shift(value, 240, 31)) - 120;
        const u32 magnitude = static_cast<u32>(mapped < 0 ? -mapped : mapped);
        out[0] = mapped < 0 ? '-' : '+';
        if (slot == 4) {
            out[1] = static_cast<char>('0' + magnitude / 100); out[2] = '.';
            out[3] = static_cast<char>('0' + magnitude / 10 % 10); out[4] = 'x'; out[5] = 0;
        } else {
            integer(out + 1, magnitude / 10);
            u32 n = 1; while (out[n]) ++n;
            out[n++] = '.'; out[n++] = static_cast<char>('0' + magnitude % 10); out[n] = 0;
        }
        return;
    }
    integer(out, mul_shift(value, 100, 31));
}
}
void display_reset(DisplayState& state) { state.geometry = 0; }
void display(DisplayState& state, const opxy::draw::Input& input, Frame& output) {
    using namespace opxy;
    using namespace opxy::draw;
    if (!++state.geometry) ++state.geometry;
    for (u32 i = 0; i < Frame::node_count; ++i) hide(output, i);
    const char* const names[8] = {"POSITION %", "SIZE ms", "DENSITY", "SPRAY ms", "SCAN", "TUNE st", "SPREAD %", "VOLUME %"};
    for (u32 i = 0; i < 4; ++i) {
        const u32 slot = input.shown[i] < 8 ? input.shown[i] : i;
        char value[header_capacity] = {};
        caption(value, slot, input.slots[slot]);
        text(output, i, (8 + 120 * i) * unit, 12 * unit, unit, names[slot]);
        text(output, 4 + i, (8 + 120 * i) * unit, 34 * unit, unit, value);
        header(output, i, value);
    }
    text(output, 8, 16 * unit, 59 * unit, unit, input.shown[0] >= 4 ? "GRAINS / PAGE 2 OF 2" : "GRAINS / PAGE 1 OF 2");
    text(output, 10, 16 * unit, 192 * unit, unit, "LATEST STARTED VOICE");
    text(output, 11, 16 * unit, 212 * unit, unit, "PAGE 1: CONTROLS / TRIM");
    SampleDisplaySnapshot snapshot{};
    const bool available = sample_display_snapshot(input.track, snapshot);
    const bool waveform = available && snapshot.frames && (snapshot.flags & sample_waveform_ready);
    text(output, 9, 16 * unit, 173 * unit, unit, !available ? "SAMPLE DATA PENDING" : !snapshot.frames ? "NO SAMPLE ASSIGNED" :
        !waveform ? "WAVEFORM PENDING" : (snapshot.flags & sample_waveform_approximate) ? "SAMPLED WAVEFORM" : "SAMPLE WAVEFORM");
    for (u32 p = 0; p < 4; ++p) {
        output.paths[p].key = state.geometry;
        // Each 32-column envelope is a closed strip, not retraced zero-area lines.
        for (u32 x = 0; x < 32; ++x) {
            const u32 bin = p * 32 + x;
            const i32 high = waveform ? snapshot.maximum[bin] : 0;
            const i32 low = waveform ? snapshot.minimum[bin] : 0;
            const i32 px = (static_cast<i32>(x) * 7 - 109) * unit / 2;
            const i32 top = -high * 38 * unit / 32768 - unit / 4;
            const i32 bottom = -low * 38 * unit / 32768 + unit / 4;
            output.paths[p].points[x] = Point{px, top};
            output.paths[p].points[63 - x] = Point{px, bottom};
        }
        if (waveform) place(output, p, (72 + 112 * p) * unit, 115 * unit, unit, 1, fill_and_stroke);
    }
    if (!waveform || !(snapshot.flags & sample_voice_live)) return;
    for (u32 i = 0; i < snapshot.marker_count && i < 4; ++i) {
        const SampleMarker& marker = snapshot.markers[i];
        if (marker.frame >= snapshot.frames) continue;
        const u32 x = mul_shift(marker.frame, 448, 0) / snapshot.frames;
        place(output, 4 + i, (16 + x) * unit, 115 * unit, unit,
            marker.flags & 1u ? 3 : 2, fill_and_stroke);
    }
}
}
