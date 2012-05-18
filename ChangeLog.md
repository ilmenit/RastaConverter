ChangeLog
=========

RastaConverter-0.9  2012-04-18  ilmenit
---------------------------------------

* http://atarionline.pl/forum/comments.php?DiscussionID=1611&page=7#Item_28

RastaConverterBeta2  2012-04-20  ilmenit
----------------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__50#entry2505402

* fixed bug in the generator (thanks to Jakub Husak)
* new options: /cdither, /dither, /init=less, /euclid

RastaConverterBeta2  2012-04-21  ilmenit
----------------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__75#entry2505807

1. RastaConverter is not Quantizator! While they both convert pictures they are
  totally different programs and have different command line parameters. I
  wanted to create a new thread not to mix them, but discussion already started
  here. 
2. No multiprocessing for now. You can run a few instances of RastaConverter
  (with at least one second delay - RNG init resolution is 1 second), because
  sometimes picture get different details optimized.
3. To limit confusion the newest version is attached to this post.
4. Currently you can't edit created pictures. You can copy output files
  (output.*) to the Generator directory and run build.bat - this will compile
  executable file to be run on the Atari. Loading output to Graph2Font is
  planned, but both Graph2Font and RastaConverter must be extended for that.
5. I plan to add some better interface with the "mask of details" feature - user
  will be able to define a mask and algorithm will try to optimize more the
  masked area. It will increase user defined details f.e. faces on the
  pictures.
6. I started recently CUDA programming so maybe in the future we will have this
  converter much faster 
7. 64bit version can be created, but I haven't seen any boost with that.
8. Multiprocessing won't increase the speed. The slowest part is execution of
  raster program and it can't be parallelized.

RastaConverterBeta3  2012-04-26  ilmenit
----------------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__125#entry2509434

RastaConverter Beta3 attached. Big improvements in optimization heuristics and
"continue" option are two main features in this version.  Tomorrow I'm going on
holidays for 10 days so I publish it without other promised features.

* New dithering algorithms
* Changed command line parameters for dithering
* Improved mutation heuristics (more accurate)
* Changed default init behavior from smart to random
* Improved random initialization
* Preview for the destination picture and rescaled source picture
* On big enough desktops displayed pictures in the app have proper proportions
* Resuming of optimization added
* Conversion in Beta3 is MUCH faster than in Beta2 and overal picture quality
  is better.

Rasta-opthack  2012-05-05  phaeron
-----------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__200#entry2515740

This thing is really cool, especially for being pretty much hands off. My
random contribution (NTSC and PAL):

I spent way too much time hacking on the source and managed to optimize it a
bit (attached, based on beta 3; requires SSE2). Might be buggy -- had to
regressions on the way, and they might not all be fixed -- but it runs faster
now and still seems to produce decent output. What I found out on the way:

* As others have discovered, this sucker spends a ton of time in the color
  differencing function. Since the transform between RGB and YCbCr is linear,
  this can be immediately doubled in speed by doing the YCbCr conversion after
  the difference instead of before... but, it turns out, it's even faster to
  just precompute the differences between each pixel and the entire palette.
  This change more than tripled the iteration rate as not only does the
  differencing function basically go away during the run, but it also reduces
  the size of the output array by 75%.
* Several arrays are transposed from ideal memory ordering, although this is
  minor.
* Adding a line cache to the row evaluation loop is a huge gain since the
  converter starts re-evaluating mostly similar frames a lot after initial
  convergence. Actual execution and color re-matching drops to less than 5%,
  and I'm pretty sure that further gains could be made by caching the line
  evaluation results as well... but what this really means is that a more
  aggressive mutator is needed. The algorithm starts making very slow progress
  past 200K iterations.
* There appears to be an omission in the dithering routine. At least for
  Floyd-Steinberg, it is usually a good idea to alternate traversal direction
  on each scan line to avoid error diffusion skewing toward the right side of
  the image.

Rasta-opthack2  2012-05-05  phaeron
-----------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__200#entry2516103

I figured out that the post-evaluation pass after the raster program run was
redundant and added caching for line difference values, and also rewrote the
line cache to use a custom allocator. It should run about 50% faster now over
the previous version.

rasta-opthack3  2012-05-06  phaeron
-----------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__225#entry2516711

Here's an optimized version with some bug fixes:

* Fixed dithering not being taken into account -- the error map I added was
  being inited too soon, before the dithering had taken place.
* /continue now reloads NOPs in the raster instruction lists.
* Fixed bug where the first mutation after a /continue was always accepted
  unconditionally, because the score for the loaded solution wasn't
  evaluated. I think this may have been in the original build as well,
  although I don't have a buildable version of it to check.

There are still some discrepancies in the score after /continue... I'm going to
see if I can track down the remaining problems.
