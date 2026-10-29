#pragma once
#include <opxy/draw.hpp>
namespace granular {
struct DisplayState { opxy::u32 geometry; };
using Frame = opxy::draw::Frame<4, 64, 8, 12, 4>;
void display_reset(DisplayState&);
void display(DisplayState&, const opxy::draw::Input&, Frame&);
}
