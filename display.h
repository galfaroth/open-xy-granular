#pragma once
#include <opxy/page_header.hpp>
#include <opxy/sample_overview.hpp>

namespace granular {
// Matches resources.display: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 21, 17, 6>;

// Nodes: the waveform's four strips (paths), the two-page header's eight bars,
// four grain markers, the position marker, the spray's two edges and the
// sample's start and end markers. Text slots 0..15 are the header's, 16 the
// status line.
const opxy::u32 wave_first = 0, wave_paths = 4, header_first = 4, grain_first = 12, grain_shown = 4,
                position_node = 16, spray_first = 17, trim_first = 19, status_text = 16;
// Palette entries authored in panel.svg.fragment.
const opxy::u32 shade_dim = 1, shade_mid = 2, shade_bright = 3, shade_reverse = 4, shade_wave = 5;

// A grain on the screen: simulated from the controls and time, not read from
// audio. life 0 marks a free slot; x and velocity are Q8 pixels on the strip.
struct ShownGrain {
    opxy::u32 x;
    opxy::i32 velocity;   // Q8 pixels per second, negative when reversed
    opxy::u16 age, life;  // milliseconds
};

struct DisplayState {
    opxy::draw::PageHeader header;   // 20 bytes
    opxy::draw::NoteTracker notes;   // 16 bytes
    ShownGrain grains[grain_shown];
    opxy::u32 scan;      // scan offset within the region while a note is held, Q8 pixels
    opxy::u32 next;      // milliseconds until the next grain starts
    opxy::u32 random;
    opxy::u32 track;
};

void display_reset(DisplayState&);
void display(DisplayState&, const opxy::draw::Input&, Frame&);
}
