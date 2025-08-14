* [What the converter does?](#what-does)
* [Why the converter works so slowly?](#slow)
* [But why the converter works so slow, if it works so fast?](#slow2)
* [Why the converter generates different output each time?](#different-output)
* [Why the converter is not able to convert even simple pictures from other 8 bit computers?](#simple-pics)
* [But the picture I have can be converted 1 to 1!](#1to1)
* [But the picture I have is low-color and trivial. The converter should give a results in seconds!](#low-color)
* [Why don't you use some other algorithm like genetic algorithm?](#genetic)
* [Can I use multiple threads for faster conversion?](#multithreading)
* [Can GPU acceleration (CUDA, OpenCL) be used for faster conversion?](#gpu-acceleration)
* [What is dual-frame mode and why should I use it?](#dual-mode)
* [What optimization algorithms are available?](#algorithms)
* [Why the converter does not use character mode to get one extra color? Or interlace? Or hires?](#charmode)
* [What are the -src , -dst and output pictures?](#src-dst)
* [If the best solution can't ever be reached then how long should I wait?](#how-long)
* [Why some blue colors get violet during conversion?](#blue)
* [Why the knoll dithering is so slow?](#knoll)
* [Why to use more solutions with /s parameter? How many should I use?](#solutions)
* [Why the converter produces outputs with banding and visible horizontal lines?](#banding)
* [How many different colors in line can we have?](#col_no)
* [How to report bugs?](#reporting)

<a name="what-does"></a>
## What the converter does?
The converter tries to solve some complex optimization problem: to write a [Kernel Program](http://www.atariarchives.org/dere/chapt05.php#H5_7) that executed by [6502 CPU] (http://en.wikipedia.org/wiki/MOS_Technology_6502) and [ANTIC](http://en.wikipedia.org/wiki/ANTIC) will show the best picture possible on [8bit Atari computer](http://en.wikipedia.org/wiki/Atari_8-bit_family).

<a name="slow"></a>
## Why the converter works so slowly?

Well... It does not work slowly. It works fast considering the problem complexity. Straight brute-force algorithm to find the optimal solution would require to test about [805^4080](http://www.atariage.com/forums/topic/156160-quantizator/page__st__575#entry2548993) combinations. 
The code is already quite optimized. The current version works ~1000 times faster than the first version.

<a name="slow2"></a>
## But why the converter works so slow, if it works so fast?

Because the problem is not trivial. All proposed and tested simple ideas (like assign the most common colors to playfield colors and the rest to the sprites) give very bad results comparing to what can be achieved. Those simple ideas are already implemented in the converter as initialization and optimization heuristics.

To understand the problem complexity you need to understand how the 8Bit Atari works, how it creates the screen, how many cycles are used by 6502, how many color and general purpose registers are available, how the sprites priority works, how the sprites multiplication works, how many color cycles ANTIC steals etc.

The problem is probably as hard as some [NP-complete]( http://en.wikipedia.org/wiki/NP-complete) problems and the size of the problem is big here.

<a name="different-output"></a>
## Why the converter generates different output each time?

The converter uses advanced optimization algorithms that are nondeterministic and do not guarantee to find the optimal solution. Therefore it is better to run it a few times and to choose the output you like best.

**Primary Algorithm: DLAS (Diversified Late Acceptance Search)**
- Based on the [Diversified Late Acceptance Search](https://doi.org/10.1007/978-3-030-03991-2_29) algorithm
- Provides fast convergence and good exploration of the search space
- Uses history length controlled by the `/s` parameter

**Alternative Algorithm: LAHC (Late Acceptance Hill Climbing)**
- Based on the [Late Acceptance Hill Climbing](https://www.sciencedirect.com/science/article/abs/pii/S0377221716305495) algorithm
- Better for long runs and plateaus
- Can find improvements when DLAS gets stuck

**Why nondeterministic?**
The [search space](http://en.wikipedia.org/wiki/Candidate_solution) is huge and in most cases it is not possible to tell in reasonable time what is the best solution. The algorithms search only through some part of the search space and each time you run them, they try different areas. This nondeterministic nature actually helps avoid getting stuck in local optima.

<a name="simple-pics"></a>
## Why the converter is not able to convert even simple pictures from other 8 bit computers?

All 8 bit platforms had different architectures and different graphics capabilities. What is easy to show on one computer can be not possible to show on the Atari.

<a name="1to1"></a>
## But the picture I have can be converted 1 to 1!

Are you really, really sure that this is possible? Even simple pictures can have color settings in lines that are not possible to be displayed on the Atari.

<a name="low-color"></a>
## But the picture I have is low-color and trivial. The converter should give a results in seconds!

Low amount of colors does not mean that it is trivial to set them. The CPU and ANTIC timing is complex and there are only 3 CPU registers the can be used to set sprite and color registers. Are you really sure that conversion is trivial or even possible? If yes, then I have a bad news: RastaConverter works in a nondeterministic way that was explained before. It is not created to [brute-force](http://en.wikipedia.org/wiki/Brute-force_search) simple pictures, because they rarely happen in reality and "so simple pictures" are usually not that simple. There is however a special case of pictures with less than 5 colors handled and the output should be given instantly.

<a name="genetic"></a>
## Why don't you use some other algorithm like [genetic algorithm](http://en.wikipedia.org/wiki/Genetic_algorithm)?

I've tested some other optimization techniques (Tabu Search, Genetic Algorithm, Beam Search, LAHC) and found that each has different strengths. DLAS (Diversified Late Acceptance Search) is generally more effective for quicker conversions, while LAHC (Late Acceptance Hill Climbing) can outperform DLAS in very long runs with long history parameters.

<a name="multithreading"></a>
## Can I use multiple threads for faster conversion?

Yes! RastaConverter supports multithreading with the `/threads` parameter. The converter automatically manages worker threads for optimal performance.

**Performance tips:**
- Use `/threads=N` where N is the number of CPU cores
- Set threads to the number of physical cores (or 1-2 less) to keep system responsive
- Increase cache size per thread (e.g., `/cache=16` for 16MB per thread)
- Regional mutation strategy may be better with 8+ threads to reduce mutex contention

<a name="gpu-acceleration"></a>
## Can GPU acceleration (CUDA, OpenCL) be used for faster conversion?

GPU acceleration is not implemented in RastaConverter. The current caching approach is more effective for optimizing the conversion process.

**Why GPU acceleration would be limited:**

The core optimization process involves emulating raster programs for the 6502 CPU, which has characteristics that don't map well to GPU architectures:

* **Heavy branching and conditional logic** - Raster program execution involves many conditional branches, loops, and state-dependent operations that GPUs handle poorly
* **Iterative execution** - The emulation runs thousands of iterations with complex interdependencies between steps
* **Memory access patterns** - GPU memory access patterns are optimized for regular, parallel operations, not the irregular access patterns of CPU emulation
* **State management** - Maintaining CPU register states, sprite positions, and timing across many cycles is inherently sequential

**What could potentially use GPU acceleration:**

Only a small subset of operations could benefit from GPU acceleration:
* Per-pixel color distance calculations
* Simple image preprocessing operations
* Basic dithering algorithms

**Why caching is more effective:**

RastaConverter uses sophisticated caching strategies that provide better performance gains:
* **Line instruction caching** - Avoids recomputing identical raster instructions
* **Memory allocation optimization** - Efficient memory management for large numbers of evaluations
* **Result caching** - Stores and reuses evaluation results to avoid redundant computation
* **Thread-local caches** - Minimizes memory contention in multithreaded scenarios

**Conclusion:**

While GPU acceleration could provide some benefit for specific pixel-level operations, the core optimization algorithm's iterative, branching nature means the performance gain would be negligible compared to the current CPU-based approach with intelligent caching. The development effort required to implement GPU acceleration would not justify the minimal performance improvement.

<a name="dual-mode"></a>
## What is dual-frame mode and why should I use it?

Dual-frame mode (`/dual`) creates two alternating frames (A and B) that, when displayed on a CRT, create the illusion of more colors through temporal dithering. This effectively expands the available color palette beyond the 128 Atari colors.

**Key benefits:**
- More color variety in output
- Better color accuracy for complex images
- Configurable flicker control
- Can achieve color combinations impossible in single-frame mode

**Flicker control:**
- `/flicker_luma=0..1` - How much luminance flicker you accept (0=no, 1=full)
- `/flicker_chroma=0..1` - How much chroma flicker you accept (0=no, 1=full)
- Perceived flicker from luma is stronger than from chroma, so they are controlled separately

**Strategies:**
- `/dual_strategy=staged` (default) - Focus on one frame for many iterations, then switch
- `/dual_strategy=alternate` - Choose A or B each step using `/dual_mutate_ratio`
- `/dual_init=dup|random|anti` - How frame B relates to frame A initially

**Outputs:**
- Separate files for both frames (OUT-A.rp, OUT-B.rp, etc.)
- Blended preview showing the perceived result
- Flicker heatmap for diagnostics

<a name="algorithms"></a>
## What optimization algorithms are available?

RastaConverter supports multiple optimization algorithms:

**DLAS (Diversified Late Acceptance Search) - Default**
- Based on the [Diversified Late Acceptance Search](https://doi.org/10.1007/978-3-030-03991-2_29) algorithm
- Fast convergence and good exploration
- Excellent for most use cases and quicker conversions
- Uses history length controlled by `/s` parameter
- Generally moves fast early in the optimization process

**LAHC (Late Acceptance Hill Climbing)**
- Based on the [Late Acceptance Hill Climbing](https://www.sciencedirect.com/science/article/abs/pii/S0377221716305495) algorithm
- Better for long runs and plateaus
- Can find improvements when DLAS gets stuck
- Good for marathon runs where you want maximum quality
- Can outperform DLAS in very long runs with long history parameters

**Algorithm selection:**
- Use `/optimizer=dlas` (default) for most runs and quicker conversions
- Try `/optimizer=lahc` on marathon runs, when DLAS plateaus, or when you want maximum quality
- The `/s` parameter controls history length (3-10 are reasonable for DLAS, higher values may be needed for LAHC)

<a name="charmode"></a>
## Why the converter does not use character mode to get one extra color? Or interlace? Or hires?

Because it is not implemented. Feel free to contribute, but you should know in advance that proper implementation will not be easy.

<a name="src-dst"></a>
## What are the -src , -dst and output pictures?

* The –src picture is the input picture resized to 160*height pixels using the chosen resize filter (`/filter` parameter).
* The –dst picture is the picture in Atari palette with dithering applied. This is the goal for the optimization process and this goal in most of the cases can never be reached! If it would be known what is the best possible output then the whole optimization would not be needed.
* The output picture is the picture telling how the screen will look like when executed on the 8bit Atari.

<a name="how-long"></a>
## If the best solution can't ever be reached then how long should I wait?

As long as you want :-) I usually stop the conversion when I don't see any visible improvements for a longer time. Sometimes it takes 2 million iterations, sometimes even 500 million – depending on a picture.

**Tips for determining when to stop:**
- Watch for plateaus in the improvement graph
- Use dual-frame mode for better color variety
- Try different algorithms if one gets stuck
- Save intermediate results with `/save=auto` (default)

<a name="blue"></a>
## Why some blue colors get violet during conversion?

Example

<table>
    <tr>
        <td>Input</td><td>Destination picture</td>
    </tr>
    <tr>
        <td>
        <img src="https://raw.github.com/ilmenit/RastaConverter/master/wiki/zx.PNG"/>
        </td>
        <td>
        <img src="https://raw.github.com/ilmenit/RastaConverter/master/wiki/zx-ciede.png"/>
        </td>
    </tr>
</table>

This is because of the default [color distance](http://en.wikipedia.org/wiki/Color_difference) metric CIEDE2000. It is chosen to be default, because Atari palette does not have linear spread over color space and with other distance metrics (YUV or Euclidian) too many pictures get converted to gray.
If your picture gets violet instead of blue then use a different color distance for the preprocess f.e. `/predistance=yuv`:

![zx-yuv](https://raw.github.com/ilmenit/RastaConverter/master/wiki/zx-yuv.png)

Basically if a picture is high-contrast one or colors are well saturated, then use `/predistance=yuv`.

Look how the colors get converted to Atari palette with different color distance metrics:

Original file
![Original](https://raw.github.com/ilmenit/RastaConverter/master/wiki/col.png)

CIEDE2000:
![CIEDE2000](https://raw.github.com/ilmenit/RastaConverter/master/wiki/col-ciede.png)

Euclidian RGB:
![Euclid](https://raw.github.com/ilmenit/RastaConverter/master/wiki/col-euclid.png)

YUV:
![YUV](https://raw.github.com/ilmenit/RastaConverter/master/wiki/col-yuv.png)

CIE94:
![CIE94](https://raw.github.com/ilmenit/RastaConverter/master/wiki/col-cie94.png)

<a name="knoll"></a>
## Why the [knoll dithering](http://bisqwit.iki.fi/story/howto/dither/jy/#PatternDitheringThePatentedAlgorithmUsedInAdobePhotoshop) is so slow?

It is slow, because the algorithm is slow :-) It gets much slower with CIEDE2000 distance metric, which is [complex to calculate]( http://en.wikipedia.org/wiki/Color_difference#CIEDE2000).

**Performance tips:**
- Use `/predistance=yuv` instead of CIEDE2000 for faster knoll dithering
- Knoll dithering now supports multithreading for better performance
- Consider simpler dithering algorithms like Floyd-Steinberg for faster preprocessing

<a name="solutions"></a>
## Why to use more solutions with /s parameter? How many should I use?

With `/s=1` (default) the algorithm used is [Hill Climbing](http://en.wikipedia.org/wiki/Hill_climbing), which provides result fast, but the result is usually far from optimal. With `/s` greater than 1, the Diversified Late Acceptance Search is used which is less prone to stuck in [local maximum](http://en.wikipedia.org/wiki/Maxima_and_minima).

The DLAS algorithm is much more efficient than previous methods, so you typically need much smaller values - `/s=3` to `/s=5` often produces excellent results. You can use larger values (`/s=10`), but the improvement diminishes quickly with DLAS.

**Recommendations:**
- Start with `/s=3` for most images
- Use `/s=5` to `/s=10` for maximum quality
- Higher values may help with complex images but with diminishing returns

<a name="banding"></a>
## Why the converter produces outputs with banding and visible horizontal lines?
There are a few reasons for that:
* Amount of available colors to be used at one time is very low on Atari and colors cannot be set everywhere whey they are needed because of CPU and ANTIC timing. The converter sometimes "reserves" a color for further usage or places a sprites in previous line, where it is not really needed. All of this is kept when it may improve global picture quality.
* The sprites width used is quadruple to cover more area. RastaConverter does not support sprites with lower width. Because of sprites priority it is not always possible to cover part of a sprite with the best color.
* Best areas to cover with more details are not the same for a human eye and for a computer. Computer works on the color distance while humans prefer to see more details for example on faces, small but important objects, center of a picture, objects in the first plan etc. Use the `/details` mask to choose where do you want to have details.
* The algorithm stucked in [local optimum](http://en.wikipedia.org/wiki/Local_optimum) and is not able to find a better solution. [Rerun](#different-output) it again or try larger number of solutions with `/s` parameter.

**Additional solutions:**
* Use dual-frame mode (`/dual`) to reduce banding through temporal dithering
* Try different mutation strategies (`/mutation_strategy=regional` vs `global`)
* Increase the number of solutions with `/s` parameter
* Use different initialization strategies (`/init=smart` or `/init=less`)

<a name="col_no"></a>
## How many different colors in line can we have?

RastaConverter does midline changes of colour/sprite registers. You can have up to 7 changes in line. With initial set of colours (4 playfield, 4 players) you can have up to 15 colours in line. The number is not constant and depends on a picture (colors in previous and next lines, position of colors on the screen). Usually you can have 5 to 13 colours in line. Therefore reducing number of colors in lines f.e. to 8 would work only for some pictures, for others giving much worse output.

**Dual-frame mode considerations:**
* In dual-frame mode, the effective number of perceived colors can be much higher
* Temporal dithering between frames A and B creates additional color variety
* Flicker control parameters affect how many colors are effectively visible

<a name="reporting"></a>
## How to report bugs?

Attach all the input and output files to the bug report. Without that your report will be useless.

**Include in bug reports:**
* Input image file
* Command line parameters used
* Output files generated
* Error messages or unexpected behavior
* System information (OS, CPU, memory)
* Whether the issue occurs in single-frame or dual-frame mode