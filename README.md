Rasta Converter
===============

RastaConverter is a graphics converter from modern computers to old [8bit Atari computers](http://en.wikipedia.org/wiki/Atari_8-bit_family).
The tool uses [SDL2](https://www.libsdl.org/) and [FreeImage](http://freeimage.sourceforge.net/) graphics libraries.

The conversion process is optimization of the [Kernel Program](http://www.atariarchives.org/dere/chapt05.php#H5_7).
It uses most of the Atari graphics capabilities including sprites, midline color changes and sprite multiplication. 

Key capabilities
----------------
- Extremely optimized emulator of subset of [6502 CPU](https://en.wikipedia.org/wiki/MOS_Technology_6502) and [ANTIC](https://en.wikipedia.org/wiki/ANTIC) to simulate execution on real machine.
- Optimization: Late Acceptance Hill Climbing (LAHC) and Diversified Late Acceptance Search (DLAS), with support for reproducible runs, evaluation limits, auto-save and resume.
- Dithering: chess, Floyd–Steinberg, random-Floyd, line, line2, 2D, Jarvis, simple, and Knoll; tunable strength and randomness.
- Color distance: YUV (default), RGB Euclidean, CIEDE2000, and CIE94; independently selectable for preprocessing and optimization.
- Dual-frame mode: two alternating frames (A/B) with YUV or RGB blending, optional temporal luma/chroma penalties to reduce flicker, and export of both per-frame and blended outputs.
- Performance: multi-threaded execution with per-thread line caches and configurable cache size.
- Image pipeline: resize filters (box, bilinear, bicubic, bspline, Catmull–Rom, Lanczos3) plus brightness/contrast/gamma adjustments.
- Hardware control: fine-grained control over Atari registers, including enabling/disabling hardware sprites (players/missiles) per scanline.
- Details mask: provide a mask image to emphasize selected regions and bring out fine details in the result.
- Interfaces: SDL2 GUI and headless console modes.
- Palette selection: choose target palette files via Adobe ACT to match different monitors and CRT settings.
- Cross-platform: CMake-based builds for Windows and Linux; 
- Extras: scripts and generators to assemble Atari executables.

The converter uses Late Acceptance Hill Climbing (LAHC) and [Diversified Late Acceptance Search](https://doi.org/10.1007/978-3-030-03991-2_29).

Screenshot
----------
![Rasta Converter screenshot](https://github.com/ilmenit/RastaConverter/raw/master/examples/screenshot.jpg "Rasta Converter screenshot")

Examples
--------
![Example1](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-autumn-new-output.png)
![Example2](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-snow_woods.xex-output.png)
![Example3](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-fairey_wood.xex-output.png)
![Example4](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-landscape.xex-output.png)

Atari executables for those and many other pictures can be downloaded [here](https://github.com/ilmenit/RastaConverter/blob/master/examples/atari-executables.zip?raw=true).

