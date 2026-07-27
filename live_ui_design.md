# RastaConverter Live UI — Design Document

Status: **partially implemented.** The three-column shell (§7.1a), the full
option form with its disclosure rules (§7.2, §7.3), the target preview and
palette-utilization readout (§7.6), and the run dashboard (§9) are built and
working. What is still outstanding, in the order it is worth doing:

| Section | Status |
|---|---|
| §4 build gating, §5 layout | done (`ENABLE_LIVE_UI` defaults **ON**, not OFF as written below) |
| §6 `/saturation`, `/vibrance` | done |
| §7.1 shell and breakpoints | done, **without the navigator rail** - see note below |
| §7.2 option coverage | done — all 49 GUI-relevant options have a control |
| §7.3 disclosure/enablement rules | done |
| §7.4 wireframe | done, minus the preset row |
| §7.5 validation, `/onoff` grid editor | **outstanding** — `/onoff` is a file picker, not an editor; Setup does not yet re-run `ProcessCmdLine` for inline errors |
| §7.6 three-way preview, cost tiers, palette grid | done — Knoll and the slow distance metrics show the documented approximation with an "Update for exact" action |
| §8 presets | **outstanding** — "Copy command line" is built; save/load/browse are not |
| §9 dashboard | done, including the §10 paint/edit controls in the shared viewer |
| §9.2 error heatmap | **outstanding**; the details-mask overlay from the same section is built |
| §9.4 live tuning tiers | **outstanding** — `LiveTunables` exists but nothing writes or reads it yet |
| §10 paint mode | **done, as one paused editor** — a single "Pause & Edit" button opens a full-window editor with a segmented target selector, eight drawn-icon tools, scrolling/zooming canvas, per-target picker (0-255 value ramp for the mask, 16x8 hardware palette for the destination), live mask parameters, stroke and parameter history with undo/redo, and an apply bar that states the cost and offers this run or a new one. The manual *named* snapshot browser at the end of the section remains a separate enhancement. |
| §11 Done state | **outstanding** — the window closes when the run ends |
| §12 Build & Run on Atari | **outstanding** |
| §13 multi-instance safety | partial — output name is derived from the input and an overwrite warning is shown; the lock file and thread-budget registry are not built |
| §14 keyboard ownership | done |

**Deviation from §7.1's V6 choice: there is no navigator rail.** In practice the
collapsible sections with their state summaries already give the table of
contents the rail was meant to provide, and the rail cost width that the image
wants far more. The screen is two columns - the option form and a dominant
viewer - with the search box and the "Only changed" toggle moved to the top of
the form, and the input picker promoted to the window's top bar since choosing
an image is the first thing anyone does. §7.1b's Modified view survives as that
toggle.

Search filters the live form rather than listing matches, so a hit is the real
control, editable in place. That required moving §7.3's *hide* rules out of the
form and into the option table (`OptionDesc::available`), so search cannot offer
a control the form would not draw; an option that is gated explains why instead
of reporting no match.

The dashboard is fed by `RastaConverter::PublishLiveStats` through a new
`LiveStats` snapshot struct, so §9.8's claim that nothing in the optimizer needs
to change held up: no hot-path code was touched.

Revision 3. Revision 2 replaced the screen designs wholesale after the original
Setup form was found to cover only ~11 of the 52 command-line options and to
draw two controls ("Mutation rate", "Budget 45 min") corresponding to no field
in `Configuration` — §17 lists those corrections. Revision 3 adds the real-time
target preview (§7.6), `/saturation` and `/vibrance` (§6), the layout-variant
analysis and the single three-column shell (§7.1), and multi-instance handling
(§13). It also removes three mechanisms that did not earn their complexity —
focus mode, per-group presets, and tier glyphs on the Setup screen — and
replaces the asserted option count with a reproducible check (§7.2).

Companion visual mockups: **`rasta-live-ui-mockups.html`**. ⚠ That file renders
the *revision 1* layouts and is now out of date with §7–§11 below. It needs
regenerating before it is used as an implementation reference; the ASCII
wireframes in this document are normative until then.

## 1. Goal

Today's GUI (`RastaSDL`, `src/frontend/gui/RastaSDL.cpp`) is a fixed display:
three static image blits and a text-stats block, driven by `rasta.cpp` through
a 4-primitive contract (`gui.h`: `DisplayBitmap`, `DisplayBitmapLine`,
`DisplayText`, `NextFrame()` returning a 7-value `GUI_command` enum). There is
no interactivity beyond a handful of hardcoded keystrokes.

This document proposes an **optional, additive** layer — gated by a CMake
option and a runtime flag — that adds:

1. A pre-run **Setup screen** exposing *every* conversion option, replacing
   hand-edited `config.env`/CLI flags for interactive use.
2. A **real-time preview of the target pipeline** — colour correction, palette
   quantization, and dithering, each inspectable and comparable — plus two new
   colour options, `/saturation` and `/vibrance`.
3. A **preset system** for saving, loading and sharing option sets.
4. A during-run **dashboard** built around convergence feedback and real-time
   tunable parameters.
5. A **paint/edit mode** for modifying the destination image and the details
   mask mid-run, with undo and named snapshots.
6. A defined **completion state**, and a **Build & Run on Atari** action that
   invokes the bundled MADS assembler and opens the resulting `.xex`.

Nothing about `gui.h`, the console/`NO_GUI` build, or existing keyboard
shortcuts changes. Everything here sits beside the existing contract, not
inside it.

## 2. Design principles

These six rules resolve most layout arguments before they start. Every later
section is an application of them.

### P1 — Group by pipeline stage, and give each stage its preview

The options are not a flat bag; they belong to three sequential stages, and
each stage already has a visible product in today's three-image display:

| Stage | Product (today's display) | Options it owns |
|---|---|---|
| **① Source** | left image — source resized to Atari width | `/i`, `/h`, `/filter`, `/pal` |
| **② Target** | right image — quantized + dithered goal | `/predistance`, `/dither`, `/dither_val`, `/dither_rand`, `/brightness`, `/contrast`, `/gamma` |
| **③ Search** | centre image — the raster program's output | everything else |

Grouping this way answers, structurally, the single most confusing thing about
the CLI: **there are two colour-distance options**. `/predistance` builds the
target; `/distance` scores candidates against it. Placed in different stages,
beside different previews, the distinction is self-evident. Placed in one
"settings" list — as in every mockup so far — it is a documentation problem
forever.

A control belongs to the earliest stage whose preview it changes.

### P2 — Hide dependent *detail*; disable, with a reason, whole *capabilities*

Revision 1 said "visible but disabled" for everything. That is wrong at this
option count: it produces a form where half the controls are dead. The correct
rule distinguishes two different situations.

- **A parameter that only exists for one value of a selector** is *not shown*
  when another value is selected. `/spatial_weight`, `/edge_weight` and
  `/region_weight` are three names for one concept — "the weight of the extra
  term this objective adds" — and exactly one is ever live. Showing three
  fields with two greyed out is clutter that teaches nothing the objective
  dropdown did not already say. Show **one contextual weight field** that
  rebinds and relabels with the dropdown. Same for the details-mask mode
  parameters (§7.3).
- **A capability that exists but is unavailable in the current configuration**
  *stays visible and disabled*, with the reason inline. When dual mode is on,
  the whole Objective group must remain on screen, greyed, reading "Dual mode
  uses its own joint objective." Hiding it produces "where did that option
  go?"; disabling it teaches the constraint.

Heuristic: hide a *knob*, disable a *feature*.

### P3 — Collapsed never means hidden state

Progressive disclosure at this scale requires collapsible sections, and
collapsible sections have one classic failure: a setting the user cannot see
silently changes their result. Every collapsed section header therefore carries
a one-line summary of its own current state:

```
▸ Dithering — floyd, strength 1.0, randomness 0.0
▸ Details mask — mask.png, normalized, strength 0.50        [3 non-default]
▸ Dual frame — off
```

The user can audit the entire configuration by reading headers, and expands only
what they intend to change. This is also why the design prefers one continuous
scrolling column over a tab strip: a tab hides state behind a click *and* behind
a decision about which tab to look in.

Header summaries work only for sections currently on screen. The **Modified (n)**
view (§7.1b) is the same guarantee for a form too tall to see at once — and,
not coincidentally, is exactly the delta set a preset stores (§8).

### P4 — Mark every control with when it takes effect

Three lifetimes exist (§6) and the UI must never leave which one applies to a
guess. One consistent visual language — **used during a run only**. In Setup no
run exists, so every option is simply "applies on Convert"; the glyphs would be
noise on the first screen a user meets (§7.1b). There they appear as tooltip
text instead.

| Badge | Meaning | Behaviour |
|---|---|---|
| ● green "live" | applies immediately | slider drag updates the run |
| ◐ amber "staged" | needs **Apply & Retarget** | edits accumulate; pending count shown |
| ○ grey "restart" | cannot change mid-run | shown read-only during a run, with **Restart with changes** |

The grey tier is not a dead end. "Restart with changes" carries the current
settings into a new run with the edit applied — which is what a user reaching
for a restart-only control actually wants, and is far better than discovering
the control is unavailable and having to rebuild the configuration by hand.

### P5 — Every control maps to a real field

No control ships without a `Configuration` field or a documented new option
behind it. Revision 1 violated this twice; §17 records the corrections. When a
genuinely new option is wanted (a wall-clock budget, say), it is specified as a
new option with parser support and a help entry, not drawn into a mockup.

### P6 — Disabled states explain themselves

Every disabled control and every disabled button has a tooltip or inline note
naming the condition that would enable it. This includes the primary action:
`Convert!` disabled must read "Choose an input file", never just grey out.

## 3. Why Dear ImGui, and why build the paint tool ourselves

`src/frontend/gui/RastaSDL.cpp` already owns an `SDL_Renderer`. The sibling
project `AltirraSDL` fetches Dear ImGui (docking branch) via CMake
`FetchContent`, builds it as a static lib linked against `SDL3::SDL3`, and uses
the stock `imgui_impl_sdl3.cpp` + `imgui_impl_sdlrenderer3.cpp` backends —
ImGui draws through the *same* `SDL_Renderer` RastaSDL already has, no second
rendering pipeline. Its `AltirraSDL/CMakeLists.txt:451-498` fetch block is the
template to copy (pinned tag, minimal build options, verify the resulting
`ImGui::*` target exists post-fetch) — the same pattern already used in this
repo's `CMakeLists.txt` for the SDL3_ttf fallback.

For the paint tool specifically: no mature ImGui+SDL3 paint widget exists to
adopt. `imgui-paint` (github.com/OpenGL-Graphics/imgui-paint) renders through
NanoVG — a second rendering backend with no upside here. `imgui_canvas`
(github.com/kuravih/imgui_canvas) is a bare shape/masking primitive, not a tool
palette. RastaConverter's canvases top out around 320×240, so a small custom
layer is simpler than adapting either: `ImDrawList` for toolbar/cursor chrome,
a plain CPU pixel buffer for the canvas, textbook Bresenham line/midpoint
circle/flood fill, uploaded to the existing `SDL_Texture` only on stroke-end
(not per mouse-move).

For the convergence chart (§9.3), stock `ImGui::PlotLines` is sufficient and
adds no dependency. ImPlot would give log-scale axes and markers for free but
is a second `FetchContent` — treat it as optional, decided at implementation
time, not a prerequisite.

## 4. Build system

- New CMake option `ENABLE_LIVE_UI` (default `OFF`). When `ON` and building the
  GUI target: fetch Dear ImGui the same way SDL3_ttf is fetched today — pinned
  tag, `FetchContent_Declare` + `FetchContent_MakeAvailable`, static lib linked
  to `SDL3::SDL3`, sources = `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
  `imgui_widgets.cpp`, `backends/imgui_impl_sdl3.cpp`,
  `backends/imgui_impl_sdlrenderer3.cpp`. Verify `TARGET ImGui::ImGui` (or
  equivalent) exists post-fetch; `FATAL_ERROR` if not, matching the SDL3_ttf
  fallback's own verification step.
- Nogui and default (`ENABLE_LIVE_UI=OFF`) GUI builds are unaffected — no new
  dependency fetch, no size/compile-time cost, unless explicitly requested.
- New runtime flag `/livegui` gates the feature *again* at runtime even in a
  build that has ImGui compiled in, so a user who built with
  `ENABLE_LIVE_UI=ON` still gets today's exact behavior by default.

## 5. Source layout (proposed)

```
src/frontend/gui/
  RastaSDL.cpp / .h            (existing — unchanged contract)
  live_ui/                      (new, compiled only under ENABLE_LIVE_UI)
    LiveUIOverlay.h/.cpp         top-level ImGui frame driver, owns all panels
    SetupScreen.h/.cpp           pre-run config form (§7)
    OptionBinding.h/.cpp         Configuration <-> widget binding + enablement rules (§7.3)
    PresetStore.h/.cpp           preset load/save/diff, CLI-token serialization (§8)
    Dashboard.h/.cpp             during-run panels (§9)
    ImageViewer.h/.cpp           zoom/pan/compare/overlay viewer, shared by run + paint
    ErrorHeatmap.h/.cpp          per-pixel destination-vs-output error map (§9.2)
    ProgressHistory.h/.cpp       ring buffer of (evals, norm-dist) samples (§9.3)
    PaintCanvas.h/.cpp           CPU pixel buffer + tool implementations
    PaintTools.h/.cpp            Bresenham line, flood fill, midpoint circle, brush stamp
    LiveTunables.h                atomic struct shared with optimizer threads
    XexBuilder.h/.cpp            Generator/GeneratorDual invocation + platform-open
```

`RastaSDL` gets an optional `LiveUIOverlay*` member (null unless
`ENABLE_LIVE_UI` and `/livegui`). In `RastaSDL::NextFrame()`, SDL events are fed
to `ImGui_ImplSDL3_ProcessEvent` first; ImGui consumes what it wants (slider
drags, canvas clicks), everything else falls through to the existing `switch`
unchanged. After the existing texture blits, `LiveUIOverlay::Render()` runs
before `SDL_RenderPresent`.

`ImageViewer` is deliberately shared between the dashboard and paint mode —
see §10 for why paint is a *mode of the viewer* rather than a separate screen.

## 6. Three live-tunable tiers

Verified against `Evaluator.cpp`: `LineCache`/`InsnSequenceCache` cache the
*rendered color* per pixel — a deterministic function of the raster program +
registers, independent of the target image. Objective weights
(`m_spatial_objective_weight`, `m_edge_objective_weight`,
`m_region_objective_weight`, luma/chroma, `Evaluator.cpp:1298-1310`) are
combined **after** that cache lookup, over already-rendered rows.
Brightness/contrast/gamma (`rasta.cpp:449-451`) instead sit **upstream** of
everything: they change `input_bitmap` → `m_picture` → `m_picture_all_errors`
(the palette-distance lookup table every candidate is scored against) — the
scoring ground truth itself.

| Tier | Controls | Mechanism | Live? |
|---|---|---|---|
| **1 — pure search policy** | time/eval budget, save cadence, dual A/B/Mix view, pause/checkpoint | No rescoring at all | Yes, trivially |
| **2 — free reweighting** | objective term weight, dual luma/chroma penalty | Applied post-cache over rendered rows; no cache invalidation | Yes — true live sliders |
| **3 — retarget** | brightness, contrast, gamma, saturation + vibrance (new), destination-image paint edits, **the whole details-mask family** (`/details_val`, `/details_floor`, `/details_feather`, and mask paint strokes) | Rebuilds `m_picture_all_errors` — wholly for a colour change, only over the stroke's dirty rectangle for a mask edit — then flushes the line caches and resets the acceptance history | No — but a mask stroke is cheap enough to auto-apply on stroke end (§10); colour changes stay staged behind **Apply & Retarget**. *Preview is still real-time* (§7.6) |
| **restart-only** | palette, `/distance`, `/predistance`, dithering, `/opt`, `/s`, `/init`, `/h`, `/filter`, thread count, dual-mode toggle and schedule | Affects preprocessing-time quantization decisions or the search's structure | No — **Restart with changes** (P4) |

Tier 3 is cheaper than a full restart (rescoring reuses cached rendered rows,
no re-simulation) but still requires a **soft reset of the LAHC/DLAS
acceptance-history array** to the freshly rescored current value — old history
entries are on a different scale after the target changes and must not be
compared against post-retarget scores. This mirrors the reset already performed
at run start. The UI must say so on the Apply button's confirmation, because it
is a real, visible discontinuity in the score.

### Two new colour-correction options: `/saturation` and `/vibrance`

Neither is currently exposed — FreeImage offers only
`FreeImage_AdjustBrightness/Contrast/Gamma` (`rasta.cpp:449-451`). Both need a
small RGB→HSL→RGB routine placed next to those three calls. Per P5, each is
specified as a real option with parser support and a help entry, not merely
drawn as a slider:

- **`/saturation=[-100,100]`**, default `0` — uniform chroma scale, matching
  the `[-100,100]` convention of `/brightness` and `/contrast`.
- **`/vibrance=[-100,100]`**, default `0` — chroma scale weighted *inversely*
  to each pixel's existing saturation, so dull regions gain most and already
  saturated regions are protected.

Vibrance is arguably the more valuable of the two here, for a palette-specific
reason. help.txt:116 documents the recurring complaint that output is "too
gray" and suggests `/dither_val=2` as a workaround. The real cause is that the
Atari palette's chroma range is narrow at the luma extremes: a flat saturation
boost pushes already-saturated pixels past what the palette can represent, and
they all quantize into the *same* few entries — raising saturation while
*lowering* the distinct-colour count. Vibrance lifts the dull midrange where
the palette actually has headroom. §7.6's palette-utilization readout makes
that difference directly visible instead of a matter of opinion.

Order of operations: brightness → contrast → gamma (the existing order, kept
byte-for-byte) → saturation → vibrance. Appending the two new stages means a
run with both at their defaults produces bit-identical output to today, which
is the compatibility guarantee the benchmark suite needs.

### Three constraints that earlier revisions got wrong

- **The details-mask family is single-frame only.** `details_*` is referenced
  only in `src/core/rasta.cpp`; nothing under `src/core/dual/` reads it.
- **The details mask is not free reweighting.** Revision 1 filed the whole
  `details_*` family under tier 2, reasoning by analogy with the objective
  weights. The analogy fails: objective weights are combined *after* the cache
  lookup, but the mask is folded into `m_picture_all_errors` while that table is
  built (`rasta.cpp:588-616`) — the very table the tier-3 row is about, and the
  one `line_cache_result::line_error` is accumulated from. `Evaluator.{h,cpp}`
  contains no reference to `details` at all. So every mask change — a strength
  slider as much as a brush stroke — invalidates cached line errors and makes
  pre-change scores incomparable. It is structurally tier 3.
- **The objective family is single-frame only.** `visual_objective` and the
  three weights are referenced only in `Evaluator.{h,cpp}` and `rasta.cpp`;
  nothing under `src/core/dual/` reads them, and help.txt:260 states dual
  retains its own joint objective.

Both groups are therefore disabled-with-reason (P2) whenever dual mode is on,
in Setup and in the dashboard alike.

Additionally: `details_floor` is a **normalized-mode-only** parameter
(help.txt:154-156) and `details_mode` defaults to `legacy` (`config.h:89`). A
details-floor slider offered without the mode selector — as revision 1's Setup
screen did — does nothing at all by default. §7.3 fixes this.

### `LiveTunables` — the thread-safety mechanism

```cpp
struct LiveTunables {
    std::atomic<double> objective_term_weight;   // spatial|edge|region, per active objective
    std::atomic<double> dual_luma;
    std::atomic<double> dual_chroma;
    std::atomic<uint64_t> retarget_generation{0}; // bumped on Apply
};
```

UI thread writes on slider change (tier 1/2 — every frame during drag is fine,
these are plain atomic stores). Worker threads read at the top of each
candidate-evaluation loop. This mirrors the existing acquire/release pattern
already used for `m_best_snapshot`/`PublishedBestSnapshot`
(`rasta.cpp:2208-2210`) — no new locks on the hot path.

Tier 3 (retarget) does **not** use a per-drag atomic store — no atomic can make
a changed `m_picture_all_errors` safe to read mid-evaluation, and the caches
have to be flushed anyway. Destination paint edits and the colour sliders
accumulate in a UI-owned staging buffer; only "Apply & Retarget" performs the
rebuild + rescoring + history reset + `retarget_generation` bump, under a brief
pause. Details-mask edits take the same path, differing only in that the rebuild
is confined to a dirty rectangle, which makes the pause short enough to run
automatically on stroke end (§10).

The pause itself needs no new machinery: every iteration of `Evaluator::Run`
already ends inside `m_gstate->m_mutex`, so a `pause_requested` flag plus a
condvar — the same shape as the existing `m_finished` — quiesces the workers at
a safe point.

## 7. Screen 1 — Setup

### 7.1 Layout — variants considered

Six layouts were weighed against 49 GUI-relevant options, a user base that
iterates (re-runs the same image with one setting changed) far more often than
it configures from scratch, and P1–P6.

| Variant | Shape | Verdict |
|---|---|---|
| **V1** pipeline spine + collapsible sections | stages ①②③ down the left with thumbnails, sections right | **Rejected.** Two competing organizing metaphors on one screen. Three stacked previews are each too small to judge quantization by. Stage ③ ends up a label pointing elsewhere — a panel with no controls of its own. |
| **V2** master/detail | category list left, one category's detail right | Good navigation, scales to any option count, but **shows one category at a time** — the P3 hidden-state problem returns at category granularity. |
| **V3** one long scrolling form | sticky headers, everything present | No hidden state, but 49 options is a punishing scroll with no way to jump. |
| **V4** wizard / stepper | Source → Target → Search → Run | Good for a first run, hostile to the actual dominant workflow of changing one thing and re-running. Modal. |
| **V5** top tab strip | tabs per group | Rejected in P3: a tab hides state behind a click *and* behind guessing which tab. |
| **V6** navigator rail + continuous detail + persistent viewer | rail scroll-anchors into one continuous scrollable form; large image always visible | **Chosen.** |

**V6 is V2's navigation with V3's honesty.** The critical decision is that the
left rail is a **navigator, not a filter**: clicking a category scrolls the
detail pane to that section rather than hiding the others, and the rail
highlight tracks scroll position. Nothing is ever unreachable by scrolling,
nothing is ever more than one click away, and the rail doubles as a table of
contents carrying per-category badges (non-default count, error).

No "focus mode" toggle. An earlier draft offered one to also satisfy V2
preferers; that is a symptom of not deciding. Commit to the navigator, and
revisit only if real use shows demand.

### 7.1c How big is it actually

The claim that one scrolling column is navigable needs a number. 49 options
across 8 sections is roughly 57 rows plus spacing — about **three screens** at
a typical detail-column height, fully expanded.

It is never fully expanded by default. Default state:

| Section | Default |
|---|---|
| ① Source | expanded |
| ② Algorithm | **collapsed** |
| ③ Colour | expanded |
| ④ Dithering | expanded |
| ⑤ Details mask | **collapsed** |
| Run & output (bottom bar) | always visible |

*(As built. The sections were originally numbered by pipeline stage, which put
three of them at "③" and read as a numbering bug rather than as a grouping.
They are now numbered in the order they appear, and Objective and Dual frame
have become the last and first groups of Algorithm - see §7.2.)*

So the screen a first-time user meets is: input file, height, filter, palette,
colour sliders, dither type/strength, output, threads, **Convert!** — about 14
controls, one screen, no scrolling. The other 35 options are four collapsed
headers away, each showing its state per P3. That default is the real answer to
"is this simple": the complexity is present and reachable, but not *charged* to
someone who does not want it.

### 7.1d Small windows

Three columns plus a usable image needs width. Two breakpoints:

- **< ~1100 px**: rail collapses to icons with tooltips; labels return on hover
  or on a pin toggle.
- **< ~850 px**: the viewer becomes a toggleable overlay over column B
  (`Tab` to flip) rather than a third column. Setup and Run stay usable on a
  laptop screen; only simultaneous form-and-image goes away.

Below that, the window is smaller than the existing GUI supports and no special
handling is proposed.

### 7.1a One shell for all three modes

The stronger consequence: **Setup, Run and Done are the same three-column
shell**, not three screens.

```
┌───────────┬──────────────────────────┬───────────────────────────┐
│  A: rail  │  B: detail               │  C: viewer                │
│  navigator│  form / dashboard        │  the image — always       │
└───────────┴──────────────────────────┴───────────────────────────┘
```

- **Column A** — categories in Setup, panels in Run. Always a navigator.
- **Column B** — the option form in Setup, the dashboard in Run, the artifact
  list in Done.
- **Column C** — the *same* `ImageViewer` widget in every mode, with the same
  zoom, pan, compare and overlay controls.

Reading order is left-to-right causal: **where → what → result**. The control
being dragged (B) is adjacent to the image it changes (C), which is the
adjacency that matters.

Because column C is literally the same widget across modes, pressing **Convert!**
does not reset the view: the zoom and pan you set up while judging the dither
preview carry straight into watching the optimizer work on that same region.
That continuity is impossible in a three-separate-screens design and is worth
more than it sounds — it is the difference between a tool and three tools.

Splitter positions are draggable and remembered **per mode**, defaulting to
detail-dominant in Setup and viewer-dominant in Run. `F11` collapses A and B
entirely for a full-window image.

### 7.1b Search and the Modified view

Two rail entries that are not categories:

- **Search box** over option name, CLI flag *and* help text. Typing `saturation`
  finds it without knowing it lives under Target; typing `/details_floor` jumps
  straight there, which is the migration path for every existing CLI user. This
  is what neutralizes the discoverability cost of any categorized layout.
- **Modified (n)** — a pseudo-category listing only options that differ from
  their defaults. This is the audit view that P3's collapsed-header summaries
  provide when sections are on screen but cannot provide once scrolled away.
  It is also, exactly, the content of a preset (§8): the same delta set,
  rendered two ways.

Categories in the rail follow P1's pipeline order so the structure still
teaches the pipeline, without paying V1's cost:

```
  🔍 Search…
  ★ Modified (3)
  ─────────────────
  ① Source
  ② Algorithm              ⚠ 1
  ③ Colour
  ④ Dithering
  ⑤ Details mask             2
  ─────────────────
  Run & output
```

Five categories. `/onoff` lives inside Algorithm rather than a "Constraints"
category of its own — a one-option category is a category too many.

Note what the rail does **not** carry: tier badges. In Setup nothing is live or
staged, because no run exists yet — every option simply applies when Convert is
pressed. Teaching the ●◐○ language on the first screen a user sees costs
attention and buys nothing there. The tier appears in Setup only as tooltip
text on the options it applies to ("can be tuned live during a run"), and the
glyphs themselves appear only in the run dashboard (§9.4), where they change
what a control does.

### 7.2 Complete option coverage

**Completeness is verifiable, not asserted.** The authoritative list is the
`addOption`/`addFlag` calls in `src/app/config.cpp`:

```sh
grep -oE '(addOption|addFlag)\("[a-z_0-9]+"' src/app/config.cpp \
  | sed -E 's/(addOption|addFlag)\("//; s/"//' | sort -u
```

That yields **53 registered names**; `unstuck_drift_norm` is a documented alias
of `unstuck_drift` (`config.cpp:300-305`), giving **52 distinct options**.
`/version`, `/quiet` and `/help` are console-only and have no GUI control by
design, leaving **49 that the Setup screen must expose**. Every one is in the
table below. This grep belongs in a CI check: a new option added to the parser
without a home in the UI should fail the build, because a GUI that silently
lags the CLI is how the current situation arose.

| Group | Options | Notes |
|---|---|---|
| ① Source | `/i` `/h` `/filter` `/pal` | `/h` behind an "Auto" checkbox |
| ② Algorithm — frames | `/dual` `/first_dual_steps` `/after_dual_steps` `/altering_dual_steps` `/dual_blending` `/dual_luma` `/dual_chroma` `/dual_dither` `/dual_dither_val` `/dual_dither_rand` | first, because it decides what the rest of the section means; sub-options gated on `/dual` |
| ② Algorithm — search | `/opt` `/s` `/init` `/unstuck_after` `/unstuck_drift` `/seed` `/onoff` | `/onoff` lives here rather than in a one-option "Constraints" category |
| ② Algorithm — objective | `/objective` `/distance` + one contextual weight from `/spatial_weight` `/edge_weight` `/region_weight` | last; disabled in dual, which scores jointly |
| ③ Colour | `/brightness` `/contrast` `/gamma` `/saturation` `/vibrance` | real-time preview (§7.6) |
| ④ Dithering | `/dither` `/dither_val` `/dither_rand` `/predistance` | val/rand gated on type; `/predistance` labelled "Target build distance" |
| ⑤ Details mask | `/details` `/details_mode` `/details_val` `/details_floor` `/details_feather` `/details_refine_mix` `/details_score` `/details_allocate` `/details_global_period` | whole group disabled in dual; params gated on mode |
| Run | `/o` `/threads` `/cache` `/max_evals` `/save` `/continue` `/preprocess` | bottom bar |
| Console-only | `/v` `/quiet` `/help` | intentionally absent |

### 7.3 Disclosure and enablement rules

Every rule below is a real dependency in the code or help text, not a design
preference. **Hide** = P2 knob, **Disable** = P2 feature.

| Control | Rule | Condition | Source |
|---|---|---|---|
| `dither_val`, `dither_rand` | Hide | `dither == none` | config.h:85-87 |
| `dither_val`, `dither_rand` | Disable + note | `dither == rfloyd` (documented inert) | help.txt:125-127 |
| contextual objective weight | Show exactly one, relabelled | `source-composite`→spatial, `source-edge`→edge, `source-region`→region; none for `legacy`/`source`/`source-spatial` | help.txt:263-276 |
| entire Objective group | Disable + "dual uses its own joint objective" | `dual` on | no refs in `src/core/dual/` |
| entire Details group | Disable + "single-frame only" | `dual` on | `details_*` only in rasta.cpp |
| all `details_*` | Disable | no mask file chosen | `/details` is the gate |
| `details_floor`, `details_feather` | Hide | `details_mode != normalized` | help.txt:154-158 |
| `details_refine_mix` | Hide | `details_mode != refined` | help.txt:164-166 |
| `details_allocate` | Disable | `details_mode == legacy` | help.txt:173-176 |
| `details_global_period` | Hide | `details_allocate` off | help.txt:178-180 |
| all `dual_*` sub-options | Hide | `dual` off | config.h:120-131 |
| `dual_dither_val`, `dual_dither_rand` | Hide | `dual_dither == none` | config.h:129-131 |
| `after_dual_steps` bootstrap note | Show | explains that `generate` costs a second `first_dual_steps` bootstrap | help.txt:24 |
| height spinner | Disable | "Auto" checked | — |
| `unstuck_drift` | Disable | `unstuck_after == 0` | help.txt:234-238 |

Two non-blocking warnings rather than enablement rules:

- `predistance == ciede` together with `dither == knoll` → inline "⚠ very slow
  combination" (help.txt:109, 283).
- `objective != legacy` together with `continue` on a run saved with a
  different objective → "changes invalidate resume comparability", driven by
  the existing `resume_*_changed` machinery (`config.cpp:143-150`) rather than
  a reimplemented comparison.

### 7.4 Wireframe

```
┌ test.jpg → test.png — RastaConverter — Setup ────────────────────────────────────────┐
│ Preset [ Local-quality preset * ▾ ] [Save as…] [Revert]     [Copy as command line]    │
├──────────────┬────────────────────────────────┬──────────────────────────────────────┤
│ 🔍 Search…   │ ▾ ① SOURCE                     │ View [Source|Corrected|Quantized|▸]  │
│ ★ Modified(3)│   Input   [test.jpg         …] │ Compare [—|▥ split|✳ blink] Zoom[Fit]│
│ ─────────────│   Height  [x] Auto  [240 ▲▼]   │ ┌──────────────────────────────────┐ │
│ ① Source     │   Filter  [ box            ▾]  │ │                                  │ │
│ ② Colour     │   Palette [ laoo           ▾]  │ │                                  │ │
│ ② Dithering  │                                │ │      live target preview         │ │
│ ③ Objective⚠1│ ▾ ② TARGET · COLOUR            │ │      (dithered), 200%            │ │
│ ③ Details  2 │   Brightness  [  0] ═══○═══    │ │                                  │ │
│ ③ Algorithm  │   Contrast    [  0] ═══○═══    │ │                                  │ │
│ ③ Dual frame │   Gamma       [1.0] ══○════    │ └──────────────────────────────────┘ │
│ ─────────────│   Saturation  [  0] ═══○═══    │ ┌ palette 16 hues × 8 lumas ──────┐ │
│ Run & output │   Vibrance    [+15] ════○══    │ │ ▪▪▫▫▪▪▫▫▪▪▫▫▪▪▫▫                │ │
│              │                                │ │ ▪▪▪▫▪▪▪▫▪▪▪▫▪▪▪▫                │ │
│              │ ▾ ② TARGET · DITHERING         │ └─────────────────────────────────┘ │
│              │   Build distance [ rasta    ▾] │  52 of 128 colours used  (was 47)   │
│              │   Dither         [ floyd    ▾] │                                      │
│              │     Strength   [1.0] ═══○═══   │  ⓘ approximate (fast distance)      │
│              │     Randomness [0.0] ○══════   │     [Update preview] for exact       │
│              │                                │                                      │
│              │ ▸ ③ Objective — dual mode  ⚠   │                                      │
│              │ ▸ ③ Details — mask.png, norm…2 │                                      │
│              │ ▸ ③ Algorithm — lahc, hist 1   │                                      │
│              │ ▸ ③ Dual frame — on, copy      │                                      │
├──────────────┴────────────────────────────────┴──────────────────────────────────────┤
│ Output [test.png  …] ⚠ in use by pid 4821   Threads [12/16] ⚠ 2 instances want 24    │
│ Max evals [ ∞ ]  Autosave [auto ▾]  [ ] Continue stopped run  [ ] Preprocess only     │
│                                                          [        ▶ Convert!       ]  │
└───────────────────────────────────────────────────────────────────────────────────────┘
  rail badges: ⚠ error · n = settings differing from default.  No tier glyphs in Setup.
```

This is the **default** state: stages ① and ② expanded, all of stage ③ collapsed
to state-summary headers. Fourteen controls, one screen, no scrolling — and the
other 35 options are one click away.

The rail is a navigator: clicking `③ Details` expands and scrolls to that
section; it does not hide Colour or Dithering. The middle column is one
continuous form; the viewer never moves.

### 7.5 Validation, and the two file-backed options

Setup does not reimplement validation. It calls the same
`Configuration::ProcessCmdLine` path, then renders `error_messages` inline on
the offending control and `warning_messages` in a dismissible strip
(`config.h:116-117`). Clamping — `details_floor` to [0.01,1], `details_feather`
to [0,8], `threads` to hardware concurrency, etc. (`config.cpp:544-561`) — is
therefore identical to CLI use by construction, and a clamped value visibly
snaps in its widget rather than silently differing from what was typed.

`/details` and `/onoff` are file paths, and both deserve more than a picker:

- **Details mask** — the picker is joined by "Edit…", entering paint mode (§10)
  on a mask that may not exist yet, so a mask can be authored without leaving
  the tool.
- **Register on/off** — the format is line-oriented (`REGISTER OFF|ON FROM TO`
  over COLOR0–3/COLBAK/COLPM0–3/HPOSP0–3, help.txt:291-309). A small grid
  editor — 13 register rows × scanline range — is far more usable than a text
  file, and can round-trip the file format exactly. This is the option most
  users never discover; a visual editor is the difference between "exists" and
  "used".

### 7.6 Real-time preview of the target pipeline

Colour-correction sliders without a live preview are unusable: nobody can
predict what `contrast +12` does *after* quantization to 128 Atari colours.
This section is what makes stage ② of P1 worth having.

#### The three-way target preview

The stage-② preview is not one image but three points in the pipeline, and the
interesting information is in the *differences* between them:

| Preview | Shows | What it answers |
|---|---|---|
| **Corrected source** | full-colour, after brightness/contrast/gamma/saturation/vibrance | "did I over-crush the shadows?" |
| **Quantized** | mapped to the palette via `/predistance`, **no dithering** | "what actually survives the palette?" |
| **Dithered** | the real destination, `/dither` applied | "what does dithering buy me here?" |

All three share the viewer's compare modes (§9.2) — side-by-side, split-wipe,
and blink. **Quantized ↔ Dithered blink** is the direct answer to "is this
dither type and strength helping?", a question the current tool gives no way to
ask short of a full run. The preview also honours the Atari pixel aspect
(`RescaleFIBitmapDoubleWidth`, `rasta.cpp:198`) so what is previewed is shaped
like what is produced.

#### Cost tiers — the preview degrades, it never blocks

Preview cost varies by three orders of magnitude across the option space, so a
single update strategy cannot work. Three tiers, chosen automatically:

| Tier | Work | Cost at 160×240 | Update policy |
|---|---|---|---|
| **A — correction** | per-pixel curve + HSL chroma scale | ~38 k px, sub-millisecond | **Every frame during the drag.** No debounce. |
| **B — quantize** | `FindAtariColorIndex` over 128 entries, fast distance (`rasta`/`yuv`/`euclid`/`oklab`) | ~5 M distance ops, a few ms | Debounced ~100 ms, on a worker thread |
| **C — slow paths** | `/predistance=ciede` and/or `/dither=knoll` — documented "VERY slow" (help.txt:109, 283) | seconds | Explicit **Update preview** button, or auto after a long idle; runs cancellable in the background with a progress bar |

During a tier-C drag the preview shows the tier-B approximation (same
correction, fast distance, no Knoll) with a visible **"approximate — press
Update for exact"** badge, and the exact version is computed on release. The UI
never freezes and never silently lies about which one is on screen. `Knoll`
already has a parallel implementation and a cancellation return path
(`KnollDitheringParallel`, `rasta.cpp:1292`; the `cancelled` early-out at
`rasta.cpp:801-810`), so the cancellable-background-job machinery is partly
built already.

#### Palette utilization — objective feedback for a subjective slider

`color_indexes_on_dst_picture` (`rasta.cpp:130`) is a `set<unsigned char>` of
the palette entries the destination actually uses. It is already populated by
all three quantization paths (`rasta.cpp:684, 821, 1320`) and already consumed
by `CreateLowColorRasterPicture` (`rasta.cpp:1662`) and a low-colour sanity
check (`rasta.cpp:1980`). It costs nothing to surface, and it is exactly the
measurement the colour sliders need.

Display it as the Atari palette's natural **16 hues × 8 lumas grid**, with used
entries lit and sized by pixel count, plus a headline **"N of 128 colours
used"**. Dragging saturation or vibrance then shows, live, whether chroma is
actually reaching the palette or collapsing back into the grey column — which
turns help.txt's "too gray" complaint from a matter of taste into something the
user can see and optimize against. Surface the existing `< 5` low-colour
condition (`rasta.cpp:1980`) as an inline warning here rather than leaving it as
a silent internal special case.

#### Two refactors this requires

Both are small, but the design does not work without them and they should be
scheduled explicitly:

1. **Colour correction must become non-destructive.**
   `FreeImage_AdjustBrightness/Contrast/Gamma` mutate `input_bitmap` *in place*
   (`rasta.cpp:449-451`), after rescale and 24-bit conversion, so today the
   uncorrected pixels are gone and a slider could only ever add to the previous
   correction. Retain the rescaled-but-uncorrected bitmap and make correction a
   pure function of it. The same function must run at run start, so preview and
   reality cannot drift apart.
2. **`PrepareDestinationPicture()` must be callable against a scratch buffer.**
   The preview has to call *that* function (`rasta.cpp:764`), not a
   reimplementation of it — otherwise the preview and the real target diverge
   the first time either is touched, which is the classic way a preview feature
   becomes a liability. It currently writes to the member
   `destination_bitmap` and calls `ShowDestinationLine`/`ShowDestinationBitmap`
   directly; parameterize the output buffer and the per-line callback. This is
   also what lets the preview run on a worker thread without touching GUI state.

Requirement (2) has a bonus: it makes `/preprocess` mode, the preview, and the
real run one code path with one set of results — a WYSIWYG guarantee by
construction rather than by discipline.

#### Where else the preview appears

The same machinery serves the tier-3 **staged retarget** during a run (§9.4).
The colour sliders remain fully live *as a preview* mid-run — the user sees the
new target immediately — while the optimizer keeps scoring against the old one
until **Apply & Retarget**. That split is the whole point of the tier system,
and it needs the badge language of P4 to be legible: the preview is live, the
scoring is staged.

## 8. Presets

**A preset is a command-line token string, not a serialized struct.**
`Configuration::ProcessCmdLine` already accepts `extraTokens` and
`Configuration` already carries `command_line` and `resume_override_tokens`
(`config.h:146-147, 174`) — the command line *is* this application's canonical
serialization. Reusing it buys, with no new parser to keep in sync:

- exact GUI ↔ CLI parity by construction
- **Copy as command line** — the single most useful button for a tool whose
  users script batch runs
- **Paste command line** — populates the entire form from a forum post
- readable, diffable, greppable preset files
- forward compatibility: tokens from a newer version round-trip untouched
  instead of being silently dropped

Presets store **deltas from defaults**, not full snapshots, so a preset stays
meaningful if a default changes and the preset browser can honestly say "this
preset changes 4 things" instead of listing 51.

Three scopes:

1. **Built-in, read-only.** Ship the ones the documentation already validates:
   *Default*; *Maximum quality* (direct source/Floyd, help.txt:260); *Local
   quality* (`/objective=source-region /region_weight=0.025 /dither=floyd
   /dither_val=1 /dither_rand=0`, help.txt:260); *Fast preview*; *Dual-frame
   starter*.
2. **User presets** — save / rename / duplicate / delete.
Rules:

- **Scope is chosen at save time, not by a second mechanism.** "Save as
  preset…" shows the Modified list with a checkbox per section, all ticked by
  default. Unticking everything but Details gives the "reuse just my
  details-mask settings" case. An earlier draft added per-group presets via a ▾
  on every section header; that is a whole second preset system to build,
  document and keep consistent, in exchange for what one checkbox column does
  better and more flexibly.
- Presets exclude `/i`, `/o`, `/details`, `/onoff` and `/seed` by default —
  paths and seed are per-job, not per-recipe. A checkbox opts into a full-job
  snapshot.
- A modified preset shows as `Local quality *` with a **Revert** action. Without
  this, users lose track of whether a preset is still in effect.
- Presets are the natural unit for the benchmark suite in `benchmark/` to
  reference, so a quality claim and a reproducible option set are the same
  artifact.

## 9. Screen 2 — During the run

### 9.1 What the user is actually asking

The dashboard should be organised around the five questions a user has while a
run is going, in frequency order:

1. *How does it look?* → the image, large, zoomable.
2. *Is it still improving — should I stop?* → convergence, not a stats table.
3. *Where is it still wrong?* → per-pixel error, spatially.
4. *Can I nudge it without starting over?* → tiered live tuning.
5. *Give me the file.* → save, checkpoint, build `.xex`.

Today's display answers only (1), partially. Question (2) is the one that
actually governs user behaviour — RastaConverter runs are open-ended, and
"when do I stop?" is the decision the tool currently gives no help with. It
gets the most prominent non-image real estate.

### 9.2 The viewer

One large viewer replaces three fixed blits, which is strictly more capable at
the same texture cost (`ImGui::Image()` wrapping the same `SDL_Texture`, no
extra copy):

- **View selector**: Source · Target · Output — plus A · B · Mix in dual mode,
  preserving the meaning of today's `A`/`B`/`M` keys.
- **Shared zoom/pan across all views**, so switching view keeps your place.
  This is what makes comparison possible at all; today's fixed thumbnails
  cannot be inspected at the pixel level.
- **Compare modes**: side-by-side, split-wipe, and **blink** (rapid alternation
  between Output and Target). Blink comparison is the classic technique for
  spotting pixel-level differences and is nearly free to implement.
- **Overlays**, independently toggleable: error heatmap, details mask, pixel
  grid, scanline ruler.

The **error heatmap** deserves its own note because it is the answer to
question (3) and it is cheap. The UI thread already has the rendered best
output (it draws it) and `m_picture` (the destination); the per-pixel distance
is the same lookup the evaluator uses, over 160×240 = 38,400 pixels, recomputed
a few times per second at most. No optimizer change, no hot-path cost. Read the
best program through the existing `std::atomic_load_explicit(&m_best_snapshot,
acquire)` path (`rasta.cpp:2208-2210`) — no new synchronization.

The heatmap also supplies the missing *reason* for paint mode to exist: "the
face is where the error is concentrated → paint mask priority exactly there →
watch the error there drop." That is a complete, motivated workflow, and it is
why the heatmap and the mask brush must live in the same viewer (§10).

### 9.3 Progress and convergence

A chart, not a table. Normalized distance against evaluations, with the plateau
state called out in words:

```
Norm. dist   0.021438  ▼ improving
  ▁▂▃▄▅▆▇█ curve, x = evaluations (log), markers at each improvement
Last improvement   12.4 M evals ago  ·  4 min 02 s ago
Rate  371 k eval/s   ·   412 M evaluations total
```

Everything here comes from state that already exists:
`m_eval_gstate.m_evaluations`, `m_last_best_evaluation`, `m_best_result` via
`NormalizeScore`, and `m_rate` — the same four values `ShowMutationStats()`
already prints (`rasta.cpp:2152-2174`). "Last improvement N evals ago" is
literally `m_evaluations - m_last_best_evaluation`, and it is the single most
decision-relevant number the program computes. `ProgressHistory` is a UI-side
ring buffer sampled each frame; nothing in the optimizer changes.

Two honesty requirements:

- **No fabricated ETA.** Revision 1 showed "ETA ~26m10s" for a search that is
  open-ended by design. An ETA is shown *only* when `/max_evals` is set, as a
  progress bar against that limit. Otherwise the field reads "runs until
  stopped" — which is the truth.
- If `/unstuck_after` is active and the plateau exceeds it, show the drift
  the same way the current display does — `Norm. Dist: x (+drift)`
  (`rasta.cpp:2166-2171`) — so the escalation is visible rather than mysterious.

### 9.4 Live tuning, tier-marked

The tuning panel is split by tier (P4), never mixed:

```
● LIVE                        applied immediately
   Region weight    [0.025] ═══○═════
   Dual luma / chroma                      (dual only)
◐ STAGED — 2 pending          preview live, scoring unchanged until Apply
   Brightness [ +4] ═══○═  Gamma [1.10] ══○══
   Saturation [  0] ═══○═  Vibrance [+15] ════○
   Details strength [0.50 ] ═══○═════      (disabled in dual)
   Details floor    [0.25 ] ══○══════      (normalized mode only)
   14 paint strokes · 52 of 128 colours used (was 47)
   [Discard]  [Apply & Retarget]
     ⚠ resets the acceptance history; the score will jump once
○ RESTART ONLY
   palette laoo · distance rasta · predistance rasta · dither floyd
   optimizer lahc · history 1 · init random · threads 12
   [Copy as command line]     [Restart with changes…]
```

Crucially, the staged colour sliders are **live as a preview and staged as
scoring** (§7.6). The Target view updates as you drag, so the adjustment can be
judged visually against the current output, while the optimizer keeps scoring
against the old target until Apply. The palette-utilization delta ("52, was
47") tells you whether the adjustment actually gained the palette anything
before you pay the acceptance-history reset.

The restart-only block doubles as the run's configuration recap — during a long
run it is genuinely useful to be able to see, and copy, exactly what produced
the result in front of you.

### 9.5 Dual mode

Dual mode has real internal state that today appears as three text lines. It
gets a panel, shown only when `cfg.dual_mode`:

- **Phase**, from `m_dual_phase`: Bootstrap A → Bootstrap B (copy | generate) →
  Alternating, with the currently optimized frame from `m_dual_stage_focus_B`
  and the copy/generate distinction from `m_dual_bootstrap_b_copied`
  (`rasta.cpp:2179-2191`).
- **Schedule progress**: a bar showing position within the current bootstrap
  (`first_dual_steps`) or alternation block (`altering_dual_steps`), which
  makes the otherwise invisible schedule legible.
- Frame view A/B/Mix, mirroring the existing keys.

### 9.6 Mutation statistics and diagnostics

`m_mutation_stats[E_MUTATION_MAX]` already exists and is already displayed in
single-frame mode only (`rasta.cpp:2142-2150`). Keep that behaviour and present
it as a sorted horizontal bar list — which mutation operators are firing is
informative at a glance and illegible as a column of numbers.

Behind a collapsed **Diagnostics** disclosure, the island-optimizer counters
that exist but are never surfaced: `m_single_accepted`,
`m_single_global_improvements`, `m_single_migrations`, and the lock/copy timing
counters (`Evaluator.h:160-173`). These are developer-facing; collapsed by
default is correct.

### 9.7 Wireframe

Same three-column shell as Setup (§7.1a) — only column B's contents change, and
the splitter default moves to favour the viewer. Column C is the identical
widget, holding the zoom and pan carried over from the Setup preview.

```
┌ test.jpg → test.png · 0.0214 · RastaConverter ──────────────────── ● running ─┐
├───────────┬────────────────────────┬───────────────────────────────────────────┤
│ ★ Progress│ PROGRESS               │ View [Source|Target|Output] [▥][✳] [200%] │
│ ● Tuning  │  Norm. dist 0.021438 ▼ │ Overlay [x]heatmap [ ]mask [ ]grid [✎Edit]│
│ ◐ Staged  │  ▁▂▃▄▅▆▇█ (log evals)  │ ┌───────────────────────────────────────┐ │
│ ○ Config  │  Last improvement      │ │                                       │ │
│ ─────────│    12.4M evals·4m02s   │ │                                       │ │
│ Mutations │  371k eval/s · 412M    │ │      output @200%, heatmap on         │ │
│ Dual frame│  runs until stopped    │ │      red = where error is worst       │ │
│ Diagnostic│ ─────────────────────  │ │                                       │ │
│ ─────────│ ● LIVE TUNING          │ │                                       │ │
│ Output    │  Region wt [0.025]═○═  │ │                                       │ │
│           │  Details   [0.50 ]══○  │ └───────────────────────────────────────┘ │
│           │ ◐ STAGED — none        │ x=142 y=88 target(162,74,201)             │
│           │ ○ RESTART ONLY         │            out(150,80,190) err 14.2       │
│           │  lahc·rasta·floyd·t12  │                                           │
│           │  →/home/me/pics/test.png│                                          │
│           │  [Copy cmdline][Restart]│                                          │
│           │ ─────────────────────  │                                           │
│           │ ▸ Mutations — LineRecolor 41%                                      │
│           │ ▸ Diagnostics — accept 4.8%, 312 migrations                        │
│           │ ▸ Dual frame — off                                                 │
├───────────┴────────────────────────┴───────────────────────────────────────────┤
│ [⏸ Pause]  12/12 workers · cache 64 MB/thr · autosave 3m ago                    │
│                                     [💾 Save (S)] [■ Stop & save] [✕ Abort]     │
└────────────────────────────────────────────────────────────────────────────────┘
```

Note `Stop & save` versus `Abort`: today `Esc` prompts to quit and `S` saves.
Making the difference explicit in two buttons removes a genuinely risky
ambiguity at the end of a multi-hour run.

### 9.8 Data sources

Everything the dashboard shows, and where it comes from. Nothing in this
section requires an optimizer change.

| Display | Source | Status |
|---|---|---|
| Evaluations, rate, best, last-best | `m_eval_gstate` atomics | exists, displayed today |
| Norm. dist + drift | `NormalizeScore`, `m_current_norm_drift` | exists, displayed today |
| Plateau ("N evals ago") | `m_evaluations - m_last_best_evaluation` | exists, not displayed |
| Convergence curve | `ProgressHistory` ring buffer, UI-side sampling | new, UI-only |
| Mutation stats | `m_mutation_stats` | exists, displayed today |
| Island diagnostics | `m_single_*` counters | exists, never displayed |
| Dual phase / focus / copy | `m_dual_phase`, `m_dual_stage_focus_B`, `m_dual_bootstrap_b_copied` | exists, displayed today |
| Best output image | `m_best_snapshot` via acquire load | exists |
| Error heatmap | UI-side: rendered output vs `m_picture` | new, UI-only |
| Elapsed / wall-clock | UI-side timer | new, UI-only |

## 10. Paint / edit — a mode of the viewer, not a separate screen

Revision 1 made painting a third screen. That is wrong: it discards the zoom
and pan state the user built up while looking for the problem, and it severs
the heatmap-to-mask workflow (§9.2) that gives painting its purpose. Painting is
a **mode of the same `ImageViewer`** — the toolbar appears on the left, a
colour/snapshot column on the right, the canvas keeps its zoom, pan and
overlays, and the heatmap can stay visible *underneath the brush*.

**As built**: one `Pause & Edit` button in the dashboard's action row opens the
editor, which takes the whole window - every dashboard panel is frozen while the
optimizer is paused, so keeping them would only cost canvas. The target is a
two-segment control, not tabs: it is one canvas seen two ways, and the segment
carries the cost of applying ("cheap - dirty rectangle" against "full rebuild").
Six shape tools collapsed into three plus a *Fill shapes* toggle, and the eraser
is right-drag, which is what "primary and secondary" already means everywhere
else. Mask strength, mode, floor, feather and scoring live in the editor's
inspector: a painted value means nothing without the multiplier that scales it,
and the retarget an apply already performs covers a parameter change for free.
Branching is no longer a toolbar verb - it is the second entry in the apply
menu, named by its result and showing the folder it would create.

Two edit targets. Both are tier 3 (§6) — the earlier claim that mask edits were
free reweighting was wrong — but they cost very different amounts:

- **Details mask** — the cheap half. The target picture is untouched; only the
  weights folded into `m_picture_all_errors` change, and only over the pixels
  the stroke covered. Applied automatically on stroke end, under a pause of a
  few milliseconds. No staged-Apply UI.
- **Destination** — the expensive half. Edits change the scoring ground truth
  itself, so they are staged and entering this mode **pauses the optimizer
  automatically** (resuming on Apply or Discard — scoring candidates against a
  target mid-edit is meaningless and wastes compute).

### The mask brush is always available

Painting priority must not require having chosen a mask file in Setup. The run
owns a mask *layer* — `w x h` bytes, initialized from the loaded file or to all
zeroes when there is none — and the brush edits that layer whether or not a file
was ever involved.

Zero has to mean exactly today's behaviour, and in **legacy** mode it does:
`1 + strength * v / 255` is 1.0 wherever `v` is 0. Painting is then purely
additive and purely local: no stroke changes the weight of a pixel it did not
touch. That is why an unpainted canvas is bit-identical to a run with no mask,
and why legacy is the mode the brush starts in when no file was loaded.

Normalized and refined modes divide by the image-wide mean, so every stroke
silently demotes everything else and forces a full weight rebuild. The brush
still works there, but the UI says that the background is being renormalized —
it is a different mental model and the user has to know which one they are in.

One forced move: `/details_score` must be on for painting to affect scoring at
all (`rasta.cpp:599`). If the user paints while it is off, turn it on and say
so — no other interpretation matches "put more detail here".

### What applying a stroke actually does

Per §6, a mask edit changes the table every evaluator scores against, so the
sequence is fixed and none of it is optional:

1. **Quiesce the workers** — `pause_requested` + condvar at the iteration
   boundary, with an acknowledgement count. The island LAHC/DLAS path no
   longer ends every iteration under `m_gstate->m_mutex`, so merely taking that
   mutex is not a barrier; every active worker must explicitly report paused
   before the table or its evaluator-owned caches are touched.
2. **Patch `m_picture_all_errors`** for the stroke's dirty rectangle across all
   128 planes. A brush stroke touches a few hundred pixels, so this is
   microseconds rather than the ~5M-entry rebuild a colour change needs.
   Normalized and refined modes force the full rebuild — one pass, tens of ms.
3. **Flush the line caches** — `Evaluator::ClearAllCaches()` on every evaluator
   plus `uncache_insns()` on the global pictures. `line_cache_result::line_error`
   was accumulated from the old table; this is the step whose omission silently
   corrupts everything. Dual's phase switch
   (`RastaDual_MainLoop.cpp:686`) already performs this exact dance and is the
   template to copy.
4. **Re-score the current best and refill the acceptance history** with that one
   value, resetting index, `m_cost_max`, `m_current_cost`, `m_N` and
   `m_best_result`. `reconfigureAcceptanceHistory()` (`rasta.cpp:3014`) already
   does precisely this for resume; painting calls the same code, not a parallel
   implementation of it.
5. Recompute `details_line_priorities` when `/details_allocate` is on.
6. Resume.

Step 4 is not a nicety. Painting *raises* error where you painted, so every
candidate's cost jumps; a history full of pre-edit values would reject
essentially everything and the search would sit dead, while a stale
`m_best_result` would mean no improvement is ever recorded again.

The score therefore steps visibly at the edit. The convergence chart marks it
(§9.3) so it reads as "you painted here" rather than as a bug.

**Dual mode**: the alternating phase scores against its own tables, and nothing
under `src/core/dual/` reads `details_*` at all (§6). Painting is disabled with
a reason there rather than silently ignored.

### What a painted run *is* — persistence, continue, Recent

The moment a stroke lands, the run stops being reproducible from its command
line: `/details=face.png` no longer describes it. Everything else follows from
that one fact.

- **The mask becomes a run artifact.** Every save, autosave included, writes
  `OUTPUT-details.png` beside the other outputs, and the recorded recipe in the
  `.opt` header rewrites `/details=` to point at it, pinning `/details_mode`, `/details_val`
  and `/details_score=on`.
- **Continue stays in the same run.** The user painted because they want *this*
  run to carry on with better priorities; a new folder would discard the
  optimizer state, which is the opposite of the intent. Resume reloads the run's
  own mask, and the existing effective-hash check
  (`m_saved_details_effective_hash`) correctly sees "unchanged" and skips the
  history rebuild. Edit that PNG externally between sessions and the hash
  differs, which already triggers the rebuild — that path exists today.
- **Snapshots, not new conversions.** Before the first edit following a save,
  the pre-edit state is dumped to `snap-NNN/` inside the run folder — the
  `.opt`/`.rp`/`.mic` set plus the mask as it stood. A few hundred KB buys
  "painting is never destructive", and it is the right granularity: a snapshot
  belongs *within* a run, not beside it.
- **Branching is an explicit button.** "Branch to new run" allocates the next
  `rc-<image>-NNN`, copies the current program state and the edited mask,
  registers it in the history and carries on there. A deliberate act, never
  automatic.
- **Reuse copies the mask** into the new run folder rather than referencing the
  old one, so deleting an old run can never break a newer one — the same
  self-containment principle that makes the `rc-` folder worth having.
- **Recent** shows an "edited" badge and a snapshot count, both read from new
  `.opt` header lines (`; Mask Edited:`, `; Snapshots:`) so no folder walking is
  needed. Continue resumes the latest state; a card can expand to continue from
  a snapshot instead.


Tools: brush (size + square/round), line, rect and filled rect, oval and filled
oval, bucket fill, eyedropper, and a **revert-brush** that paints back to the
pre-edit original value — more useful here than a generic eraser, since
"restore what preprocessing actually produced" is the meaningful baseline for
both targets. Left/right mouse bind to primary/secondary colour; a palette grid
restricts the picker to the active Atari hardware palette (toggleable).

For mask editing specifically, the brush paints *priority*, so the palette is
replaced by a 0–1 strength ramp and the natural default is "paint white where
the heatmap is red". Eyedropper and palette snapping have no meaning there and
are hidden rather than disabled — there is no second colour to pick.

```
┌ test.jpg → test.png · 0.0217 · RastaConverter ─────────────────── ● running ─┐
├──────────────────────────────────────────────────────┬───────────────────────┤
│ [Destination │▐ Details Mask ▌]  ↶ ↷ │ ✥ 🔍 200% │…│ ▾ BRUSH               │
│ ✎ ╱ ▭ ▬ ◯ ● ◨ ▧revert  │ Size 5px  Shape ●          │  Strength  ▁▂▃▄▅▆▇█   │
├──────────────────────────────────────────────────────┤   ├────────●──── 0.80 │
│ ┌──────────────────────────────────────────────────┐ │  Hardness ══○═══ soft │
│ │                                                  │ │                       │
│ │        output @200%, heatmap underneath,         │ │ ▾ MASK                │
│ │        mask overlay 40%, brush cursor ○          │ │  Mode  legacy      ▾  │
│ │                                                  │ │  Strength [3.00]      │
│ │             ·······                              │ │    white = 4.00x error│
│ │           ···█████···   ← painted over the face  │ │  Overlay [Off|On|Only]│
│ │             ·······                              │ │    opacity ══○══ 40%  │
│ │                                                  │ │  Scoring   [x] on     │
│ └──────────────────────────────────────────────────┘ │    turned on by paint │
│ x=142 y=88  mask 0.80  err 14.2      applied · 6 ms  │                       │
│ history reset to 0.0217 · workers resumed            │ ▾ SNAPSHOTS           │
├──────────────────────────────────────────────────────┤  before mask edit  ⟲  │
│ Strokes apply on release. [↶ Undo stroke] [Branch…]  │  ＋ Save snapshot now │
└──────────────────────────────────────────────────────┴───────────────────────┘
```

Three things that wireframe is making a point of: there is no Apply button, the
status line reports what applying *cost* (`applied · 6 ms`) and what it *did*
(`history reset to 0.0217`) rather than hiding a discontinuity the user will see
in the chart anyway, and the mask panel states the multiplier in the units of
the active mode (`white = 4.00x error`) instead of an abstract 0–1 strength.

### Undo vs. Snapshot — two different safety nets

- **Undo** (Ctrl+Z, toolbar): per-stroke, scoped to the canvas being edited. A
  full-buffer copy before each stroke is ~230 KB at 320×240 — cheap enough that
  whole-buffer snapshotting beats diffing at this resolution. Not persisted.
- **Snapshot**: a manual, named restore point spanning the *entire* conversion
  state — destination image, details mask, objective weights, and the
  current-best raster program — reusing the existing checkpoint format that
  already backs `/continue`. Listed with thumbnail, timestamp and label;
  "Restore" swaps the whole live session back to that point. Undo protects a
  stroke; Snapshot protects a session.

## 11. The Done state

Neither previous revision defined what happens when a run ends — by
`/max_evals`, by Stop, or by `/preprocess` completing. It needs to be a real
state, not a frozen dashboard:

- Header switches to **Finished — 412 M evaluations, norm. dist 0.021438**, and
  the reason it stopped.
- The viewer stays fully interactive (zoom, compare, heatmap) — inspecting the
  result is the main activity at this point.
- **Artifacts list**: every file written (`.png`, `.mic`, `.opt`, `.opt.h`,
  `.opt.ini`, `.pmg`, or the `out_dual_A/B.*` set), with sizes and an "open
  folder" action. Users routinely lose track of which files the Generator needs.
- **Build & Run on Atari** (§12) is the primary action here.
- Three exits: **Continue this run** (raise `/max_evals` and resume from the
  checkpoint), **New run with these settings** (returns to Setup pre-filled),
  and **Copy as command line**.

## 12. Build & Run on Atari

`no_name.asq` (`Generator/no_name.asq`, `GeneratorDual/no_name.asq`) hardcodes
the filenames it expects — `output.png.mic`, `output.png.opt`, `output.png.opt.h`,
`output.png.opt.ini`, `output.png.pmg` for single mode; `out_dual_A.*`/
`out_dual_B.*` for dual — independent of whatever `cfg.output_file` basename the
user actually chose. `Generator/mads` is a thin launcher script that
`uname -s`/`uname -m`-selects the right bundled binary (`mads-macos-arm64` on
Darwin/arm64, `mads-linux-x86_64` on Linux/x86_64, `mads.exe` on Windows) and
errors explicitly on unsupported hosts.

Proposed `XexBuilder` flow:

1. **Availability check** (gates whether the Build button is enabled): port the
   shell launcher's `uname`→binary-name `case` into C++. No match → button
   disabled with a tooltip naming the unsupported host (P6), matching the shell
   script's own error text.
2. **Scratch build directory, not the installed `Generator/` in place** —
   writing into the install dir breaks under a read-only install location
   (`/usr/`, `Program Files`) and cannot handle two RastaConverter instances
   building concurrently. Create `<output_dir>/xex_build/`, copy the static
   template (`no_name.asq`, `no_name.h`, the one matching bundled `mads-*`
   binary) plus the current run's artifacts renamed to the hardcoded names the
   template expects (single or dual, per active mode).
3. **Invoke MADS** in that directory (`<mads-binary> no_name.asq -o:output.xex`),
   capture stdout/stderr into the panel's log console (tail-scrolled), status
   pill Idle → Building → Success/Failed.
4. **On success**: show the `.xex` path and size; "Open" hands off to the
   platform's default handler — `xdg-open` (Linux), `open` (macOS),
   `ShellExecute`/`start` (Windows). Explicitly best-effort: this depends on an
   `.xex` file association existing on the user's machine (Altirra, Atari800,
   etc.), which RastaConverter cannot guarantee — the panel should say so
   rather than imply a launch is certain. An "auto-open on success" checkbox
   controls whether this fires automatically.
5. **On failure**: keep the log visible, do not attempt to open anything.

Reachable from the during-run dashboard (builds from the current best — useful
for a mid-run hardware sanity check) and from the Done state, which is the
primary real use case.

## 13. Running multiple instances

Running several conversions at once — same image with different settings, or
different images, from the same or different folders — is normal use, and
help.txt already acknowledges it twice (lines 57 and 62, both warnings about
overwriting). The GUI must make it safe rather than merely warn.

### 13.1 Why multiple *processes*, not tabs in one process

The obvious alternative — one window managing several conversions — is
structurally blocked. RastaConverter's conversion state is process-global, not
per-instance:

| Global | Location |
|---|---|
| `rgb atari_palette[128]` | `TargetPicture.cpp:8` |
| `f_rgb_distance distance_function` | `TargetPicture.h:11` |
| `set<unsigned char> color_indexes_on_dst_picture` | `rasta.cpp:130` |
| `OnOffMap on_off` | `rasta.cpp:132` |
| `Evaluator eval` | `rasta.cpp:134` |
| `int solutions`, `bool quiet` | `rasta.cpp:76-78` |

Two concurrent conversions in one process would share one palette, one distance
function and one evaluator. Making them per-instance is a large, invasive
refactor with real risk to the hot path, and it buys nothing the OS does not
already provide. **Multiple processes is the correct architecture**; the design
work is coordination between them, not consolidation.

### 13.2 What actually collides

| Resource | Collision | Severity |
|---|---|---|
| `output.png.*` artifacts (`.mic`, `.opt`, `.opt.h`, `.opt.ini`, `.pmg`, `.rp`, `.optstate`) | Same output basename in the same folder — the default for *every* instance | **Severe** — silently destroys a multi-hour run |
| `/continue` resume files | Resolved from the output basename, so a collision makes resume load the wrong run | **Severe** |
| CPU threads | Each instance defaults to full hardware concurrency | **High** — N instances oversubscribe N× and all run slower |
| `<output_dir>/xex_build/` (§12) | Shared when output dirs match | Moderate |
| Preset store | Concurrent writes to a shared user-level file | Moderate — losing a preset is annoying |
| ImGui layout `.ini` | Last writer wins | Trivial |

### 13.3 Measures

**Derive the default output name from the input file.** `test.jpg` → `test.png`
rather than the constant `output.png`. This alone removes the common case —
different images in one folder — without any coordination machinery. GUI-only:
the CLI default stays `output.*` so no script changes behaviour (P5's spirit
applied to defaults).

One interaction to note: `Generator/no_name.asq` hardcodes `output.png.*`, so a
non-default basename breaks the manual `build.bat` route. The GUI can afford
non-default names *precisely because* `XexBuilder` (§12) copies and renames
artifacts into a scratch directory. The Done screen should say so when the
basename is non-default, since a user who then tries `build.bat` by hand will
otherwise be confused.

**A lock file per run.** On start, write `<output_basename>.lock` containing
pid, start time, input path, and thread count. In Setup, the Output field
checks for a live lock as soon as a path is chosen and shows an inline warning
naming the owning pid and its start time — visible in the bottom bar of the
§7.4 wireframe. Liveness is a `kill(pid, 0)` / `OpenProcess` check; a stale lock
offers a one-click clear. Remove on clean exit; a crashed run leaves a stale
lock that is detected, not fatal.

This is the one measure that must not be skipped: silently overwriting another
instance's in-progress output is the worst failure the tool can have, and today
nothing prevents it.

**Advisory thread-budget coordination.** Each running instance drops a small
descriptor into a per-user runtime directory (`$XDG_RUNTIME_DIR/rastaconverter/`,
`%LOCALAPPDATA%\RastaConverter\run\`, or the temp dir). Setup reads them and,
when the total requested threads exceeds hardware concurrency, shows:

```
⚠ 2 instances requesting 24 threads on 16 cores — each will run ~1.5× slower.
   [Share cores evenly (8 each)]     [Keep 12]
```

Advisory only — surfaced with a one-click fix, never silently enforced. Magic
thread reallocation would be worse than the problem. Unreadable or stale
descriptors are ignored; this is best-effort telemetry between cooperating
processes, not a protocol, and a missing registry directory simply disables the
warning.

**Per-instance scratch:** `<output_dir>/xex_build/<output_basename>/` rather
than a shared `xex_build/`.

**Atomic preset writes:** write to a temp file and `rename()` over the target,
so a concurrent write can lose an *update* but never corrupt the store. The
ImGui layout `.ini` gets no special handling — last writer wins is fine for a
window layout.

### 13.4 Instance identity in the UI

With four windows open the user must be able to tell them apart at a glance,
from the taskbar:

- **Window title**: `test.jpg → test.png · 0.0214 · RastaConverter`, i.e. input,
  output basename and current score. During Setup the score is omitted; in Done
  it is prefixed `✓`.
- The full output **path** — not just the basename — appears in the Run panel's
  restart-only recap (§9.4), because "which folder is this one writing to?" is
  precisely the question that arises with several instances open.
- The `Copy as command line` output (§8) is per-instance and includes `/o`, so
  reproducing any specific window's run is one paste.

### 13.5 Deliberately not solved

A **batch queue** — one instance running conversions back to back — is a
recurring request shape and would sidestep CPU oversubscription entirely by
serializing. It is *not* proposed here: the globals in §13.1 are reset by
`SetConfig`/`Init` for a fresh run, but nothing verifies that every one of them
is fully reinitialized between runs in the same process, and a queue that
silently leaks state from run N into run N+1 would be far worse than no queue.
It is listed as an open question (§16), gated on an audit of exactly that.

## 14. Keyboard ownership (conflict policy)

The existing `RastaSDL::NextFrame()` binds single keys: `S`/`D` → SAVE, `A` →
SHOW_A, `B` → SHOW_B, `M` → SHOW_MIX, `Esc` → quit-prompt. Once ImGui is present
these must not double-fire (typing `output_b` into a text field, or pressing `B`
to paint, must not also flip the dual-mode view).

Policy: after `ImGui_ImplSDL3_ProcessEvent`, consult `ImGui::GetIO()` — if
`WantCaptureKeyboard` is set (a widget or text field is focused) **or** paint
mode is active, the legacy key `switch` is skipped entirely for that event. The
legacy shortcuts keep working exactly as today on the plain dashboard and are
suppressed only when ImGui or the canvas legitimately owns the keyboard.

New bindings live outside the legacy `switch`: `Space` pause/resume, `+`/`-`
zoom, `H` heatmap, `G` grid, `Tab` cycle view, and inside paint mode `Ctrl+Z`
undo, `[`/`]` brush size, `X` swap colours.

## 15. Staged rollout

1. Wire ImGui in behind `ENABLE_LIVE_UI`; re-skin the existing 3-image/stats
   display as ImGui widgets with **zero new functionality**. Proves the
   integration and the event-routing/thread-safety story before anything
   interactive is added.
2. The **three-column shell** (§7.1a) plus `ImageViewer` — zoom, pan, view
   switching, compare modes. Pure UI, no optimizer contact. Building the shell
   first means Setup, Run and Done are variations on one layout rather than
   three screens later reconciled; `ImageViewer` is the substrate for steps 3,
   6 and 8.
3. Progress/convergence panel + error heatmap. Still read-only, still no
   optimizer change, and it delivers the highest-value answer (§9.1 q2 and q3)
   before any write path exists.
4. **Target-preview refactors** (§7.6): non-destructive colour correction, and
   `PrepareDestinationPicture()` parameterized over its output buffer and
   per-line callback. Pure refactors with no behaviour change — verifiable by
   byte-comparing `/preprocess` output before and after. They unblock both the
   Setup preview and the tier-3 retarget path, so they come before either.
5. `/saturation` and `/vibrance` as CLI options, defaults preserving
   bit-identical output (§6). Independently testable without any GUI.
6. Setup screen + presets, including the three-way target preview and the
   palette-utilization grid. Validated by round-tripping every option through
   `Copy as command line` (§8).
7. Tier-2 live sliders end-to-end — exercises `LiveTunables`, the atomic read
   path in workers, and the tier badges of P4.
8. Paint mode — mask editing first. It is tier 3 like destination editing, but
   the cheap half of it: the rebuild is confined to a dirty rectangle and the
   target picture is untouched, so it exercises the pause/flush/rescore/reset
   path (§10) without the staged-Apply UI or the `-dst` rewrite. Destination
   editing follows once that path is verified in isolation.
9. Done state + `XexBuilder` — independent of the rest, can land any time after
   step 1.

**Multi-instance safety (§13) is not a numbered step** — it ships with step 6.
The input-derived output name and the lock-file check are small, they belong to
the Setup screen that introduces them, and shipping a GUI that makes launching
a second instance easy *without* them would actively increase the rate of
destroyed runs compared to today's CLI.

Steps 4 and 5 are the notable reordering versus revision 2: they are plain C++
work with no ImGui involvement, they are verifiable by byte-comparison against
today's output, and everything visual in stages ② and the tier-3 retarget path
depends on them. Doing them early de-risks the largest UI step.

## 16. Open questions

- Exact `ImGuiIO` font handling: reuse the bundled `clacon2.ttf` or add a second
  UI-appropriate font for panel chrome (the current font is loaded for the retro
  stats display, not necessarily legible at small UI sizes).
- Whether to take ImPlot for the convergence chart (log axes, improvement
  markers) or stay on stock `ImGui::PlotLines` (§3).
- Where `retarget_generation` should also invalidate the PMG capture/replay fast
  path — needs a code-level check before step 6's destination-editing work.
- Whether `xex_build/` should be cleaned automatically or left for inspection
  (assembler log, intermediates) — recommend leaving it and documenting it as a
  build-artifact directory the user may delete freely.
- Whether `/saturation` and `/vibrance` (§6) are in scope — they are the only
  proposed *new* conversion options in this document and, being CLI options
  with defaults that preserve byte-identical output, can ship independently of
  any GUI work.
- The exact vibrance weighting curve. The intent is fixed (gain falls as
  existing chroma rises) but the curve shape wants tuning against real Atari
  palette output, using the palette-utilization count (§7.6) as the objective
  measure rather than eyeballing.
- Whether the tier-C preview (`ciede` / Knoll) should auto-update after an idle
  timeout at all, or only on the explicit button. Auto-update is friendlier but
  can burn seconds of CPU per slider release on a machine already running the
  optimizer at full thread count.
- ~~**Whether a sequential batch queue is safe**~~ (§13.5). The audit this was
  gated on is done: of the process-globals listed in §13.1, `atari_palette` and
  `distance_function` are rewritten wholesale per run, `on_off` is memset by its
  loader and only consulted when a file is configured, `solutions` is reassigned
  by the parser, and the global `Evaluator eval` turned out to be dead code and
  has been removed. Only `color_indexes_on_dst_picture` accumulates, and
  `ResetProcessGlobalsForNewRun()` now clears it. The live UI already runs
  several conversions back to back in one process on the strength of that; an
  unattended queue is now a UI question rather than a safety one.
- Whether a sequential batch queue is worth building. It would neatly solve
  CPU oversubscription across conversions, but requires first auditing that
  every process-global in §13.1 — `atari_palette`, `distance_function`,
  `color_indexes_on_dst_picture`, `on_off`, `eval`, `solutions` — is fully
  reinitialized by `SetConfig`/`Init` between runs in one process. Until that
  audit exists, a queue risks silently leaking state from one conversion into
  the next, which is worse than not having one.
- Where the cross-instance runtime registry (§13.3) should live on each
  platform, and whether it is worth shipping at all versus the simpler lock
  file alone. The lock file prevents data loss; the registry only prevents
  slowness.

## 17. Corrections applied in revision 2

Recorded so the earlier mockups are not re-adopted by mistake.

- **"Mutation rate" removed.** No such option or `Configuration` field exists;
  `grep -rniE 'mutation_rate|"mutation' src/` returns nothing.
- **"Budget 45 min" replaced by `/max_evals`.** There is no wall-clock budget;
  the only limit is an evaluation count (`config.h:108`).
- **"Optimizer [Island model]" corrected.** `/opt` takes `lahc|dlas|legacy`
  (`config.h:134-135`). "Island model" is an internal parallelization detail,
  not an optimizer choice.
- **Fabricated ETA removed** for open-ended runs (§9.3).
- **`details_floor` no longer offered without `details_mode`** — it is
  normalized-mode-only and the mode defaults to `legacy`, so the revision-1
  slider was inert by default.
- **Objective and Details groups marked single-frame-only**, verified by
  absence from `src/core/dual/`.
- **35 previously unreachable options given a home** (§7.2), including all nine
  dual-frame sub-options, all seven remaining details-mask options, the three
  objective weights, `/onoff`, `/init`, `/s`, `/filter`, `/h`, `/predistance`,
  `/brightness`, `/contrast`, `/gamma`, `/unstuck_*`, `/cache`, `/seed`,
  `/max_evals` and `/save`.
