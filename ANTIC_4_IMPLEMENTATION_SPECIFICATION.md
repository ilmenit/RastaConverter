# ANTIC 4 Implementation Specification

Status: proposed implementation plan  
Target branch: `antic-mode-4`  
Scope: single-frame RastaConverter output, 160×240 pixels

## 1. Purpose

Add ANTIC mode 4 as an alternative graphics mode for RastaConverter.

The existing ANTIC mode E output remains the default and must remain unchanged.
The first ANTIC 4 implementation should favor correctness and a useful end-to-end
result over maximum optimizer performance.

ANTIC 4 is interesting because it has the same 160-pixel horizontal resolution
as the current output, while each 4×8-pixel character cell can select either
`COLPF2` or `COLPF3` for its `11` pixels. This gives the optimizer one additional
global playfield color, selected locally by bit 7 of each screen byte.

## 2. Goals

The first usable implementation shall:

- accept the existing 160×240 input and optimizer workflow;
- provide an explicit `antic4` graphics-mode option;
- model ANTIC 4 DMA timing per scanline;
- optimize raster color changes, player graphics, and the ANTIC 4 cell
  attribute;
- emit a screen, character sets, PMG data, raster program, and runnable
  assembly example;
- reproduce the internal renderer in Altirra/AltirraBridge;
- leave mode-E output and performance unaffected when ANTIC 4 is not selected.

## 3. Non-goals for the first version

- ANTIC 4 combined with dual-frame mode;
- arbitrary display widths, horizontal scrolling, or narrow/wide playfields;
- arbitrary image heights; the first version supports 160×240 only;
- missiles, player overlap modes, fifth-player mode, or new PRIOR modes beyond
  the priority behavior already supported by RastaConverter;
- automatic character reuse or character-set compression;
- highly optimized ANTIC 4 caching in the first correctness milestone;
- changing the existing mode-E file format or generated program.

Unsupported combinations should fail early with a clear error rather than
silently falling back to mode E.

## 4. Hardware contract and evidence

This section is the contract that both the evaluator and output generator must
implement. Pixel decoding and the three DMA maps are verified against Altirra
source. The current differential PoC in
[`tools/antic4_poc.py`](tools/antic4_poc.py) verifies one fixed charset and
randomized color writes. Dynamic `CHBASE` transitions and frame-wrap behavior
remain source-derived until the dedicated bridge tests in section 10 pass.

### 4.1 Pixel decoding

One mode-4 screen byte describes one 4×8-pixel character cell:

- bits 0–6 select one of 128 character glyphs;
- bit 7 selects the meaning of glyph pixel value `11`;
- each character-data byte contains four two-bit pixels, most significant pair
  first.

The color mapping is:

| Glyph bits | Screen bit 7 = 0 | Screen bit 7 = 1 |
|---|---|---|
| `00` | `COLBK` | `COLBK` |
| `01` | `COLPF0` | `COLPF0` |
| `10` | `COLPF1` | `COLPF1` |
| `11` | `COLPF2` | `COLPF3` |

Bit 7 is therefore not a normal inverse-video bit. It switches only the fourth
color of that cell. There are five global playfield color registers, but at
most four playfield colors are available in any one 4×8 cell.

The attribute selects a register, not a fixed color value. `COLPF2` and
`COLPF3` may still change independently through the raster program during the
cell's eight scanlines. `CHACTL` inversion and blink controls do not govern this
mode-4 attribute.

### 4.2 Screen and font geometry

For a 160×240 image:

- screen width: 40 bytes;
- character rows: 30;
- screen memory: 1,200 bytes;
- glyph scanlines per character: 8;
- one mode-4 character set: 1,024 bytes, containing 128 glyphs.

The output does not attempt glyph deduplication. Every output cell position gets
a deterministic glyph number in a packed character set.

Three 40-cell character rows fit in one character set:

```text
character row 0 in group -> glyphs 0..39
character row 1 in group -> glyphs 40..79
character row 2 in group -> glyphs 80..119
glyphs 120..127          -> unused
```

Thirty character rows therefore require 10 character sets, or 10 KiB. For
screen cell `(cell_x, char_y)`:

```text
charset_index = char_y / 3
glyph_index   = (char_y % 3) * 40 + cell_x
screen_byte   = glyph_index | (attribute[cell_x, char_y] ? $80 : $00)
font_offset   = charset_index * 1024 + glyph_index * 8 + (pixel_y % 8)
```

#### Chosen charset policy: one private glyph per cell

This 120-of-128 layout is the first-version design, not merely one possible
packing:

- one 1 KiB charset remains constant for three complete text rows, or 24
  scanlines;
- all 120 cells in those rows have distinct glyph indices and therefore
  independent 4×8 pixel data;
- the next three rows reuse glyph indices 0–119 under the next `CHBASE`;
- glyphs 120–127 remain unused.

The eight spare glyphs cannot usefully hold part of a fourth 40-cell row:
`CHBASE` applies to the whole scanline, so using only eight glyphs from another
charset would require a midline `CHBASE` split. That would add timing cost and
couple the screen layout to a fragile raster effect for negligible memory
saving.

“Changing the charset” at runtime means changing `CHBASE` once per three-row
group. The program does not rewrite font RAM while the picture is displayed.
All ten character sets are generated before display.

This layout gives the same pixel freedom as a 2-bpp bitmap inside the legal
ANTIC-4 color set: every cell can have arbitrary glyph bytes, with no glyph
shared by another cell in the same three-row group. It costs 640 unused bytes
overall (8 glyphs × 8 bytes × 10 sets), which is a good trade for simple,
independent encoding.

### 4.3 DMA timing

The supported hardware profile is:

- normal-width playfield;
- ANTIC display-list DMA enabled;
- single-line player/missile DMA enabled;
- no horizontal scrolling;
- one LMS at the beginning of screen memory;
- 40 mode-4 cells per row.

Under that profile, the source-verified CPU availability is:

| Scanline type | Stolen cycles | CPU slots |
|---|---:|---:|
| First character-row scanline, with LMS | 89 | 25 |
| Other character-row first scanline (“badline”) | 87 | 27 |
| Character-row continuation scanline | 54 | 60 |

A badline occurs at visible lines where `y % 8 == 0`. It fetches 40 screen
character bytes and 40 character-data bytes. A continuation line fetches only
the 40 character-data bytes. The first line also pays for the LMS address.

For comparison, the current mode-E layout with an LMS on every line has 57 CPU
cycles on every visible scanline.

Counts alone are not sufficient. Register-write positions depend on the exact
stolen-cycle map. The production timing code must use a per-line CPU-cycle to
color-clock map derived from the same ANTIC requests modeled by the PoC:

- display-list opcode and optional LMS fetches;
- 40 character-name fetches on badlines;
- 40 character-data fetches on every scanline;
- single-line missile and four-player DMA;
- ANTIC refresh requests, including delayed refresh behavior.

For the supported profile, the exact DMA requests within a hardware scanline
are:

```text
missile DMA:          0
display-list opcode:  1 on character-row starts
player DMA:           2, 3, 4, 5
LMS address:          6, 7 on the first row only
character names:      18, 20, ..., 96 on character-row starts
character data:       21, 23, ..., 99 on every visible scanline
```

Delayed refresh resolves to:

```text
LMS/ordinary badline: 98
continuation:          26, 30, 34, 38, 42, 46, 50, 54, 58
```

These source lists are canonical test fixtures. Production code may generate
the final available-slot arrays at startup or compile time, but tests must
compare the complete arrays rather than checking only the 25/27/60 totals.
Do not encode ANTIC 4 as another opaque hand-written string like the current
mode-E table in `Cycles.cpp`; generate the maps from named DMA-cycle lists while
preserving the existing mode-E map as a regression fixture.

As in the current raster kernel and the PoC, a logical line's execution window
starts at WSYNC release, ANTIC cycle 106 of the preceding scanline. Represent
that as position `-8`, and end the line before cycle 106 of the current
scanline. The timing map must therefore preserve negative visible-X offsets; a
table built only from cycles 0–113 would place early register writes
incorrectly.

The canonical available CPU-slot arrays in that logical window are:

```text
LMS badline (25):
  -8,-7,-6,-5,-4,-3,-2,-1,8,9,10,11,12,13,14,15,16,17,19,
  100,101,102,103,104,105

ordinary badline (27):
  -8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,10,11,12,13,14,15,16,17,19,
  100,101,102,103,104,105

continuation (60):
  -8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,13,14,15,16,17,18,
  19,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,72,74,
  76,78,80,82,84,86,88,90,92,94,96,98,100,101,102,103,104,105
```

Copy these arrays into independent test fixtures. Do not compute the expected
fixture by calling the production DMA-map builder.

The relevant Altirra implementation is in
[`antic.cpp`](/home/ilm/Documents/GitHub/AltirraSDL/src/Altirra/source/antic.cpp).
The PoC is the executable, repository-local reference for the subset used here.

### 4.4 Repository examples and tools

[`2p-game/scroll2p-2fonts.asm`](2p-game/scroll2p-2fonts.asm) demonstrates a
mode-4 display list, 1 KiB character sets, and changing `CHBASE` from a DLI.
It is useful evidence that the memory layout is practical. It is not a timing
template for RastaConverter because it does not combine the same per-scanline
raster program, PMG DMA, and randomized register writes.

The bundled
[`AltirraBridge-nightly-linux-x86_64`](AltirraBridge-nightly-linux-x86_64)
supports the headless operations needed by the differential test: bare boot,
memory loading and poking, display-list and ANTIC setup, program execution, and
RAWSCREEN capture. It does not expose Altirra's DMA map directly, so exact DMA
positions remain modeled from Altirra source and validated through pixel
output.

## 5. User-facing configuration

Add:

```text
/graphics_mode e|antic4
```

Default: `e`.

The value must be stored in configuration and saved in resumable recipes and
the optional ANTIC-4 optimizer-state extension described in section 9.1. A
resumed run must reject a graphics-mode mismatch. Existing mode-E state files
without the extension remain valid.

For the first version:

- `graphics_mode=e` follows the current code path exactly;
- `graphics_mode=antic4` requires 160×240 and single-frame mode;
- `graphics_mode=antic4` with `/dual on` is an error.

Use a typed enum internally rather than repeated string comparisons:

```cpp
enum class GraphicsMode {
    AnticE,
    Antic4
};
```

## 6. Core data model

### 6.1 Add `COLPF3` without renumbering existing targets

Append `E_COLOR3` to `e_target` immediately before the new
`E_TARGET_MAX`. Do not insert it among `E_COLOR0..E_COLBAK`: existing enum
values are serialized and several parts of the code depend on their order.

Appending it means color registers are no longer one contiguous enum range.
Replace affected range assumptions with small helpers:

```cpp
bool IsPlayfieldColorTarget(e_target);
bool IsColorTarget(e_target);
const std::array<e_target, 4>& PlayfieldTargetsForCell(bool use_pf3);
```

The project is C++17, so this interface must not use `std::span`.

The ANTIC 4 playfield candidate sets are:

```text
attribute 0: COLBAK, COLPF0, COLPF1, COLPF2
attribute 1: COLBAK, COLPF0, COLPF1, COLPF3
```

PMG candidates remain subject to the existing player coverage and priority
rules.

Audit all code that assumes `E_COLOR0 <= target <= E_COLBAK`, including:

- closest-color selection;
- random instruction target selection;
- structured-solver target validation;
- on/off register controls;
- register name tables;
- raster-program load/save;
- statistics and target packing.

The single-frame PMG restart path currently snapshots color registers in
`PmgPixelSnapshot::color_regs[E_COLPM3 + 1]`. Appended `E_COLOR3` lies outside
that range. Expand the snapshot (the simple first implementation may use
`E_TARGET_MAX`) and restore `E_COLOR3` along with the other colors; otherwise a
midline `COLPF3` write is evaluated at the wrong time after PMG adds a bit and
replays the pixels.

The packed line-cache target format has four bits per target. Appended
`E_COLOR3` still fits, but retain an assertion documenting that constraint.

Appending the enum also increases `mem_regs_init`. The current `.ini` emitter
iterates that array with `sizeof`, which would add a `COLOR3` initialization to
ordinary mode-E output. Replace serializer loops with explicit mode-specific
ordered target lists:

```text
mode E:  the existing 12 targets, in the existing order
ANTIC 4: the same targets plus E_COLOR3
```

Mode-E initialization, mutation selection, on/off parsing, `.rp/.ini` output,
and loading must omit `E_COLOR3`; its otherwise-unused array byte stays zero.
This is required for byte-for-byte legacy output compatibility.

### 6.2 Store the cell attributes with the candidate picture

Add 30 packed attribute rows to `raster_picture`:

```cpp
std::array<uint64_t, 30> antic4_attributes{};
```

Only the low 40 bits of each row are used. Bit `x` selects `COLPF3` for the
cell at `(x, char_y)`; a clear bit selects `COLPF2`.

Keeping attributes inside `raster_picture` makes them part of candidate copy,
accept/reject, publication, migration, save, and resume behavior. Extend those
operations explicitly. For mode E the array remains zero and is ignored.

Do not store glyph bytes in optimizer state. Glyph data is a lossless encoding
of the selected per-pixel playfield targets and can be generated when saving.

### 6.3 Separate DMA timing from line scheduling

Replace the single global timing limit with two small concepts:

```cpp
struct DmaTimingProfile {
    int cpu_slots;
    std::array<ScreenCycle, CYCLES_MAX> cpu_to_screen;
    int cpu_to_screen_count;
};

enum RasterFixedEvent : uint8_t {
    RasterFixedEventNone = 0,
    RasterFixedEventChbase = 1
};

struct RasterLineSchedule {
    const DmaTimingProfile* dma;
    int optimizer_cycle_limit;
    int suffix_cycles;
    uint8_t fixed_events;
};
```

`DmaTimingProfile` describes only hardware bus ownership. Provide four
immutable DMA profiles:

- the existing mode-E profile;
- ANTIC-4 initial LMS badline;
- ANTIC-4 ordinary badline;
- ANTIC-4 continuation.

`RasterLineSchedule` overlays fixed program costs on the selected hardware
profile. ANTIC 4 has four visible scheduling cases:

| Scheduling case | DMA profile | Optimizer limit |
|---|---|---:|
| Initial LMS badline | LMS badline | 22 |
| Ordinary badline | Ordinary badline | 24 |
| Continuation | Continuation | 54 |
| Continuation with `CHBASE` | Continuation | 48 |

The `CHBASE` case reuses the continuation DMA map; it is not another hardware
profile. A helper selected by `(graphics_mode, y)` returns the complete
schedule, deriving the optimizer limit from CPU slots minus suffix and fixed
events.

Do not create a general ANTIC emulator in the C++ code. These four DMA profiles
and the small schedule overlay are sufficient for the supported output modes.

The evaluator, mutations, structured solver, validation, statistics, and
assembly emitter must all query the same schedule and its referenced DMA
profile. There must not be an independent “approximately equivalent” cycle
formula in any of those paths.

## 7. Raster-program scheduling

### 7.1 Exact line length

The emitted raster program must consume every available CPU cycle on each
scanline so the next line begins at the expected position.

The existing three-cycle zero-page `CMP` tail can be retained:

| Line type | CPU slots | Initial optimizer limit | Fixed suffix |
|---|---:|---:|---|
| LMS badline | 25 | 22 | 3-cycle `CMP` |
| Badline | 27 | 24 | 3-cycle `CMP` |
| Continuation | 60 | 54 | extra 3-cycle `CMP` + existing 3-cycle `CMP` |
| Continuation with `CHBASE` | 60 | 48 | 6-cycle event + both `CMP`s |

This deliberately limits continuation-line optimizer instructions to the
current 54-cycle maximum. It avoids adding odd-length optimizer instructions
in the first implementation while still producing exact 60-cycle lines.
Unused optimizer cycles are filled with two-cycle NOPs.

The extra continuation-line `CMP` uses the existing zero-page scratch operand.
It changes flags only; the emitted straight-line raster code does not consume
those flags, and the existing final `CMP` replaces them immediately afterward.

This is a correctness-first policy, not a hardware limit. A later performance
change may expose the remaining three continuation cycles, but only with tests
showing exact line alignment.

### 7.2 `CHBASE` changes

Each character set covers three character rows, so `CHBASE` changes every 24
visible scanlines. Set 0 is installed before the raster display starts. Sets
1–9 are selected by fixed `LDA #value` / `STA CHBASE` code scheduled late on
lines:

```text
23, 47, 71, ..., 215
```

In the supported normal-width profile, the last current-line character-data DMA
request is at ANTIC cycle 99 and the following badline's first character-data
request is at cycle 21. A `CHBASE` write takes effect two CPU cycles after the
hardware write. Select a slot whose effective time lies strictly between those
fetch regions, and verify it with AltirraBridge. Reserve all six instruction
cycles before giving the remaining budget to the optimizer.

With the initial suffix policy, a `CHBASE` switching line therefore has a
48-cycle optimizer limit:

```text
48 optimizer/filler + 6 LDA/STA CHBASE + 3 CMP + 3 CMP = 60
```

The fixed sequence must occupy the reserved late slot; it must not merely be
inserted wherever an optimizer mutation leaves a six-cycle gap.

The intended first schedule starts the `LDA` after exactly 48 executable CPU
slots from the line's WSYNC-release boundary. In the source-verified
continuation-line map, the `STA CHBASE` write cycle then lands at ANTIC cycle 98
and becomes effective at cycle 100: after the final cycle-99 fetch and well
before the next row's cycle-21 character-data fetch. Treat these positions as
assertions. This transition is not bridge-verified until the dedicated
two-font screenshot test passes.

Treat a `CHBASE` update as a fixed scheduling event, not as an optimizer
mutation. Do not use DLI handlers in the first implementation; keeping the
change in the same cycle model makes the generated program and evaluator
agree.

`LDA #charset` intentionally replaces A. Do not add save/restore code: it costs
scarce late-line cycles and can move the CHBASE write into the unsafe fetch
window. Instead, the evaluator must execute the fixed LDA/STA event at the same
logical point as the emitter and carry the new A value into the cached outgoing
register state and the following line. The event must not be an exporter-only
insertion.

Keep this event outside the mutable `raster_line.instructions` vector.
`raster_line.cycles` continues to count optimizer-controlled instructions only,
and the 48-cycle switching-line limit already reserves the fixed event. In the
emitted `.rp`, delimit it explicitly:

```asm
; ANTIC4_FIXED_CHBASE_BEGIN
        lda #>charset_1
        sta CHBASE
; ANTIC4_FIXED_CHBASE_END
```

The raster-program loader must skip these tagged physical instructions and
reconstruct the fixed event from graphics mode and line number. Otherwise it
would reload the `LDA` as a mutable optimizer instruction while failing to
represent `STA CHBASE`, causing cycle and A-state drift after resume.

Do not ship until a safe slot is demonstrated at every transition. Using more
character sets would increase switching frequency without fixing an unsafe
write schedule, so it is not a valid fallback.

### 7.3 Frame bootstrap and wrap

The generated program is a continuous display loop. Cold startup and every
steady-state frame must enter logical line 0 with the same machine state and
cycle phase.

The generator contract is:

1. After cold startup, disable interrupts and OS display-list shadow activity
   before direct ANTIC setup.
2. During initialization or vertical blank, install the display-list pointer,
   DMA/PMG configuration, `CHACTL=0`, `PRIOR`, initial color/position
   registers, and `CHBASE` for charset 0.
3. Finish initialization by placing A, X, Y, and the tracked hardware
   registers in the exact canonical entry state used by the evaluator.
4. Synchronize with `VCOUNT` and a calibrated delay/WSYNC sequence so logical
   line 0 begins at the execution-window boundary: ANTIC cycle 106 of the
   scanline preceding the first LMS badline, represented by timing position
   `-8`.
5. Execute lines 0–239 and leave line 239 at the same cycle-105 boundary as
   every other raster line.
6. Enter the frame epilogue during vertical blank. Restore charset 0 and all
   canonical initial raster state before waiting for the next line-0 phase.
   In particular, line 215 leaves `CHBASE` on charset 9; it must not remain
   there for the next frame.

The evaluator models one canonical frame. It initializes A/X/Y and
`mem_regs_init` exactly as the generator does and does not carry the outgoing
state of line 239 directly into line 0. Fixed frame-reset code is outside the
optimized raster lines.

The ANTIC-4 generator may reuse the broad structure of the current mode-E
bootstrap, but its VCOUNT target and delay constants are not assumed correct.
Calibrate them against the ANTIC-4 display list and verify:

- cold startup to the first complete frame;
- line 239 through vertical blank to the next line 0;
- identical line-0 pixels and register timing on consecutive frames.

## 8. Evaluation and optimization

### 8.1 Rendering a pixel

The existing evaluator already advances raster instructions according to the
cycle-to-screen map and chooses a closest register at each pixel. In ANTIC 4
mode, change only the playfield candidate list:

1. read the attribute bit for `(x / 4, y / 8)`;
2. offer `COLBAK`, `COLPF0`, `COLPF1`, and the selected `COLPF2/COLPF3`;
3. apply existing PMG coverage and priority handling;
4. save the winning target and color as today.

The attribute is constant for the whole 4×8 cell. It must never be chosen
independently per pixel.

When a PMG register wins, use `COLBAK` as the underlying glyph target unless
the evaluator already retained a legal playfield winner. This matches the
current conservative MIC export behavior and avoids inventing a fifth glyph
value.

The optimizer does not mutate glyph bytes directly. With a private glyph per
cell, each final playfield target row maps deterministically to one two-bit
glyph byte. Searching both the target map and a duplicate font representation
would add state and cache invalidation without adding any image choices.
Generate or refresh `.a4.fnt` only when publishing/saving a selected picture.

### 8.2 Attribute mutation

Append one mutation type:

```text
Flip ANTIC 4 cell attribute
```

The mutation:

- chooses one of the 1,200 cells;
- toggles its packed bit;
- affects exactly the eight scanlines belonging to that character row;
- participates in normal accept/reject and mutation statistics;
- is disabled in mode E.

Initialize the attribute mask once when the initial `raster_picture` is built,
using the run's existing seeded RNG. Worker/island copies inherit that same
picture; do not add a separate per-island initialization path. Use all-zero
attributes for deterministic unit and bridge tests. A smarter initializer can
be added later if measurements show it is worthwhile.

The initial implementation should use a fixed, modest mutation probability.
Do not add adaptive cell-block mutation strategies until single-bit mutation
quality and cost have been measured.

### 8.3 Color-register mutations

`COLPF3` must be available to initial color selection and raster register-write
mutations in ANTIC 4 mode. It must not be selected in mode E.

Helper-based target lists are required here because `E_COLOR3` is appended
rather than numerically adjacent to the existing playfield targets.

### 8.4 Line-type legality for mutations

Every mutation that changes instruction ownership must validate the destination
line's `RasterLineSchedule`. Unequal ANTIC-4 budgets make the current global
limit unsafe.

Required rules:

```text
add:
    current_cycles + new_instruction_cycles <= current_limit

push to previous:
    previous_cycles + moved_instruction_cycles <= previous_limit

copy source to destination:
    source_cycles <= destination_limit

swap A and B:
    A_cycles <= B_limit AND B_cycles <= A_limit
```

This applies to the current copy/push/swap mutations, mutation chains,
initializers, raster-program loading, optimization passes, and structured-solver
insertions. A line numbered 23, 47, ..., 215 uses the 48-cycle switching limit,
not the ordinary 54-cycle continuation limit.

Reject an illegal mutation without modifying the candidate. Do not silently
trim instructions: trimming changes instruction order, register state, and the
meaning of the proposed mutation. After every mutation transaction, validate
all touched lines against their destination schedules in debug/test builds.
The assembly emitter must perform the same validation as a final release-build
guard.

### 8.5 Caching policy

Correctness milestone:

- allow ANTIC 4 to run with line caching disabled;
- keep instruction-sequence caching only if it is independent of rendered
  targets;
- obtain exact end-to-end output before optimizing cache behavior.

First cache-enabled milestone:

- add the current 40-bit attribute-row mask to `line_cache_key`;
- use the mask for `y / 8`;
- include it in equality and hashing;
- flipping one cell naturally causes misses on those eight lines;
- downstream lines may hit again because the attribute does not alter CPU
  register state.

This is preferable to global cache invalidation. It also avoids putting all
1,200 attributes in every line key.

Any candidate-copy or undo optimization must copy or restore the changed
64-bit attribute row. It need not copy all 30 rows for a one-cell mutation.

## 9. Output encoding

### 9.1 Files

In ANTIC 4 mode save:

```text
<output>.rp / <output>.opt       raster programs, as today
<output>.pmg                     player data, as today
<output>.a4.scr                  1,200-byte mode-4 screen
<output>.a4.fnt                  ten consecutive 1 KiB character sets
<output>.png                     preview, as today
<output>.optstate                optimizer history plus optional ANTIC-4 state
```

Do not emit `<output>.mic` in ANTIC 4 mode; it would imply mode-E bitmap data.

The current `.optstate` format stores optimizer history, not the candidate
picture. Extend it in a backward-compatible way by appending a tagged block
after the existing history values:

```text
ANTIC4_STATE 1
graphics_mode antic4
attribute_rows 30
<30 hexadecimal 40-bit masks, one per line>
```

This requires passing the saved `raster_picture` to `SaveOptimizerState`.
Loading rules are:

- end-of-file after the legacy history is a valid old mode-E state;
- ANTIC 4 resume requires the tagged block and exactly 30 valid 40-bit masks;
- a run configured for mode E rejects an `antic4` tagged block;
- unknown block versions fail with a clear error.

The bit 7 values in `.a4.scr` must agree with the saved masks, but `.a4.scr` is
an output artifact, not the source of resume state.

### 9.2 Glyph generation

For every output pixel whose selected target is a playfield register, encode:

```text
COLBAK -> 00
COLPF0 -> 01
COLPF1 -> 10
COLPF2 -> 11, requiring cell attribute 0
COLPF3 -> 11, requiring cell attribute 1
```

PMG-selected pixels encode the chosen underlying legal playfield value, or
`COLBAK` when no underlying value was retained.

Assert during export that no cell contains a `COLPF2` target when its attribute
selects `COLPF3`, or vice versa. Such a mismatch is an evaluator bug and must
not be silently rewritten.

### 9.3 Display list and memory placement

Use:

- one `$44` mode-4 instruction with LMS;
- 29 further `$04` mode-4 row instructions;
- one `$41` JVB instruction.

Place the 1,200-byte screen so it does not cross ANTIC's 4 KiB playfield
address wrap; 4 KiB alignment is the simplest rule. Align every character set
to 1 KiB. The assembly template may place the ten sets consecutively when the
first is 1 KiB-aligned. Keep the 35-byte display list within one 1 KiB
display-list page; aligning its start is the simplest generator rule.

The generated program must explicitly initialize `CHACTL=0`, the first
`CHBASE`, normal-width single-line PMG DMA, and the same `PRIOR` value modeled
by the evaluator. `CHACTL` is not reliably cleared by a warm reset, and bit 2
would vertically reflect the glyph rows. As in the existing generator, disable
OS display-list shadow interference before taking direct control of ANTIC.

Add a small ANTIC-4-specific generator template instead of filling the current
mode-E template with conditionals. Shared macros may be factored later if the
duplication becomes troublesome.

## 10. Differential validation

AltirraBridge is the display oracle. The existing PoC already randomizes:

- screen codes and bit-7 attributes;
- character data;
- four player bitmaps at fixed, non-overlapping positions;
- initial playfield and player colors;
- WSYNC-delimited, per-line color-register writes.

The PoC deliberately forces background playfield data under the players and
uses `PRIOR=0`. It does not validate the generated program's current priority
setting, player overlap/priority, `CHBASE` switching, moving players, or
RastaConverter's PMG fixed-point selection. Those require integrated test
cases using the exact generated hardware configuration.

It crops AltirraBridge's 336×240 RAWSCREEN to the 320×240 normal playfield and
compares every cropped pixel exactly against its local renderer. Preserve it as
a developer test rather than merging its Python renderer into production code.

Before using the PoC as an acceptance gate, make its frame phase explicit:

1. call `BOOT_BARE`, then pause the simulator;
2. install all RAM and hardware state while paused;
3. make the test kernel restore a canonical register/`CHBASE` state on every
   frame, rather than relying on colors left by the preceding frame;
4. add a RAM completion counter or marker written only after line 239;
5. run enough gated frames to obtain at least two completed canonical frames;
6. issue a read/ping after `FRAME`, validate the completion marker, then capture
   RAWSCREEN.

The bridge protocol already makes the command following `FRAME` wait for the
frame gate, but the explicit pause/setup/marker sequence also proves that the
captured frame is complete and canonical. A transient mismatch is a test
failure. Do not hide it with automatic retries; retain its artifacts and frame
marker state.

Before considering the integrated implementation correct, extend or adapt the
test so one deterministic case:

1. is rendered and exported by the C++ evaluator;
2. is run from those exported artifacts in AltirraBridge;
3. is checked by an exact evaluator-preview versus RAWSCREEN comparison.

Add a dedicated CHBASE case with sharply different adjacent character sets.
For every transition, compare the last scanline using the old charset and the
first scanline using the new one. Also compare line 239 followed by line 0 of
the next frame to prove that the epilogue restored charset 0.

On failure, retain the random seed, screen, fonts, PMG, raster program, raw
screenshot, expected image, and first mismatch position. The current PoC
failure bundle is the model.

## 11. Implementation sequence

### Milestone 1: mode and timing foundation

- add `GraphicsMode` and CLI validation;
- append `E_COLOR3` and remove affected contiguous-range assumptions;
- add four immutable DMA profiles plus the per-line schedule overlay;
- replace global mutation/validation limits with destination schedules;
- add full slot-array and instruction-completion timing tests;
- keep mode-E results byte-for-byte unchanged.

### Milestone 2: uncached ANTIC 4 evaluator

- add packed attributes to `raster_picture`;
- render with the legal four-color set for each cell;
- support `COLPF3` raster writes;
- initially disable line-result caching in ANTIC 4 mode;
- compare randomized evaluator output with the PoC/AltirraBridge.

### Milestone 3: output and runnable program

- write `.a4.scr` and `.a4.fnt`;
- add the ANTIC 4 display-list/generator template;
- implement and verify cold-start and steady-state frame bootstrap;
- schedule and bridge-verify every `CHBASE` change and the frame reset to set 0;
- produce a runnable 160×240 program;
- require exact AltirraBridge screenshots for deterministic test cases.

At this milestone the hardware path is proven end to end, even though the
ANTIC-4-specific attribute has not yet been made an optimizer mutation.

### Milestone 4: optimize the new degree of freedom

- add the cell-attribute mutation;
- persist attributes in `.optstate`;
- include the attribute row in line-cache keys;
- restore line caching;
- measure cache hit rate, evaluations per second, and image quality.

Only after measurements should more elaborate cell mutations or special cache
schemes be considered.

## 12. Required tests

### Unit tests

- all four glyph values with attribute clear and set;
- screen/glyph packing at character-set boundaries;
- correct glyph indices for all 30 character rows;
- `E_COLOR3` register name and serialization;
- mode-E and all three ANTIC-4 cycle counts;
- legal instruction limits for all four ANTIC-4 scheduling cases;
- copy, push, swap, add, load, and solver insertion reject destination-budget
  violations without trimming;
- attribute flip changes only the expected 4×8 cell constraints;
- line-cache keys differ when their 40-bit attribute row differs;
- a PMG fixed-point replay restores the correct midline `COLPF3` value;
- a switching-line fixed event produces the same outgoing A before and after
  save/resume;
- ANTIC-4 export rejects an illegal PF2/PF3 target/attribute combination.

### Timing-model tests

- store the complete expected stolen/available-cycle arrays for the LMS
  badline, ordinary badline, and continuation profile;
- compare every entry, not only the 25/27/60 totals;
- verify that logical execution starts with preceding-line cycles 106–113 and
  continues through current-line cycle 105;
- verify instruction completion and GTIA write positions for representative
  two- and four-cycle sequences before, within, and after visible pixels;
- assert character-name cycles 18–96, character-data cycles 21–99, badline
  refresh at 98, and continuation refresh at 26–58 every four cycles;
- assert the switching-line `STA CHBASE` write at cycle 98, effective update at
  cycle 100, and total line completion at cycle 105.

### Regression tests

- existing mode-E fixture outputs remain unchanged;
- mode-E `.rp.ini` does not gain a `COLOR3` initialization;
- mode-E performance does not take the ANTIC-4 uncached path;
- save/resume preserves graphics mode and all attributes;
- cold startup and steady-state frame entry produce the same evaluator state;
- unsupported dual/size combinations report an error.

### Emulator tests

- deterministic simple patterns for each playfield register;
- PF2/PF3 split patterns proving bit-7 behavior;
- color writes before, during, and after the visible region;
- first LMS badline, ordinary badlines, and continuation lines;
- every `CHBASE` transition with distinguishable old/new fonts;
- line 239 to next-frame line 0, including restoration of charset 0;
- completion-marker validation before every accepted RAWSCREEN;
- randomized screen, fonts, raster program, and four players;
- exact full-frame RAWSCREEN equality for a fixed seed suite.

The acceptance condition is zero pixel mismatches. A “visually similar”
screenshot is not sufficient for validating timing.

## 13. Risks and pragmatic responses

### Uneven CPU budget

Badlines have much less raster time than mode E. The optimizer will have fewer
color changes on every eighth line. This is inherent to the mode and should be
represented honestly, not smoothed into an average budget.

### Attribute search cost

One bit affects 32 pixels on eight lines and can reduce cache reuse. Start with
single-cell mutations and the 40-bit row cache key. Optimize only after
profiling.

### Character-set switching

An incorrectly timed `CHBASE` write can corrupt part of a row. Make every
transition an emulator test. If the reserved cycle-98 write does not pass,
correct the scheduler or timing model; increasing the number of character sets
does not solve the unsafe-write problem.

### Memory use

The straightforward layout costs 10 KiB of font data plus 1,200 bytes of
screen data. This is acceptable for the first version and substantially simpler
than character deduplication.

### Existing enum assumptions

Appending `E_COLOR3` preserves serialized target numbers but exposes old range
assumptions. Central target-list helpers and focused tests are required before
enabling ANTIC 4.

## 14. Definition of done

ANTIC 4 is ready for normal experimental use when:

- `/graphics_mode antic4` completes a 160×240 single-frame conversion;
- it emits runnable screen, font, PMG, and raster-program artifacts;
- the generated program displays exactly as the internal evaluator predicts;
- cold startup and consecutive steady-state frames have identical line-0 phase
  and canonical register state;
- every CHBASE transition, including frame-wrap restoration to charset 0, is
  bridge-verified;
- deterministic fixed and randomized AltirraBridge cases have zero pixel
  mismatches without retries;
- cell attributes are optimized and survive save/resume;
- mode-E tests and output remain unchanged;
- timing and cache statistics identify badlines separately so performance can
  be evaluated honestly.

The first release does not need to outperform mode E. It needs to prove that
the additional cell-selectable color produces valid hardware output and can be
searched at a practical speed.
