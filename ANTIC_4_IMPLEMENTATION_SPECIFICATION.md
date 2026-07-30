# ANTIC 4 Implementation Reference

Status: implemented for RastaConverter 1.0-RC3

This document records the hardware and file-format contract implemented by the
ANTIC 4 path. It is a reference for maintainers and emulator validation; user
instructions are in [README.md](README.md).

## Supported configuration

- Select with `/graphics_mode=antic4` or **ANTIC 4 (text mode)** in the Live UI.
- ANTIC E remains the default and follows its existing code path.
- The target is 168 Atari colour clocks wide: 42 visible 4x8 cells in ANTIC's
  wide playfield. Saved previews are doubled horizontally to 336 square pixels.
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
them at a time. The optimizer stores one 42-bit attribute mask per character
row and mutates individual cell choices.

## Screen and character-set layout

ANTIC fetches 48 screen bytes per wide row. The exported `.a4.scr` row contains:

```text
3 hidden bytes | 42 visible cells | 3 hidden bytes
```

One LMS at the first row is sufficient because the maximum 30 rows occupy only
1,440 bytes and do not cross ANTIC's 4 KiB playfield wrap.

Every visible cell receives a private glyph. Three 42-cell rows fit in one
128-glyph, 1 KiB character set:

```text
row 0 in group -> glyphs   0..41
row 1 in group -> glyphs  42..83
row 2 in group -> glyphs  84..125
glyphs 126..127 -> unused
```

For visible cell `x`, character row `r`, and scanline `y`:

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

The generated display enables wide ANTIC mode 4, one LMS, single-line
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

The playfield starts at ANTIC cycle 22. A slot maps to evaluator colour-clock
offset `(slot - 22) * 2`. A four-cycle store takes effect on its fourth CPU
slot; the evaluator resolves that completion through the selected slot map.
ANTIC E retains its historical timing map and calibrated write behavior.

| Line kind | Optimizer cycles | Fixed suffix | Total slots |
|---|---:|---:|---:|
| first LMS badline | 6 | 5 | 11 |
| ordinary badline | 8 | 5 | 13 |
| continuation after badline | 48 | 4 | 52 |
| ordinary continuation | 48 | 5 | 53 |
| CHBASE transition | 44 | 9 | 53 |

The transition suffix is `BIT zp` + `LDA #` + `STA CHBASE`. The store completes
on ANTIC cycle 104; the old character data is fetched at 105 and the queued
CHBASE value is active before the next line fetch.

## Saved state and generated files

`E_COLOR3` is appended to the target enum so existing serialized target IDs do
not change. Mode-E serializers omit it. ANTIC 4 raster pictures carry the
graphics-mode marker and one low-42-bit attribute mask per character row.
Attributes participate in candidate copying, validation, cache keys,
publication, optimizer state, resume, and final export.

An ANTIC 4 save produces:

- `.rp` and `.opt` raster programs with `Graphics Mode: ANTIC 4` metadata;
- `.optstate`, including the graphics mode and cell attributes;
- `.pmg` player/missile data;
- `.a4.scr`, containing 48 bytes per character row;
- `.a4.fnt`, containing the required 1 KiB character sets;
- the usual preview, source, destination, CSV, and metadata files.

`Generator/antic4.asq` builds the executable. It installs a mode-4 display
list, initializes CHACTL, CHBASE, wide DMA, PMG state and colour registers,
includes the exported data, restores charset zero each frame, and asserts that
screen, display list, PMG, raster code, fonts, and hardware space do not
overlap. The Recent view selects this template automatically when both ANTIC 4
data files are present.

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
- no arbitrary, narrow, or normal-width mode-4 playfield;
- no mid-line player movement;
- no glyph deduplication or character-set compression;
- no new PRIOR, fifth-player, overlap, or missile behavior beyond the existing
  evaluator model.

Unsupported combinations should fail clearly rather than silently falling back
to ANTIC E.
