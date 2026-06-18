# GlueForge — Design Spec (v1)

**Date:** 2026-06-18
**Status:** Approved — autonomous build authorized through all phases.

## Mission

A professional, cross-platform audio **compressor / dynamics processor** plugin that loads on a
track or bus in Ableton Live (VST3) and Logic Pro (AU). Genuinely comprehensive: transparent
bus glue, drum smashing, vocal leveling, mastering control, parallel compression, mid/side,
multiband, and the flagship **external-sidechain kick-ducking / tempo-synced EDM pump**.
Multiple selectable **character models** (VCA / FET / Opto / Vari-Mu) span invisible control to
obvious vibe.

## Locked decisions (from §13 of the brief)

| # | Decision | Choice |
|---|----------|--------|
| 1 | Framework | **JUCE (C++)** with the **CMake** workflow |
| 2 | UI | **Native JUCE components** (WebView possible later) |
| 3 | Multiband / Oversampling | **Architecture stubbed now**; full impl in Phase 8. Core compressor is a reusable instance so a band == one instance. Latency/PDC plumbing wired from Phase 1. |
| 4 | Character models in v1 | **VCA + FET + Opto** (Vari-Mu / Tube added later) |
| 5 | Build targets *now* | **VST3 + Standalone, Windows only** |
| 6 | Mac / AU / Logic / signing | **Deferred** to a later "Mac bring-up" session. Planned Mac signing: ad-hoc/local now → Developer ID + notarization later. |

## Project-wide invariants (must not erode)

1. **Cross-platform-clean from line one.** We only *build* VST3+Standalone on Windows, but CMake
   keeps **AU declared** in the `FORMATS` list and all DSP/param/state code stays platform-agnostic
   (no Win-only assumptions, no `<windows.h>` in shared code). The deferred Mac track must be a
   *rebuild on a Mac*, not a rewrite.
2. **Real-time-safe audio thread, always.** No allocations, locks, file/IO, or logging in
   `processBlock`. Everything pre-allocated in `prepareToPlay`. `juce::ScopedNoDenormals` at the
   top of every `processBlock`. Parameter smoothing on every continuous parameter.
3. **APVTS for all parameters** → automation, host session recall, and presets. State round-trip
   (`get/setStateInformation`) is unit-verified.
4. **Latency reporting (PDC)** hook wired from Phase 1 (`setLatencySamples`), reporting 0 until
   lookahead/oversampling exist, updated whenever they change.
5. **pluginval (strictest level, 10) is a hard gate** at the end of every phase. DSP correctness is
   unit-tested: measured GR vs. the static curve, sample-rate independence, latency reporting,
   state recall.
6. **Click-free, latency-compensated bypass** and click-free parameter changes throughout.

## Architecture — module decomposition

Each unit has one purpose, a narrow interface, and is independently testable.

| Unit | Responsibility | Depends on |
|------|----------------|------------|
| `ParamIDs` | Central parameter ID strings + APVTS layout factory | — |
| `LevelDetector` | Peak / RMS / Peak↔RMS blend detection in the log (dB) domain | — |
| `GainComputer` | Static curve: threshold, ratio, knee → target gain reduction (dB) | — |
| `EnvelopeFollower` | Attack / release / hold ballistics; auto/program-dependent release; coefficients from ms × sample rate | — |
| `Compressor` (band) | Composes detector + gain computer + envelope + gain smoothing + makeup; processes one channel-set. **The reusable instance** (multiband band == one of these) | the four above |
| `Sidechain` | External key routing, SC filter (HP/LP/tilt), SC audition, tempo-synced ducking generator | `LevelDetector` |
| `CharacterModel` | Behavior + harmonic-saturation profiles (VCA/FET/Opto); drive + mix | — |
| `Metering` | Lock-free atomic snapshots (GR / input / output / RMS / LUFS) for the UI thread | — |
| `Oversampler` *(stub)* | Off/2×/4×/8× wrapper around the nonlinear stages; reports latency | Phase 7 |
| `MultibandSplitter` *(stub)* | Linkwitz–Riley crossovers → N `Compressor` instances | Phase 8 |
| `GlueForgeProcessor` | `juce::AudioProcessor`: host integration, APVTS, orchestration, latency, state | all DSP units |
| `GlueForgeEditor` | Native JUCE UI: knobs, transfer curve, GR/IO meters, presets, A/B | `GlueForgeProcessor`, `Metering` |

## Phase roadmap

Each phase **builds, passes pluginval, has unit tests where DSP is involved, and is committed**
before the next begins.

1. **Skeleton + toolchain** — VST3 + Standalone build on Windows; gain knob; full RT-safety
   scaffolding; APVTS state round-trip; latency hook; click-free bypass. Gate: builds, pluginval
   (strictest), standalone passes audio, loads + automates + recalls in Ableton Live 12.
2. **Core compressor DSP** — threshold / ratio / knee / attack / release / hold / makeup + GR
   meter. Test: measured GR matches curve for known inputs; no zipper; SR-independent.
3. **Parallel (wet/dry) + auto-makeup + range/max-GR + output gain + stereo link (variable).**
4. **Sidechain** — external key input + SC filter + audition; then tempo-synced ducking mode with
   editable pump shape and tempo-synced release. Verify key input shows in Ableton.
5. **Character models** — VCA / FET (+ slam) / Opto behavior + saturation + drive/mix.
6. **Metering & UI** — GR meter, I/O meters (peak+RMS, LUFS optional), transfer-curve display with
   live operating point, GR-over-time graph, presets w/ categories, A/B, level-matched bypass.
7. **Lookahead + oversampling** (off/2×/4×/8×) with correct latency reporting.
8. **Mid/Side + Multiband** (2–4 bands, Linkwitz–Riley; band == `Compressor` instance; solo/bypass).
9. **Validation + packaging** — pluginval gate, Windows installer; (Mac universal + sign/notarize +
   auval in the deferred Mac session).

## Phase 1 — concrete design

**Ruthlessly minimal: a Gain knob + all correctness scaffolding. No compressor DSP yet.**

- **Toolchain (collaborative first step):** winget installs VS 2022 **Build Tools** (VCTools
  workload), **CMake**, **Ninja**. "Done" only when a real `cmake --build` of the targets compiles.
- **Repo / build:** `git init`; `CMakeLists.txt` using `juce_add_plugin` with
  `FORMATS VST3 Standalone AU` (AU declared, not built on Windows); JUCE consumed from `libs/JUCE`
  (cloned, gitignored). Company/plugin codes, sidechain bus declared (`AudioChannelSet` aux input)
  so the key-input plumbing exists early.
- **Processor:** stereo in/out, SR & block-size agnostic, mono+stereo; `ScopedNoDenormals`;
  one `juce::SmoothedValue` **Gain** parameter via APVTS; latency-compensated **bypass**;
  `get/setStateInformation` via APVTS XML; `setLatencySamples(0)`.
- **Editor:** minimal native UI — one rotary gain slider + label (real UI is Phase 6).
- **Tests:** a `ctest`/Catch2 unit-test target — trivial assertions in Phase 1, real DSP tests
  from Phase 2. State round-trip test added here.

### Phase 1 done-gate
1. `cmake --build` produces VST3 + Standalone. *(verified here)*
2. **pluginval strictest level passes.** *(verified here — primary automatable gate)*
3. **Standalone runs and passes audio**, gain knob audibly works. *(verified here)*
4. **Loads in Ableton Live 12; gain automates; session recall works.** *(verified here — Ableton is
   installed on this machine)*

## Testing strategy

- **Unit (Catch2 + ctest):** DSP math — GR vs. expected curve at given level/threshold/ratio/knee;
  attack/release time constants vs. ms; SR-independence (same behavior at 44.1/48/96 kHz);
  envelope monotonicity; state round-trip; latency value.
- **Integration:** pluginval strictest at each phase; standalone smoke test; Ableton manual
  checklist (load, automate, bypass, recall, sidechain in Phase 4+).

## Deferred Mac track (later session)

Build universal (Intel+ARM); enable AU; pass `auval`; load in Logic + Ableton/AU; ad-hoc sign +
document Gatekeeper steps; Developer ID + notarization when distribution is needed. Code already
cross-platform per invariant #1, so this is a rebuild, not a rewrite.
