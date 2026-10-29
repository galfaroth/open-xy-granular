#include "granular.h"

namespace granular {
namespace {
struct Settings {
    u32 begin, length;        // trimmed region, frames
    u32 length_q12, limit_q12;  // region in Q20.12 and the last readable position
    u32 size;                 // grain length, output samples
    u32 window_step;
    u32 interval;             // output samples between grain starts
    u32 spray_q12;            // half the random position range
    u32 span_q12;             // frames a grain covers at the current step
    u32 step;                 // frames per output sample, Q12
    u32 gain_q15;             // overlap compensation times level, up to 2.0
    u32 spread_q15;
    bool reverse;
};

void silence(i32* output, u32 channels) {
    for (unsigned i = 0; i < kBlock * channels; ++i) output[i] = 0;
}

void spawn(Voice& voice, const Settings& s, const u32 position_q31) {
    Grain* grain = nullptr;
    for (unsigned i = 0; i < kGrains; ++i)
        if (!voice.grains[i].window_step) { grain = &voice.grains[i]; break; }
    if (!grain) return;
    // Control position, scan offset and spray, all modulo the region.
    u32 relative = mul_shift(s.length, position_q31, 31 - kFraction);
    relative = add_mod(relative, voice.scan_position, s.length_q12);
    if (s.spray_q12) {
        relative = add_mod(relative, mul_shift(random(voice), s.spray_q12 << 1, 32), s.length_q12);
        relative = add_mod(relative, s.length_q12 - s.spray_q12, s.length_q12);
    }
    // Keep the whole grain inside the readable region, so it ends on its window.
    if (relative > s.limit_q12) relative = s.limit_q12;
    if (!s.reverse) {
        if (s.span_q12 >= s.limit_q12) relative = 0;
        else if (relative > s.limit_q12 - s.span_q12) relative = s.limit_q12 - s.span_q12;
    } else if (relative < s.span_q12) {
        relative = s.span_q12 <= s.limit_q12 ? s.span_q12 : s.limit_q12;
    }
    grain->position = (s.begin << kFraction) + relative;
    grain->step = s.reverse ? -static_cast<i32>(s.step) : static_cast<i32>(s.step);
    grain->window_phase = 0;
    grain->window_step = s.window_step;
    i32 pan = 0;
    if (s.spread_q15) {
        const i32 centered = static_cast<i32>(random(voice) >> 17) - 16384;  // -16384..16383
        pan = (centered * static_cast<i32>(s.spread_q15)) >> 14;             // -32767..32767
    }
    grain->gain_left = static_cast<u16>(pan > 0 ? 32767 - pan : 32767);
    grain->gain_right = static_cast<u16>(pan < 0 ? 32767 + pan : 32767);
}

Settings settings(Voice& voice, const opxy::SampleBlock& block) {
    Settings s{};
    const opxy::SampleView& sample = voice.sample;
    u32 end = block.region.end < sample.frames ? block.region.end : sample.frames;
    s.begin = block.region.begin < end ? block.region.begin : end;
    s.length = end - s.begin;
    s.length_q12 = s.length << kFraction;
    s.limit_q12 = s.length >= 2 ? s.length_q12 - 2 * kUnit : 0;  // frame + 1 stays inside the region
    s.reverse = block.region.reverse;
    const u32 size_control = unsigned_q31(block.parameters[1]);
    const u32 density_control = unsigned_q31(block.parameters[2]);
    // Software division on Blackfin: cache coefficients until controls change.
    if (!voice.size || voice.size_control != size_control || voice.density_control != density_control) {
        voice.size_control = size_control;
        voice.density_control = density_control;
        voice.size = anchor(size_control, kSizeFrames, 7);
        const u32 density = anchor(density_control, kDensityQ8, 5);
        voice.window_step = 0xffffffffu / voice.size;
        voice.interval = ((voice.size + 1) * 256 + density - 1) / density;
        if (voice.interval < kMinInterval) voice.interval = kMinInterval;
        voice.compensation = density <= 256 ? 32767 : (32767u * 256) / density;
    }
    s.size = voice.size;
    s.window_step = voice.window_step;
    s.interval = voice.interval;
    u32 spray = mul_shift(unsigned_q31(block.parameters[3]), kMaxSprayFrames, 31);
    if (spray > s.length / 2) spray = s.length / 2;
    s.spray_q12 = spray << kFraction;
    s.spread_q15 = unsigned_q31(block.parameters[6]) >> 16;
    // Playback rate: the note's frequency over C4, the pad's tuning and the
    // tune control, then the source rate over the 44.1 kHz output rate.
    u32 rate_q16 = mul_shift(block.pitch[0], kInverseC4Q16, 32);
    const i32 tune_cents = static_cast<i32>(mul_shift(unsigned_q31(block.parameters[5]), 2400, 31)) - 1200;
    rate_q16 = mul_shift(rate_q16, ratio_q14((block.region.pitch_cents < -4800 ? -4800 : block.region.pitch_cents > 4800 ? 4800 : block.region.pitch_cents) + tune_cents), 14);
    u32 step = rate_q16 >> 4;
    if (sample.rate != 44100 && sample.rate >= 8000 && sample.rate <= 192000)
        step = mul_shift(rate_q16, voice.source_ratio, 16);
    s.step = step > kMaxStepQ12 ? kMaxStepQ12 : step;
    const u32 span_frames = mul_shift(s.size, s.step, kFraction);
    s.span_q12 = (span_frames < s.length ? span_frames : s.length) << kFraction;
    // Normalize by overlap, so coherent grains keep headroom too.
    u32 level_q15 = block.region.level >> 12;
    if (level_q15 > 65535) level_q15 = 65535;
    level_q15 = mul_shift(level_q15, unsigned_q31(block.parameters[7]), 31);
    s.gain_q15 = (voice.compensation * level_q15) >> 15;
    return s;
}

// Scan moves the position control through the region at up to twice real
// time in either direction; the middle tenth of the control freezes it.
void scan(Voice& voice, const Settings& s, i32 parameter) {
    if (voice.scan_position >= s.length_q12) voice.scan_position %= s.length_q12;
    const i32 centered = static_cast<i32>(unsigned_q31(parameter) >> 15) - 32768;  // -32768..32767
    if (centered > -3277 && centered < 3277) return;
    const u32 magnitude = static_cast<u32>(centered < 0 ? -centered : centered) - 3276;  // 1..29492
    // Frames per block, Q12: magnitude / 29492 * 2 * 64 frames, through the
    // Q16 constant 128 * 4096 * 65536 / 29492.
    const u32 delta = mul_shift(magnitude, 1165084u, 16) % s.length_q12;
    voice.scan_position = centered > 0 ? add_mod(voice.scan_position, delta, s.length_q12) :
                                         add_mod(voice.scan_position, s.length_q12 - delta, s.length_q12);
}
}

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice = Voice();
    voice.sample = sample;
    voice.source_ratio = sample.rate <= 192000 ? (sample.rate << kFraction) / 44100u : 0;
    voice.rng = 0x9e3779b9u ^ sample.frames ^ static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(sample.data));
}

void start(Voice& voice) {
    for (unsigned i = 0; i < kGrains; ++i) voice.grains[i].window_step = 0;
    voice.next = 0;
    voice.scan_position = 0;
    random(voice);
}

namespace {
// Channel selection and gain preparation happen once per grain span.
template <unsigned SourceChannels, unsigned OutputChannels>
void mix(Voice& voice, const Settings& s, i32* output, u32 count) {
    const u32 begin = s.begin << kFraction;
    for (unsigned g = 0; g < kGrains; ++g) {
        Grain& grain = voice.grains[g];
        if (!grain.window_step) continue;
        const i32 gain_left = static_cast<i32>((s.gain_q15 * grain.gain_left) >> 15);
        const i32 gain_right = static_cast<i32>((s.gain_q15 * grain.gain_right) >> 15);
        for (u32 i = 0; i < count; ++i) {
            // Validate before reading: trim may move while a grain is alive.
            if (grain.position - begin > s.limit_q12) { grain.window_step = 0; break; }
            const u32 frame = grain.position >> kFraction;
            const i32 fraction = static_cast<i32>(grain.position & (kUnit - 1));
            const i16* at = voice.sample.data + frame * SourceChannels;
            const i32 left = at[0] + (((static_cast<i32>(at[SourceChannels]) - at[0]) * fraction) >> kFraction);
            const i32 right = SourceChannels == 2 ?
                at[1] + (((static_cast<i32>(at[3]) - at[1]) * fraction) >> kFraction) : left;
            const i32 window = granular_window[grain.window_phase >> 22];
            if (OutputChannels == 2) {
                output[i * 2] += (left * ((window * gain_left) >> 15)) >> 15;
                output[i * 2 + 1] += (right * ((window * gain_right) >> 15)) >> 15;
            } else {
                const i32 weight = static_cast<i32>((window * s.gain_q15) >> 15);
                output[i] += (((left + right) / 2) * weight) >> 15;
            }
            grain.position += static_cast<u32>(grain.step);
            const u32 next = grain.window_phase + grain.window_step;
            if (next < grain.window_phase) { grain.window_step = 0; break; }
            grain.window_phase = next;
        }
    }
}
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    const u32 channels = block.output_channels == 2 ? 2 : 1;
    silence(block.output, channels);
    const opxy::SampleView& sample = voice.sample;
    // Q20.12 positions cover stock's 882,000-frame cap with room to spare.
    if (!sample.data || sample.frames < 2 || sample.frames >= (1u << 20) ||
        sample.channels < 1 || sample.channels > 2 || sample.rate < 8000 || sample.rate > 192000) return;
    const Settings s = settings(voice, block);
    if (s.length < 2 || !s.step || !s.gain_q15) {
        start(voice);
        return;
    }
    scan(voice, s, block.parameters[4]);
    u32 offset = 0;
    while (offset < kBlock) {
        if (!voice.next) {
            spawn(voice, s, unsigned_q31(block.parameters[0]));
            voice.next = s.interval;
        }
        const u32 count = voice.next < kBlock - offset ? voice.next : kBlock - offset;
        i32* output = block.output + offset * channels;
        if (sample.channels == 2) {
            if (channels == 2) mix<2, 2>(voice, s, output, count);
            else mix<2, 1>(voice, s, output, count);
        } else {
            if (channels == 2) mix<1, 2>(voice, s, output, count);
            else mix<1, 1>(voice, s, output, count);
        }
        offset += count;
        voice.next -= count;
    }
    for (u32 i = 0; i < kBlock * channels; ++i)
        block.output[i] = saturate16(block.output[i]) * 32768;
}
// Audio-side telemetry only: no drawing, allocation or mutation of voice state.
void sample_visual(const Voice& voice, opxy::SampleVisual& output) {
    output = opxy::SampleVisual{};
    if (!voice.sample.data || voice.sample.frames < 2 || voice.sample.frames >= (1u << 20) ||
        voice.sample.channels < 1 || voice.sample.channels > 2 ||
        voice.sample.rate < 8000 || voice.sample.rate > 192000) return;
    for (unsigned i = 0; i < kGrains; ++i) {
        const Grain& grain = voice.grains[i];
        const u32 frame = grain.position >> kFraction;
        if (!grain.window_step || frame >= voice.sample.frames) continue;
        opxy::SampleMarker& marker = output.markers[output.count++];
        marker.frame = frame;
        marker.phase_q16 = grain.window_phase >> 16;
        marker.flags = grain.step < 0 ? 1u : 0u; // Bit 0 means reverse playback.
    }
}
}
