Rasta Converter
===============

RastaConverter is a graphics converter from modern computers to old [8bit Atari computers](http://en.wikipedia.org/wiki/Atari_8-bit_family).
The tool uses [SDL2](https://www.libsdl.org/) and [FreeImage](http://freeimage.sourceforge.net/) graphics libraries.

The conversion process is optimization of the [Kernel Program](http://www.atariarchives.org/dere/chapt05.php#H5_7).
It uses most of the Atari graphics capabilities including sprites, midline color changes and sprite multiplication. 

The converter supports two optimization algorithms:

- Diversified Late Acceptance Search (DLAS)
- Late Acceptance Hill Climbing (LAHC)

You can select the algorithm with `/optimizer=dlashc` (default) or `/optimizer=lahc`. Some users report LAHC can surpass DLAS on very long runs; DLAS is typically faster early on.

Dual-frame mode (CRT blending)
---------------------------------------
RastaConverter can optimize two frames A/B and alternate them each refresh to leverage CRT persistence. The perceived color approximates the average of A and B, effectively increasing the number of colors. Large luminance differences between A and B cause flicker; the optimization penalizes such differences via soft thresholds.

Enable with `/dual=on`. Defaults aim for good quality with low flicker while keeping evaluation fast by blending and measuring in YUV.

Key options:
 - Luma/chroma flicker control:
  - `/flicker_luma_weight` (default 1.0), `/flicker_luma_thresh` (default 3), `/flicker_exp_luma` (default 2)
  - `/flicker_chroma_weight` (default 0.2), `/flicker_chroma_thresh` (default 8), `/flicker_exp_chroma` (default 2)
 - Mutation ratio:
  - `/dual_mutate_ratio=0..1` (default 0.5)

GUI in dual mode:
- Center panel defaults to Blended A/B. Toggle view with A (frame A), Z (frame B), B (blended). The current mode is displayed under the preview.
- Press S to save at any time.

Outputs in dual mode:
- A artifacts: `<output>-A.rp`, `<output>-A.mic`, `<output>-A.pmg`
- B artifacts: `<output>-B.rp`, `<output>-B.mic`, `<output>-B.pmg`
- `<output>-blended.png` preview and `<output>-flicker.png` luma-difference heatmap
- Stats/state: `<output>-dual.csv`, `<output>-dual.lahc`
Use a display program that alternates A and B each frame on real hardware.

Screenshot
----------
![Rasta Converter screenshot](https://github.com/ilmenit/RastaConverter/raw/master/examples/screenshot.png "Rasta Converter screenshot")

Examples
--------
![Example1](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-autumn-new-output.png)
![Example2](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-snow_woods.xex-output.png)
![Example3](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-fairey_wood.xex-output.png)
![Example4](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-landscape.xex-output.png)

Atari executables for those and many other pictures can be downloaded [here](https://github.com/ilmenit/RastaConverter/blob/master/examples/atari-executables.zip?raw=true).

