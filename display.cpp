#include "display.h"
#include "granular.h"

// The screen: the sample's waveform from its overview (sample_overview.hpp),
// the sample's start and end markers, the position and spray the controls set,
// and a cloud of grain markers simulated from the controls while notes are held.
// Nothing here reads audio state: the markers follow the same arithmetic as the
// renderer, not its voices.
namespace granular {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::draw::SampleOverview;

const i32 strip_left = 16, strip_width = 448, strip_middle = 125, wave_height = 50;
const u32 strip_q8 = static_cast<u32>(strip_width) << 8;

void integer(char* out, u32 value) {
    char reversed[10]; u32 n = 0;
    do { reversed[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    u32 i = 0; while (n) out[i++] = reversed[--n]; out[i] = 0;
}
void signed_integer(char* out, i32 value) {
    out[0] = value < 0 ? '-' : '+';
    integer(out + 1, static_cast<u32>(value < 0 ? -value : value));
}

// Scan's speed in hundredths of real time, -200..200; the middle tenth freezes.
i32 scan_hundredths(i32 raw) {
    const i32 centered = static_cast<i32>(unsigned_q31(raw) >> 15) - 32768;
    const u32 drift = static_cast<u32>(centered < 0 ? -centered : centered);
    const i32 speed = drift < 3277 ? 0 : static_cast<i32>((drift - 3276) * 200 / 29492);
    return centered < 0 ? -speed : speed;
}
u32 size_ms(i32 raw) { return anchor(unsigned_q31(raw), kSizeFrames, 7) * 1000 / 44100; }

void caption(char* out, u32 slot, i32 raw) {
    const u32 value = unsigned_q31(raw);
    if (slot == 1) { integer(out, size_ms(raw)); return; }
    if (slot == 2) {
        const u32 tenths = anchor(value, kDensityQ8, 5) * 10 / 256;
        out[0] = static_cast<char>('0' + tenths / 10); out[1] = '.';
        out[2] = static_cast<char>('0' + tenths % 10); out[3] = 'x'; out[4] = 0; return;
    }
    if (slot == 3) { integer(out, mul_shift(value, 2000, 31)); return; }
    if (slot == 4 || slot == 5) {
        const i32 mapped = slot == 4 ? scan_hundredths(raw) : static_cast<i32>(mul_shift(value, 240, 31)) - 120;
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

// A sample frame's place on the strip, Q8 pixels from its left edge.
u32 strip_x(u32 frame, u32 frames) {
    if (!frames) return 0;
    if (frame >= frames) return strip_q8;
    const u32 whole = frame * static_cast<u32>(strip_width) / frames;
    const u32 rest = frame * static_cast<u32>(strip_width) % frames;
    return (whole << 8) + (rest << 8) / frames;
}
// A rate in frames per second as Q8 pixels per second, bounded: past this the
// strip only wraps.
u32 strip_speed(u32 frames_per_second, u32 frames) {
    if (!frames) return 0;
    const u32 whole = frames_per_second / frames;
    if (whole >= (1u << 11)) return 1u << 28;
    return (whole * static_cast<u32>(strip_width) +
            frames_per_second % frames * static_cast<u32>(strip_width) / frames) << 8;
}
// value * ms / 1000 without overflowing for value up to 2^28 and ms up to 1000.
u32 per_ms(u32 value, u32 ms) { return value / 1000 * ms + value % 1000 * ms / 1000; }
u32 next_random(u32& state) { return state = state * 1664525u + 1013904223u; }

// The part of the strip the engine plays, from the overview's outer markers.
struct Region { u32 begin, length; bool reverse; u32 rate, frames; };

Region region(const SampleOverview& o) {
    Region r{};
    if (!o.assigned || !o.loaded || o.frames < 2) return r;
    const u32 end = o.end < o.frames ? o.end : o.frames;
    const u32 begin = o.begin < end ? o.begin : end;
    r.begin = strip_x(begin, o.frames);
    const u32 last = strip_x(end, o.frames);
    r.length = last > r.begin ? last - r.begin : 0;
    r.reverse = o.reverse != 0;
    r.rate = o.rate >= 8000 && o.rate <= 192000 ? o.rate : 44100;
    r.frames = o.frames;
    return r;
}

void spawn(DisplayState& state, const opxy::draw::Input& input, const Region& r) {
    ShownGrain* grain = 0;
    for (u32 i = 0; i < grain_shown; ++i)
        if (!state.grains[i].life) { grain = &state.grains[i]; break; }
    if (!grain || r.length < 256) return;
    // Playback speed at the root key: the source rate and Tune, as the renderer.
    const i32 tune_cents = static_cast<i32>(mul_shift(unsigned_q31(input.slots[5]), 2400, 31)) - 1200;
    const u32 frames_per_second = mul_shift(r.rate, ratio_q14(tune_cents), 14);
    const u32 speed = strip_speed(frames_per_second, r.frames);
    const u32 life = size_ms(input.slots[1]);
    u32 span = per_ms(speed, life);
    if (span > r.length) span = r.length;
    // Position, scan and spray, modulo the region, as the renderer spawns.
    u32 relative = mul_shift(r.length, unsigned_q31(input.slots[0]), 31);
    if (relative >= r.length) relative = r.length - 1;
    relative = add_mod(relative, state.scan % r.length, r.length);
    u32 spray = strip_x(mul_shift(unsigned_q31(input.slots[3]), 2000, 31) * (r.rate / 10) / 100, r.frames);
    if (spray > r.length / 2) spray = r.length / 2;
    if (spray) {
        relative = add_mod(relative, mul_shift(next_random(state.random), spray << 1, 32) % r.length, r.length);
        relative = add_mod(relative, r.length - spray, r.length);
    }
    // Keep the whole grain inside the region, so it ends on its window.
    if (!r.reverse) { if (relative > r.length - span) relative = r.length - span; }
    else if (relative < span) relative = span;
    grain->x = r.begin + relative;
    grain->velocity = r.reverse ? -static_cast<i32>(speed) : static_cast<i32>(speed);
    grain->age = 0;
    grain->life = static_cast<opxy::u16>(life ? life : 1);
}

void place_waveform(Frame& frame, const SampleOverview& o) {
    using namespace opxy::draw;
    const bool shown = o.assigned && o.loaded && o.frames;
    // Odd keys for loaded overviews, 2 for the flat strip: never confused.
    const u32 key = shown ? (o.generation << 1) | 1u : 2u;
    for (u32 p = 0; p < wave_paths; ++p) {
        frame.paths[p].key = key;
        // 128 columns over the 96 bins; each strip of 32 is one closed outline.
        for (u32 x = 0; x < 32; ++x) {
            const u32 bin = (p * 32 + x) * overview_bins / 128;
            const i32 high = shown ? o.high[bin] : 0, low = shown ? o.low[bin] : 0;
            const i32 px = (static_cast<i32>(x) * 7 - 109) * unit / 2;
            frame.paths[p].points[x] = Point{px, -high * wave_height * unit / 32768 - unit / 4};
            frame.paths[p].points[63 - x] = Point{px, -low * wave_height * unit / 32768 + unit / 4};
        }
        if (shown) place(frame, wave_first + p, (72 + 112 * static_cast<i32>(p)) * unit, strip_middle * unit, unit,
                         shade_wave, fill_and_stroke);
        else hide(frame, wave_first + p);
    }
}

void place_marker(Frame& frame, u32 node, u32 x_q8, i32 scale, u32 shade) {
    using namespace opxy::draw;
    place(frame, node, strip_left * unit + static_cast<i32>(x_q8 << 2), strip_middle * unit, scale, shade, fill_and_stroke);
}

void header_row(DisplayState& state, const opxy::draw::Input& input, Frame& frame, const SampleOverview& o) {
    using namespace opxy::draw;
    const char* titles[8] = {"position", "size", "density", "spray", "scan", "tune", "spread", "volume"};
    char values[8][header_capacity] = {};
    for (u32 slot = 0; slot < 8; ++slot) caption(values[slot], slot, input.slots[slot]);
    if (input.shift) {
        // Shift edits the stock sampler's start, end, window and gain
        // (resources.stockShift): show them on the edited row.
        u32 first = 0;
        for (u32 col = 0; col < 4; ++col) if (input.shown[col] >= 4) first = 4;
        static const char* const stock[4] = {"start", "end", "window", "gain"};
        const bool known = o.assigned && o.loaded && o.frames;
        const u32 end = known && o.end < o.frames ? o.end : known ? o.frames : 0;
        const u32 begin = o.begin < end ? o.begin : end;
        for (u32 col = 0; col < 4; ++col) {
            titles[first + col] = stock[col];
            char* out = values[first + col];
            if (col == 3) {
                if (o.gain == gain_unavailable) copy(out, header_capacity, "-");
                else signed_integer(out, o.gain);
            } else if (!known) {
                copy(out, header_capacity, "-");
            } else {
                const u32 frames = col == 0 ? begin : col == 1 ? end : end - begin;
                integer(out, frames * 100 / o.frames);
            }
        }
    }
    const char* const shown_titles[8] = {titles[0], titles[1], titles[2], titles[3],
                                         titles[4], titles[5], titles[6], titles[7]};
    page_header(state.header, input, frame, shown_titles, values, header_first);
}
}

void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    for (u32 i = 0; i < grain_shown; ++i) state.grains[i] = ShownGrain{0, 0, 0, 0};
    state.scan = 0;
    state.next = 0;
    state.random = 0x9e3779b9u;
    state.track = ~0u;
}

void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const SampleOverview& o = *opxy_sample_overview();
    header_row(state, input, frame, o);
    place_waveform(frame, o);
    const Region r = region(o);

    if (state.track != input.track) {
        for (u32 i = 0; i < grain_shown; ++i) state.grains[i].life = 0;
        state.scan = 0;
        state.next = 0;
        state.track = input.track;
    }
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    // A note starts its voice's scan from the position control, as start() does.
    if (events.hold_started) { state.scan = 0; state.next = 0; }
    const bool moving = !input.reduced_motion && r.length >= 256;
    const u32 ms = static_cast<u32>(clamp(input.elapsed, 0, 102)) * 1000 >> 10;
    if (!moving) {
        for (u32 i = 0; i < grain_shown; ++i) state.grains[i].life = 0;
    } else {
        for (u32 i = 0; i < grain_shown; ++i) {
            ShownGrain& g = state.grains[i];
            if (!g.life) continue;
            const u32 age = g.age + ms;
            if (age >= g.life) g.life = 0;
            else g.age = static_cast<opxy::u16>(age);
        }
        if (held) {
            const i32 scan = scan_hundredths(input.slots[4]);
            if (scan) {
                const u32 magnitude = static_cast<u32>(scan < 0 ? -scan : scan);
                const u32 delta = per_ms(strip_speed(r.rate, r.frames) / 100 * magnitude, ms) % r.length;
                state.scan = scan > 0 ? add_mod(state.scan % r.length, delta, r.length)
                                      : add_mod(state.scan % r.length, r.length - delta, r.length);
            }
            // New grains every size / density, while one of four slots is free.
            const u32 density = anchor(unsigned_q31(input.slots[2]), kDensityQ8, 5);
            u32 interval = (size_ms(input.slots[1]) * 256 + density - 1) / density;
            if (!interval) interval = 1;
            state.next = state.next > ms ? state.next - ms : 0;
            for (u32 n = 0; !state.next && n < grain_shown; ++n) {
                spawn(state, input, r);
                state.next = interval;
            }
            if (!state.next) state.next = interval;
        }
    }

    // Grains: their window's swell as size, white forwards, pale blue reversed.
    for (u32 i = 0; i < grain_shown; ++i) {
        const ShownGrain& g = state.grains[i];
        if (!g.life) { hide(frame, grain_first + i); continue; }
        const u32 travel = per_ms(static_cast<u32>(g.velocity < 0 ? -g.velocity : g.velocity), g.age);
        const u32 low = r.begin, high = r.begin + r.length;
        u32 x = g.velocity < 0 ? (g.x > low + travel ? g.x - travel : low) : g.x + travel;
        if (x > high) x = high;
        const i32 phase = static_cast<i32>(static_cast<u32>(g.age) * 1024 / g.life);
        const i32 window = phase * (1024 - phase) >> 8;   // 4 p (1 - p), Q10
        place_marker(frame, grain_first + i, x, unit / 2 + window / 2, g.velocity < 0 ? shade_reverse : shade_bright);
    }

    // Position (with the held note's scan), spray's reach and the outer markers.
    if (!r.length) {
        hide(frame, position_node);
        for (u32 i = 0; i < 2; ++i) { hide(frame, spray_first + i); hide(frame, trim_first + i); }
    } else {
        u32 relative = mul_shift(r.length, unsigned_q31(input.slots[0]), 31);
        if (relative >= r.length) relative = r.length - 1;
        if (held && moving) relative = add_mod(relative, state.scan % r.length, r.length);
        const u32 position = r.begin + relative;
        place_marker(frame, position_node, position, unit, shade_mid);
        u32 spray = strip_x(mul_shift(unsigned_q31(input.slots[3]), 2000, 31) * (r.rate / 10) / 100, r.frames);
        if (spray > r.length / 2) spray = r.length / 2;
        if (spray < 256) {
            hide(frame, spray_first); hide(frame, spray_first + 1);
        } else {
            place_marker(frame, spray_first, position > r.begin + spray ? position - spray : r.begin, unit, shade_mid);
            place_marker(frame, spray_first + 1, position + spray < r.begin + r.length ? position + spray : r.begin + r.length,
                         unit, shade_mid);
        }
        const u32 trim = input.shift ? shade_bright : shade_dim;
        place_marker(frame, trim_first, r.begin, unit, trim);
        place_marker(frame, trim_first + 1, r.begin + r.length, unit, trim);
    }
    text(frame, status_text, strip_left * unit, 208 * unit, unit,
         !o.assigned ? "NO SAMPLE LOADED" : !o.loaded ? "LOADING SAMPLE" : "");
}
}
