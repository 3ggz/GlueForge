# GlueForge

A professional cross-platform audio **compressor / dynamics** plugin built with **JUCE (C++)**.
Mix-bus glue, drum smashing, vocal leveling, parallel compression, mid/side, multiband, and the
flagship **external-sidechain / tempo-synced EDM pump** — with selectable VCA / FET / Opto / Vari-Mu
character models.

> **Current status:** Phase 1 (skeleton + toolchain). Builds **VST3 + Standalone on Windows**.
> AU / Logic / macOS signing are deferred to a later Mac bring-up session (the code stays
> cross-platform so that becomes a rebuild, not a rewrite). See
> `docs/superpowers/specs/2026-06-18-glueforge-compressor-design.md` for the full design + roadmap.

## Prerequisites (Windows)

- Visual Studio 2022 **Build Tools** with the **Desktop development with C++** workload (MSVC)
- **CMake** 3.22+ and **Ninja** (or just the VS generator, which is what the scripts use)
- The vendored dependencies are cloned into `libs/` (not committed):
  - `libs/JUCE`   — `git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git libs/JUCE`
  - `libs/Catch2` — `git clone --depth 1 --branch v3.5.2 https://github.com/catchorg/Catch2.git libs/Catch2`

## Build

```powershell
pwsh scripts/build.ps1            # Release (VST3 + Standalone + Tests)
```

Artifacts land under `build/GlueForge_artefacts/Release/`:
- `VST3/GlueForge.vst3`
- `Standalone/GlueForge.exe`

## Test (DSP correctness + state)

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Validate (host integration — the hard gate)

```powershell
pwsh scripts/validate.ps1         # pluginval, strictness level 10
```

## Install for Ableton

```powershell
pwsh scripts/deploy.ps1           # copies VST3 to C:\Program Files\Common Files\VST3 (UAC prompt)
```

Then in Ableton Live 12: **Preferences → Plug-Ins → Rescan**, and drop **GlueForge** on a track.

### Ableton smoke-test checklist (Phase 1 gate #4)
1. Plugin appears under VST3 and loads on an audio track.
2. The Gain knob changes level smoothly (no clicks/zippers).
3. Automate Gain — automation is recorded and plays back.
4. Toggle the host Bypass — click-free, audio passes through.
5. Save the set, reopen it — the Gain value is recalled.
