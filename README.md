# GlueForge

A professional, comprehensive **audio compressor / dynamics processor** plugin built with
**JUCE (C++)** — from transparent mix-bus glue to aggressive drum smashing, vocal leveling,
mastering control, parallel ("New York") compression, mid/side, multiband, and the flagship
**external-sidechain kick-ducking + tempo-synced EDM pump**.

> **Status:** Windows **VST3 + Standalone**, built and validated (pluginval strictness 10).
> AU / Logic / macOS code-signing are deferred to a later Mac bring-up session — the code is kept
> cross-platform (AU is declared in CMake), so that becomes a rebuild, not a rewrite.
> Design + roadmap: `docs/superpowers/specs/2026-06-18-glueforge-compressor-design.md`.

## Features

- **Core compressor** — threshold, ratio (1:1→limiting), variable soft **knee**, attack, release,
  **hold**, makeup; peak / RMS / blend **detector**; log-domain ballistics; sample-rate independent.
- **Parallel mix** (wet/dry), **auto-makeup**, **range** (max-GR cap), **output gain**,
  **variable stereo link** (linked ↔ dual-mono).
- **Sidechain** — external key input, detector **HP/LP filter**, **SC audition**.
- **EDM flagship** — **tempo-synced ducking** volume-shaper (host-locked, note divisions, depth +
  recovery curve) and **tempo-synced release**.
- **Character models** — **VCA** (clean), **FET** (aggressive), **Opto** (warm) — each a behaviour +
  **saturation** profile, with drive + sat-mix.
- **Metering & UI** — GR + I/O meters, live **transfer-curve** display, **GR-history** graph,
  factory **presets** by category, **A/B compare**, file-based user presets, resizable dark UI.
- **Lookahead** + **oversampling** (2×/4×/8×) with correct **latency reporting (PDC)**.
- **Mid/Side** and **3-band multiband** (Linkwitz-Riley, per-band trim/bypass/solo).

## Prerequisites (Windows)

- Visual Studio 2022 / v18 **Build Tools** with the **Desktop development with C++** workload (MSVC)
- **CMake** 3.22+ and **Ninja**
- Vendored deps (cloned into `libs/`, gitignored):
  - `git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git libs/JUCE`
  - `git clone --depth 1 --branch v3.5.2 https://github.com/catchorg/Catch2.git libs/Catch2`

## Build / Test / Validate / Package

```powershell
pwsh scripts/build.ps1            # Release: VST3 + Standalone + Tests (MSVC + Ninja, auto-detected)
ctest --test-dir build -C Release --output-on-failure   # DSP + state unit tests
pwsh scripts/validate.ps1         # pluginval, strictness level 10 (downloads pluginval on first run)
pwsh scripts/package.ps1          # dist/GlueForge-Windows-0.1.0.zip (+ installer if Inno Setup present)
```

Artifacts: `build/GlueForge_artefacts/Release/VST3/GlueForge.vst3` and `.../Standalone/GlueForge.exe`.

## Install for Ableton

- Run `pwsh scripts/deploy.ps1` (copies the VST3 to `C:\Program Files\Common Files\VST3`, UAC prompt), **or**
- point Ableton's **Preferences → Plug-Ins → VST3 custom folder** at `dist\VST3`, **or**
- run the installer from `scripts/package.ps1`.

Then **Rescan** in Ableton and drop **GlueForge** on a track. For the **external sidechain**, route a
source (e.g. the kick) to GlueForge's sidechain input and set **Trigger = External SC**.

## Architecture

Header-only, RT-safe DSP units in `Source/dsp/` — each independently unit-tested:

| Unit | Role |
|------|------|
| `GainComputer` | static soft-knee curve (level dB → GR dB) |
| `BallisticsSmoother` | attack/release/hold, log-domain |
| `LevelDetector` | peak / RMS / blend follower |
| `Compressor` | composes the above — **the reusable instance** (a multiband band is one) |
| `SidechainFilter` | detector HP/LP (bypassed at extremes) |
| `TempoDucker` / `TempoSync` | tempo-locked volume-shaper + note-division helpers |
| `Saturator` | per-model waveshaping (VCA/FET/Opto) |
| `Multiband` | 3-band LR4 split → 3 `Compressor` instances → flat recombine |

`Source/PluginProcessor.*` orchestrates the signal flow + APVTS state; `Source/PluginEditor.*` +
`Source/ui/` are the native UI. Tests in `tests/` (Catch2).

## macOS (deferred)

Build universal (Intel+ARM); enable AU; pass `auval`; load in Logic + Ableton/AU; ad-hoc sign +
document Gatekeeper, then Developer ID + notarization for distribution. The code is already
platform-clean, so this is a rebuild on a Mac.
