# GlueForge — Engineering Log

A running record of notable work, decisions, and the reasoning behind them. **Newest entries at the
top.** Keep entries short and honest: what changed, *why*, and any gotcha worth remembering. This is
the "why" companion to git history (the "what") and `AGENTS.md` (the "how").

Branding: **Nightshift Audio** · codes `Nsau`/`Glf1`. Repo: https://github.com/3ggz/GlueForge

---

## 2026-06-19 — Hover help, help toggle, and an embedded PDF manual

**What:** Descriptive `setTooltip()` on every control (both tabs, top-bar buttons, meters, transfer
curve, GR history, shape editor — display components were made `juce::SettableTooltipClient`). A `?`
button toggles hover help via `HelpTooltipWindow` (a `TooltipWindow` subclass scoped to the editor
that gates `getTipFor` on a flag). A beautiful, detailed user manual authored in
`Resources/manual/GlueForge-Manual.html`, rendered to `Resources/GlueForgeManual.pdf`
(`scripts/build-manual.ps1`, headless Chrome/Edge), embedded via `juce_add_binary_data`, and opened by
a `Manual` button that extracts the PDF to temp and launches the system viewer.

**Decisions:**
- **Dark-theme PDF** to match the plugin. Required `print-color-adjust: exact`, header/footer
  suppression flags, and `@page { margin: 0 }` for full-bleed backgrounds — verified by rendering
  pages to PNG and reading them back.
- **Commit the PDF.** CI has no browser, so it embeds the committed file rather than rendering.
- `GlueForgeData` binary-data target linked into **both** plugin and test target (tests compile
  `PluginEditor.cpp`).
- `TooltipWindow` scoped to the editor (`this` as parent), not the global desktop — avoids the
  "more than one TooltipWindow" assertion with multiple instances open (per advisor).

**Gotcha:** Could not capture a *live tooltip* in an automated screenshot — JUCE only shows tips when
its process is the genuine foreground app, which synthetic input doesn't satisfy. Verified buttons +
build + pluginval; tooltips confirmed wired (standard JUCE) and show on real hover. Committed `329c76a`.

## 2026-06-19 — CI moved off Node 20

**What:** Pinned actions to Node 24 (`actions/checkout@v5`, `actions/upload-artifact@v7`,
`softprops/action-gh-release@v3`) and replaced `ilammy/msvc-dev-cmd` — which has **no Node 24 release
at any version** — with a node-free `vcvars64` step that dumps the MSVC environment into `$GITHUB_ENV`
(mirroring `build.ps1`). Verified green on master; the Node 20 deprecation annotations are gone.

**Why:** Silence the deprecation warnings and drop a stale third-party dependency. Checked each
action's `action.yml` `using:` field before pinning rather than trusting version numbers.

## 2026-06-19 — v0.2.0 release + clean-build version fix

**What:** Cut **v0.2.0** (curved editor + per-band multiband round). Bumped the version in the three
pinned spots, built, validated (pluginval reported `v0.2.0`), tagged, and CI published the installer.

**Gotcha (important):** The first local build was *incremental* and left the Windows file-properties
VERSIONINFO at `0.1.0` even though the plugin reported `0.2.0` (pluginval confirmed). A **clean build**
regenerates `GlueForge_resources.rc` and fixes the file-properties version; redeployed. CI is always
clean, so the released installer was correct regardless. Also re-learned: Ableton caches the plugin
DLL in memory — a full DAW restart is required after reinstalling, and the Nightshift rebrand changed
the VST3 class-id so pre-rebrand projects won't auto-reconnect.

## 2026-06-19 — Round 2 features: curves, per-band multiband, MB tab

**What:** Curved/tension segments in the pump-shape editor (`DuckShape` per-node `curve`, power-curve
warp, `p:v:c` serialization with `p:v` back-compat; drag-segment-to-bend). Fully **per-band
multiband** (independent threshold/ratio per band; per-band GR as `std::array<std::atomic<float>>`
after a data-race review fix). A dedicated **MULTIBAND** tab (`MultibandView`) with a log-frequency
EQ-style display, draggable crossovers, three color-coded band regions with live GR shelves, and
per-band control columns. Tabs implemented as lightweight visibility toggles.

**Gotcha:** Per-band GR was first a plain `float` array — a real audio/UI data race; switched to
atomics. Also caught an inverted GR-history fill (solid block at 0 dB) via screenshot review.

## 2026-06-18→19 — GitHub, CI, installer, rebrand, v0.1.0

**What:** Pushed to **github.com/3ggz/GlueForge**. Added `.github/workflows/build.yml` (build+test on
push; on a `v*` tag → choco Inno Setup → ISCC installer → GitHub Release) and the
`installer/GlueForge.iss` + packaging scripts. Added the LFOTool-style **pump-shape editor**
(`DuckShape` nodes → 256-pt LUT, stored on the APVTS state tree, SpinLock try-lock RT handoff).
Rebranded the developer to **Nightshift Audio** (manufacturer code `Nsau`, reused across plugins).
Shipped **v0.1.0**, verified end-to-end, and uninstalled the old local install.

**Decisions / gotchas:**
- CI runner (`windows-latest`) has the same "no VS2022 generator" situation as local → use Ninja.
- Tests registered as a single `add_test` entry; `catch_discover_tests` mis-parses names with
  `;[],()` into one bogus test and fails the Test step.
- Rebrand = new manufacturer code = **new VST3 unique ID** (noted for project compatibility).
- PowerShell em-dash broke `package.ps1` parsing → ASCII hyphens only.

## 2026-06-18 — Initial build: 9 phases, Windows VST3 + Standalone

**What:** Built the whole plugin autonomously through 9 testable phases: skeleton → core compressor →
parallel/range/link → sidechain + tempo-duck → character/saturation → metering + UI → lookahead +
oversampling (with PDC) → mid/side + multiband → validation + packaging. **44 Catch2 unit tests**,
**pluginval strictness 10 = SUCCESS**, first `dist` zip produced. Design spec:
`docs/superpowers/specs/2026-06-18-glueforge-compressor-design.md`.

**Decisions:**
- JUCE 8.0.4 + native UI; VCA/FET/Opto character models in v1 (Vari-Mu later).
- **Windows VST3 + Standalone only for now**; AU/Logic/macOS/signing deferred but kept
  cross-platform (AU declared in CMake) so the Mac track is a rebuild, not a rewrite.
- Toolchain installed via winget; VS came in as **major version 18 Build Tools** (not VS2022), so
  `build.ps1` is version-agnostic (vswhere + vcvars64 + Ninja).
- RT-safe, header-only DSP units in `Source/dsp/`, each independently unit-tested; `Compressor` is the
  reusable instance that a multiband band reuses.
