Rasta Converter
===============

RastaConverter is a graphics converter from modern computers to old [8bit Atari computers](http://en.wikipedia.org/wiki/Atari_8-bit_family).
The tool uses [SDL2](https://www.libsdl.org/) and [FreeImage](http://freeimage.sourceforge.net/) graphics libraries.

Building
--------

Use CMake presets for a simple cross-platform build:

```
cmake --preset win-clangcl
cmake --build --preset win-clangcl-release
```

See `BUILD.md` for detailed instructions and alternative compilers/platforms.

The conversion process is optimization of the [Kernel Program](http://www.atariarchives.org/dere/chapt05.php#H5_7).
It uses most of the Atari graphics capabilities including sprites, midline color changes and sprite multiplication.


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

