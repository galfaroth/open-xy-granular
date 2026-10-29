# Granular for Open-XY

A stereo granular sample engine for OP-XY OS 1.2.1, maintained independently
of the SDK. Four bounded grain slots per voice, integer DSP and no allocation
or I/O in audio callbacks. Uses the public sample and display APIs; the sampler base remains stock page 2. Preserve engine 36 through the build layout.

This revision requires Open-XY SDK 1.8 with the sample-display extension. The
local integration SDK is `../open-xy/local/sample-display-sdk`; the former pinned
SDK cannot compile these display callbacks. Firmware and hardware validation are
tracked separately from host tests.

## Sound and controls

Grains use a sin-squared window and linear PCM interpolation. Pitch follows
the note relative to C4, the sample rate, saved sample tuning and Tune.
Trim and reverse apply to every read, including when trim moves during playback.
Size measures output time, independent of pitch. Source files can be mono or
stereo; stereo output preserves the source channels and applies grain pan.

| Parameter slot | Control | Range |
| --- | --- | --- |
| 0 | Position | Position in the trimmed region |
| 1 | Size | 10–500 ms |
| 2 | Density | 0.5–4 overlapping grains |
| 3 | Spray | Random displacement up to 2 seconds, capped at half the region |
| 4 | Scan | Backward/forward drift, up to twice output sample speed; central 10% freezes |
| 5 | Tune | −12 to +12 semitones |
| 6 | Spread | Random grain pan |
| 7 | Volume | Output level |

The custom main page has two sets of four controls. Press any encoder to switch:
page 1 is Position, Size, Density and Spray; page 2 is Scan, Tune, Spread and
Volume. The header always shows the currently edited page and real-unit values.
Use **Shift + Sample** to switch to the stock waveform/trim page; the custom main
page handles grain controls. Sample assignment and stock trim remain SDK-owned.

Cloud, Freeze, Scanner and Stutter are included. Dense clouds use inverse-overlap
normalization, including coherent grains, with final half-scale output limiting.
This changes dense-preset loudness compared with the earlier prototype.
Pitch and new-grain settings are sampled once per audio block; active grains
retain the pitch and pan with which they started.

## Sample display

The main page draws a bounded 128-column waveform snapshot of the assigned sample
and up to four markers from the actual active grain positions. No animation clock
or simulated grain cloud drives the markers. White markers play forward; pale
blue markers play in reverse. The snapshot is an approximate sampled waveform,
not a full peak scan, and the screen labels it accordingly.

Markers show **the latest started voice**, not all polyphonic voices. A marker's
telemetry also includes its real window phase. Audio collects only bounded values;
GUI drawing never reads PCM pointers or mutates voice state. Missing, empty and
pending data are explicit states, and unavailable snapshots hide old graphics.

## Develop and check

Keep this repository beside `../open-xy`. Run from here:

```sh
python3 ../open-xy/local/sample-display-sdk/sdk.py test --machine . --firmware
```

Tests cover sound, mono/stereo buffers, freeze/scan, source rates, reverse,
empty/short/long sources, changing trim, invalid inputs, parameter sweeps and
maximum overlap. `tests/performance.cpp` also runs a repeatable host benchmark.
To see its timing directly, compile outside the checkout:

```sh
c++ -std=c++11 -O2 -I ../open-xy/local/sample-display-sdk/sdk/include -I . tests/performance.cpp -o /tmp/granular-bench
/tmp/granular-bench
```

The optional argument is normalized Density, e.g. `0.5`. Results are host
nanoseconds per 64-frame stereo output block, median of five runs. They are
not Blackfin cycles. The SDK currently provides neither a sample-engine host
CPU estimate nor a sample browser audition in this revision.

CPU work is bounded by four grain slots. Rendering processes each active grain
in spans between scheduled starts, specializing channel counts outside the inner
loop. Pan/level factors are computed per span; size/density divisions are cached
until those controls change. Sample-rate division happens at initialization.
The render loop has no variable division, square root, float or 64-bit arithmetic.

The SDK's [CPU guidance](https://github.com/etcetc000/open-xy/blob/33b8652d12be95f7b693ea3b5e2ddb86ed2c4e82/docs/api.md#cpu)
aims at 15,000 estimated Blackfin instructions per synth voice block and warns
above 24,000; it is not a hard sample-engine cycle limit. Sample engines require
[hardware profiling](https://github.com/etcetc000/open-xy/blob/33b8652d12be95f7b693ea3b5e2ddb86ed2c4e82/docs/profiling.md):
three runs for the exact committed revision before catalog submission.

## Local firmware build

Keep stock inputs and every firmware output outside all checkouts. From the SDK:

```sh
python3 sdk.py build --machine ../open-xy-granular --profiling on --stock-os "$HOME/Library/Application Support/OPXY-SDK/private/stock-1.2.1/os.ldr" --out "$HOME/Library/Application Support/OPXY-SDK/private/builds/granular-001"
```

Choose a fresh output folder each time. Build commands do not install anything.
Use the previous package's `--layout` when preserving an existing firmware's
machine numbers. This machine declares the sampler base (page 2). Use the existing layout to
preserve engine 36; new collection layouts can allocate another engine number. The earlier prototype used page 84 and a
private SDK extension: its projects are not promised to migrate unchanged.
No catalog submission or device operations are included in this repository setup.

## Origin and licence

Based on Piotr Szwach's Granular prototype from
[`galfaroth/open-xy`, commit `9e9f87b`](https://github.com/galfaroth/open-xy/commit/9e9f87b811c869049a0732806521a50f48a41e34),
with its fixed-point helpers and tables retained and the renderer reworked for
the current sample API. MIT; see [LICENSE](LICENSE). No firmware or sample audio
is distributed here.
