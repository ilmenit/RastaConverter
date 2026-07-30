# ANTIC 4 Implementation Reference

Status: implemented for RastaConverter 1.0-RC3

This document records the hardware and file-format contract implemented by the
ANTIC 4 path. It is a reference for maintainers and emulator validation; user
instructions are in [README.md](README.md).

## Supported configuration

- Select with `/graphics_mode=antic4` or **ANTIC 4 (text mode)** in the Live UI.
- ANTIC E remains the default and follows its existing code path.
- `/playfield=normal` is the default: 160 colour clocks and 40 cells. P2/P3 are
  reserved for the black side masks, leaving P0/P1 available to the converter.
- `/playfield=wide` selects 168 visible colour clocks and 42 cells in the
  center of the 48-byte DMA window.
- Heights are complete character rows: 8–240 scanlines in multiples of eight.
  Automatic and explicit heights are rounded to the nearest legal row.
- ANTIC 4 is single-frame only. `/graphics_mode=antic4 /dual` is rejected, and
  the UI disables dual-frame controls while ANTIC 4 is selected.
- Player/missile graphics and raster colour writes are supported. Player
  positions are fixed for the frame; mid-line HPOSP writes are excluded because
  the evaluator does not model GTIA's delayed position latch and shifter state.

## Pixel and cell model

Each screen byte selects one 4x8 glyph:

- bits 0–6 select one of 128 glyphs;
- bit 7 changes the meaning of two-bit glyph value `11`;
- glyph values are decoded most-significant pair first.

| Glyph value | Bit 7 clear | Bit 7 set |
|---|---|---|
| `00` | COLBK | COLBK |
| `01` | COLPF0 | COLPF0 |
| `10` | COLPF1 | COLPF1 |
| `11` | COLPF2 | COLPF3 |

Bit 7 is therefore a per-cell fourth-colour selector, not general inverse
video. Five playfield registers exist globally, while one cell can use four of
them at a time. The optimizer stores one 40- or 42-bit attribute mask per
character row and mutates individual cell choices.

## Screen and character-set layout

The exported `.a4.scr` row is one of:

```text
normal: 40 visible cells
wide:    3 hidden bytes | 42 visible cells | 3 hidden bytes
```

One LMS at the first row is sufficient because neither layout crosses ANTIC's
4 KiB playfield wrap.

Every visible cell receives a private glyph. Three rows fit in one 128-glyph,
1 KiB character set. Normal rows use glyph ranges 0–39, 40–79, and 80–119;
wide rows use:

```text
row 0 in group -> glyphs   0..41
row 1 in group -> glyphs  42..83
row 2 in group -> glyphs  84..125
glyphs 126..127 -> unused
```

For a wide-playfield visible cell `x`, character row `r`, and scanline `y`:

```text
charset       = r / 3
glyph         = (r % 3) * 42 + x
screen offset = r * 48 + 3 + x
screen byte   = glyph | (attribute[r,x] ? $80 : $00)
font offset   = charset * 1024 + glyph * 8 + (y % 8)
```

Up to ten consecutive 1 KiB character sets are emitted in `.a4.fnt`. CHBASE is
changed after scanlines 23, 47, and so on when another character-set group is
needed. The final line never switches to an unused set.

## DMA and raster timing

The generated display enables normal or wide ANTIC mode 4, one LMS, single-line
player/missile DMA, quadruple-width players, and no horizontal scrolling.
Raster code runs continuously without per-line WSYNC, so each line body plus
its fixed suffix must consume every available logical CPU slot.

The canonical logical slot maps start at ANTIC cycle 106 of the preceding
scanline (`-8`) and end before cycle 106 of the current scanline:

```text
LMS badline (11):
  -8,-7,-6,-5,-4,-3,-2,-1,8,9,11

ordinary badline (13):
  -8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,11

continuation (53):
  -8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,
  14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
  72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104

continuation after a badline (52):
  -7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,
  14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
  72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104
```

The line after a wide badline loses its `-8` slot because the badline's ninth
refresh request is deferred to ANTIC cycle 106.

Normal DMA begins eight ANTIC cycles later. Its exact slot maps are:

```text
LMS badline (25):
  -8,-7,-6,-5,-4,-3,-2,-1,8,9,10,11,12,13,14,15,16,17,19,
  100,101,102,103,104,105

ordinary badline (27):
  -8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,10,11,12,13,14,15,16,17,19,
  100,101,102,103,104,105

continuation (60):
  -8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,13,14,15,16,17,
  18,19,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
  72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,101,102,103,104,105
```

Normal DMA services refresh before cycle 106, so the line following a badline
uses the ordinary 60-slot continuation map.

The wide playfield starts at ANTIC cycle 22 and the normal playfield at cycle
24. A slot maps to evaluator colour-clock offset `(slot - displayStart) * 2`.
A four-cycle store takes effect on its fourth CPU slot; the evaluator resolves
that completion through the selected slot map. ANTIC E retains its calibrated
write behavior with separate normal and wide DMA maps.

| Line kind | Optimizer cycles | Fixed suffix | Total slots |
|---|---:|---:|---:|
| first LMS badline | 6 | 5 | 11 |
| ordinary badline | 8 | 5 | 13 |
| continuation after badline | 48 | 4 | 52 |
| ordinary continuation | 48 | 5 | 53 |
| CHBASE transition | 44 | 9 | 53 |

Normal-width budgets are 20+5 slots on the first badline, 22+5 on later
badlines, 56+4 on continuations, and 50+10 on CHBASE transitions.

The wide transition suffix is `BIT zp` + `LDA #` + `STA CHBASE`; normal uses
the one-cycle-longer `BIT abs`. The store completes on ANTIC cycle 104; the old
character data is fetched at 105 and the queued CHBASE value is active before
the next line fetch.

## Saved state and generated files

`E_COLOR3` is appended to the target enum so existing serialized target IDs do
not change. Mode-E serializers omit it. ANTIC 4 raster pictures carry the
graphics-mode and playfield-width markers and one low-40- or low-42-bit
attribute mask per character row.
Attributes participate in candidate copying, validation, cache keys,
publication, optimizer state, resume, and final export.

An ANTIC 4 save produces:

- `.rp` and `.opt` raster programs with `Graphics Mode: ANTIC 4` metadata;
- `.optstate`, including the graphics mode, playfield width, and cell
  attributes;
- `.pmg` player/missile data;
- `.a4.scr`, containing 40 bytes per normal row or 48 bytes per wide row;
- `.a4.fnt`, containing the required 1 KiB character sets;
- the usual preview, source, destination, CSV, and metadata files.

`Generator/antic4.asq` builds the executable. It installs a mode-4 display
list, initializes CHACTL, CHBASE, selected-width DMA, PMG state and colour
registers,
includes the exported data, restores charset zero each frame, and asserts that
screen, display list, PMG, raster code, fonts, and hardware space do not
overlap. The Recent view selects this template automatically when both ANTIC 4
data files are present.

For normal width, the generator reserves P2/P3 and uses them with M2/M3 as
four-colour-clock black masks at the left and right edges. `PRIOR=$1F` enables
fifth-player missiles and all four conflicting priority bits. The overlapping
player/missile signals consequently suppress every colour output, including
COLPF3, so the masks resolve to hardware black without consuming a palette
colour.

## Validation

The focused C++ tests cover:

- complete DMA slot fixtures and per-line budgets;
- instruction completion offsets and safe CHBASE timing;
- PF2/PF3 glyph encoding and cell-attribute validation;
- preservation of ANTIC E target rules and timing;
- attribute participation in line-cache keys;
- configuration defaults, GUI labels, command-line copying, height
  normalization, and dual-mode exclusion.

`tools/antic4_poc.py` is the hardware differential test. It generates random
screen codes, fonts, attributes, four player streams, initial colours, and a
full-budget continuous raster kernel. It restores a canonical frame state,
waits for completed frames, captures AltirraBridge's 336x240 RAWSCREEN, and
compares every pixel with the local model. A mismatch is a failure and retains
the seed and artifacts. The RC3 acceptance suite uses seeds 1, 19, 164, 0xA4,
and 999; all must report zero mismatches.

The release smoke procedure additionally converts one ANTIC E and one ANTIC 4
image, checks the expected output files and sizes, and assembles the ANTIC 4
artifacts with bundled MADS.

## Current limitations

- no ANTIC 4 dual-frame output;
- no arbitrary or narrow playfield width;
- no mid-line player movement;
- no glyph deduplication or character-set compression;
- normal ANTIC 4 reserves P2/P3 for border masking, leaving P0/P1 available to
  image conversion.

Unsupported combinations should fail clearly rather than silently falling back
to ANTIC E.
