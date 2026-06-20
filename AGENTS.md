# AGENTS.md

Working guide for AI agents and contributors in this repo. Read this before building or editing.
It captures the non-obvious parts: toolchain quirks, real-time-safety rules, the parameter/state
system, how to verify, and the gotchas that have actually bitten us.

GlueForge is a JUCE/C++ audio compressor/dynamics plugin (**VST3 + Standalone**, Windows for now;
AU/macOS is declared but deferred). Design + roadmap live in
`docs/superpowers/specs/2026-06-18-glueforge-compressor-design.md`. A running history of decisions is
in `docs/engineering-log.md` — **append to it when you make a notable change.**

---

## Golden rules

1. **Real-time safety is non-negotiable.** The audio thread (`processBlock` and everything it calls)
   must not allocate, lock, log, or do file/GUI work. Use `juce::ScopedNoDenormals`, pre-allocate in
   `prepareToPlay`, smooth parameters, and pass cross-thread data via `std::atomic` or a `SpinLock`
   **try-lock** handoff (never a blocking lock).
2. **Verify empirically, every time.** A change isn't done until it **builds**, **unit tests pass**,
   and **pluginval strictness 10 = SUCCESS**. For UI changes, also capture a screenshot. Don't claim
   done from "it should work".
3. **Don't break the gate.** Keep the public parameter set, state format, and VST3 identity stable
   unless intentionally versioning — changing them silently breaks users' saved projects.

---

## Toolchain (Windows) — the important quirks

- Visual Studio is installed as **major version 18 Build Tools**
  (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`, MSVC 19.51) — **not VS2022**. The
  `"Visual Studio 17 2022"` CMake generator does **not** match. Always build through the scripts,
  which locate VS via `vswhere`, source `vcvars64`, and use **Ninja**.
- `cmake` is on PATH; **`ctest` is not** — call it as `& "C:\Program Files\CMake\bin\ctest.exe"`.
- Shell is **PowerShell**; a Bash (Git Bash) tool also exists. In PS scripts use **ASCII hyphens**
  (an em-dash breaks the parser) and put a here-string's closing `'@` at **column 0**.
- `pluginval` detaches from the console; `validate.ps1` runs it via `Start-Process -Wait -PassThru`
  to capture the real exit code (downloads pluginval on first run).
- JUCE 8.0.4 and Catch2 v3.5.2 are **vendored into `libs/` (gitignored)** — clone them if missing
  (commands in `README.md`).

## Build / test / validate / package / install

```powershell
pwsh scripts/build.ps1 Release        # configure + build VST3 + Standalone + Tests (Ninja)
& "C:\Program Files\CMake\bin\ctest.exe" --test-dir build --output-on-failure
pwsh scripts/validate.ps1             # pluginval, strictness 10
pwsh scripts/package.ps1              # Release build -> dist zip + installer (if Inno Setup present)
pwsh scripts/deploy.ps1              # copy built VST3 to system folder (UAC prompt)
pwsh scripts/update.ps1             # pull -> build -> deploy (one-shot local update)
pwsh scripts/build-manual.ps1     # re-render the user-manual PDF from HTML
```

Build output: `build/GlueForge_artefacts/Release/{VST3,Standalone}/...`.

---

## Code layout

| Path | What |
|------|------|
| `Source/dsp/*.h` | **Header-only, RT-safe DSP units**, each unit-tested. `Compressor` composes `GainComputer` + `BallisticsSmoother` + `LevelDetector`; it is the reusable instance (a multiband band is one). Also `SidechainFilter`, `TempoDucker`/`TempoSync`, `Saturator` (VCA/FET/Opto), `Multiband` (3× LR4), `DuckShape` (node→LUT pump shape), `DspUtils`. |
| `Source/ParamIDs.h` | **Single source of truth** for parameter IDs + the APVTS layout (ranges, defaults, host-readable formatters). Add/locate parameters here. |
| `Source/PluginProcessor.*` | Signal flow + APVTS state + metering snapshots + the pump-shape LUT handoff. |
| `Source/PluginEditor.*` | Native editor: top bar, light visibility-toggle tabs (COMPRESSOR/MULTIBAND), control rows, displays, help/manual buttons. |
| `Source/ui/*.h` | `LookAndFeel`, `Components` (meters/curve/history), `ShapeEditor` (pump curve), `MultibandView` (EQ tab), `Presets`, `HelpTooltip`. |
| `tests/` | Catch2 tests (`test_dsp.cpp`); compiles `PluginProcessor.cpp` + `PluginEditor.cpp`. |
| `Resources/` | `manual/GlueForge-Manual.html` (source) + `GlueForgeManual.pdf` (committed, embedded). |
| `scripts/` | build/validate/package/deploy/update/build-manual PowerShell scripts. |
| `.github/workflows/build.yml` | CI: build+test on push; on a `v*` tag → installer → GitHub Release. |

---

## Conventions

- **Parameters:** declare in `ParamIDs.h`; bind in the editor with APVTS attachments. Cross-thread
  meter values are `std::atomic<float>` written on the audio thread, read on the UI timer (30 Hz).
- **State:** the pump shape rides on `apvts.state` as the `"duckShape"` property so it travels with
  the session, presets, and A/B. **Call `proc.rebuildShapeLut()` at every state-restore site**
  (preset apply, A/B recall, file load, `setStateInformation`).
- **Tests:** registered as a **single** `add_test(NAME GlueForgeTests ...)` entry — do **not** switch
  to `catch_discover_tests` (it mis-parses test names containing `;`, `[`, `,`, `)` into one bogus
  entry and fails CI).
- **UI:** tabs are plain visibility toggles (not `TabbedComponent`). Every interactive control gets a
  descriptive `setTooltip(...)`; the display components are `juce::SettableTooltipClient`. Hover help
  is gated by `HelpTooltipWindow` (a `TooltipWindow` scoped to the editor — pass `this` as parent).
- **Embedded resources:** the manual PDF is linked via `juce_add_binary_data(GlueForgeData ...)`,
  which is linked into **both** the plugin **and** the test target (tests compile `PluginEditor.cpp`,
  so they need the `BinaryData::GlueForgeManual_pdf` symbol). Regenerate the PDF with
  `scripts/build-manual.ps1` and **commit it** — CI has no browser and embeds the committed file.

---

## Versioning & releases

Version is pinned in **three** places — bump all of them together:
`CMakeLists.txt` `project(GlueForge VERSION x.y.z)` (this drives the plugin/pluginval version),
`scripts/package.ps1` `$version`, and `installer/GlueForge.iss` `#define MyVersion`
(CI overrides the `.iss` default via `/DMyVersion` from the tag).

Release flow: bump → commit → `git tag vX.Y.Z && git push origin vX.Y.Z`. CI builds the installer and
publishes it to GitHub Releases. Branding: COMPANY_NAME `Nightshift Audio`, codes `Nsau`/`Glf1`,
PRODUCT_NAME `GlueForge`.

---

## Gotchas (learned the hard way — see the engineering log for context)

- **Stale file version after an incremental build.** An incremental rebuild can leave the Windows
  file-properties version (VERSIONINFO) at the old number even though the plugin reports the new one.
  A **clean build** (`rm -rf build`) regenerates `GlueForge_resources.rc` and fixes it; CI is always
  clean, so its installer is correct.
- **Ableton caches the plugin DLL in memory.** Replacing the `.vst3` + "Rescan" does **not** swap a
  loaded module. Fully **quit and relaunch** the DAW, then delete/re-add the instance.
- **The Nightshift rebrand changed the VST3 class-id.** Projects saved with a pre-rebrand GlueForge
  won't auto-reconnect — they're a different plugin to the host.
- **Automating a JUCE tooltip screenshot is unreliable.** JUCE only shows tips when its process is
  the genuine foreground app; synthetic `SetCursorPos`/activation doesn't satisfy that. Verify
  tooltips by real hover, not a scripted capture.
- **No `pdftoppm` on this box**, so the Read tool can't open PDFs. To inspect a rendered PDF, render
  pages to PNG with `pip install pypdfium2 Pillow` (no system deps).
- **Inno Setup `ISCC.exe`** may be per-user at `%LOCALAPPDATA%\Programs\Inno Setup 6\` —
  `package.ps1` already searches there.
