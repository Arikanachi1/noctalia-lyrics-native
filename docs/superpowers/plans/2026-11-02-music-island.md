# Music island implementation plan

**Goal:** replace the center clock with an animated lyrics/clock widget and expandable native spectrum player.
**Architecture:** pure Lua presentation state and animation; Qt MPRIS backend for art and track status; separate PipeWire companion controlled by expiring panel leases.
**Spec:** ../specs/2026-11-02-music-island-design.md
**Tech stack:** C++20, Qt6, optional libpipewire, Luau, CMake/CTest.

## Constraints

- Pause timeout 300000 ms; missing source immediately selects clock.
- No canvas, no capture while panel is hidden or playback paused.
- Preserve search, manual lyrics, offsets and lock player.
- Never equate sandbox-blocked socket tests with success.

## Tasks

1. Import runtime sources. Add Lua state tests before implementation:
   pause at 299999/300000 ms, source removal, resume, paused metadata,
   lyrics fallback, interrupted transition, position-only updates.
   Implement `runtime/lib/island.luau`, widget and native player panel.
2. Add frequency/silence/non-finite tests before FFT implementation.
   Implement `SpectrumAnalyzer::analyze(samples, rate, dt)` and separate
   PipeWire executable with bounded lease and capture lifecycle.
3. Add MPRIS art URL to backend JSON; retain it across transient metadata
   loss for the same media identity. Keep remote image loading asynchronous.
4. Replace mini-panel visibility service with track-only stream. Wire panel
   lease and spectrum frames, preserve full lyrics/search access.
5. Install runtime via CMake; add reversible installation/config utility.
   Validate Lua behavior, manifest, CMake build, staged install and CTest.

## Review focus

Rapid pause/resume must not restart a transition endlessly; changing tracks
must not keep old lyrics/art; callback after panel close must not render;
PipeWire outage must degrade to an empty visualizer, never microphone input;
installer must preserve unrelated bar config and existing lock/search entries.

## Execution record

- Current workspace is a managed source snapshot without usable Git metadata;
  work proceeds in place and builds are staged under /tmp.
- Baseline CTest from the preceding task: lyrics-core cannot create fixture
  sockets; mpris-integration cannot start D-Bus in this sandbox.
- Deployment outside workspace is not attempted through a sandbox bypass.
- Tasks 1–5 implemented. Imported runtime retains existing full-text, search
  and lock entries. Tested C++ FFT, local artwork, Lua island/player lifecycle,
  search/motion regression, installer config and a staged installation.
- Independent read-only review found one interrupted-transition opacity jump.
  Added a regression assertion, observed failure, and preserved current alpha
  and displayed content at reversal; regression now passes.
- FFT benchmark: 2000 frames / 0.146 s in this environment, approximately
  0.18% of one core at 25fps for FFT alone, excluding capture and UI.
- Live shell rendering, live audio capture and global installation remain
  unverified because the environment does not permit the necessary access.
