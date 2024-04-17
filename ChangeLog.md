ChangeLog
=========

RastaConverterBeta9      2024-04-17 
* fixed sprite repositioning bug (sheddy, phaeron)
* removed dependency on Allegro 4 library, replaced by SDL2
* 64bit version released
* removed threads limit for /threads param
* by default auto-save enabled (each 100K iterations)
* improved build for Linux (dmsc, polluks)
* improved help file (snicklin99)
* LLVM compiler used for extra speed (sheddy)
* removed --help parameter that was pointing to help.txt file
* Conditional compilation with NO_GUI directive that removes dependency on SDL2 (for developers who want to run RC on GUI-less devices)

RastaConverterBeta7      2013-06-01 Ilmenit & Phaeron
----------------------------------------------

* MULTI-THREADED version - many thanks to Phaeron!
* Fixed bug causing crash with /dither=knoll + /preprocess 
* /onoff option added to control used registers - basic version
* Optimized version of raster program (.rp) is saved as .opt
* Changed default options to: /filter=box /pal=laoo
* /height is automatically assigned to keep proportion of the screen if width:height is higher than 4:3
* Fixed screen redraw when switching the application window
* New dithering types: line and line2
* Fixed Jarvis dithering
* Fix in floyd dithering - should't produce that many vertical stripes
* /picture_colors was buggy. Removed.

RastaConverterBeta5.1      2012-07-18  Ilmenit
----------------------------------------------

* Fix for the /continue bug that appeared in Beta5
* Reverted entries in the ChangeLog.md


RastaConverterBeta5        2012-07-17  Ilmenit
----------------------------------------------

* Added Preprocess parameters - can be useful for GUI preview:

/preprocess   If this switch exists then RastaConverter generates and saves only destination picture.
/brightness   Brightness color corection.
/contrast     Contrast color corection.
/gamma        Gamma color corection.

* Random Number Generator changed to Marsenne Twister for a long period.
/seed - if exists the constant seed for the random number generator is used

* Limiting palette 
/picture_colors - limits palette to colors existing in the destination picture. No average colors will be used.  

* Fast processing of low color (<5) pictures, where sprites are not needed

* "Tabu Search" algorithm has been replaced with amazing "Late Acceptance Hill Climbing" algorithm for /s>1. 

* You can periodically save the results of RastaConverter
/save - saves best solution after every 'n' evaluations

* Default distance function for preprocess is set to CIEDE2000. Then default /distance function is used.
/predistance - sets color distance function for preprocess


RastaConverterNewFeatures  2012-05-24  Ilmenit
----------------------------------------------

- Details mask added /details=inputfile /details_val=value
- Improved dithering algorithms
- New dithering algorithm (/dither=knoll)
- Dithering strength added (/dither_val=value)
- /distance param replaced /euclid
- new distance color function (CIEDE2000) that solves problems with "too gray" output
- /noborder param removed


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

Rasta-opthack4  2012-05-06  phaeron
-----------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__225#entry2516959

Another version of the speed hack:

* I integrated the Linux portability changes... hopefully the delta should be
  smaller now.
* Fixed an uninitialized variable bug that sometimes prevented the (s)ave
  command being recognized after a /continue.
* The swap mutation bug noted above is fixed in this version.
* A .csv file is now written out with statistics for time and distance score
  relative to evaluation count.
* The raster program (.rp) file now contains the distance score.

Rasta-opthack5  2012-05-09  phaeron
-----------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__275#entry2518629

After ivop's report with G++ I decided to try a profile-guided optimization
build, and to my surprise, it's significantly faster -- at least another 50%
faster on my Core i7, around 3.5-4.5K evals/sec. Pogo build is attached. I
also went ahead and integrated another one of the gcc build fixes and
stripped out all the 'auto' crap I put in when I was hacking on it so it
could be built with VS2005/2008. That's probably it from me for optimizations
for now... it's up to someone else to get us the next order of magnitude.

Trying to increase quality is the next thing, I think... emphasis masks might
be the way to go for a start, and with the error map I put into my build it
would essentially be free. Post-quantization dithering is much harder to do
but might solve some of the blotchiness that results with the existing dither
option.

RastaConverterBorder  2012-05-18  Xuel
--------------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__350#entry2524423

I found that I can "fix" the top border by simply patching rasta.cpp so that
it never mutates the COLBAK initial register value away from zero. No need
for additional instruction shuffling because Rasta will naturally figure it
out from there.

This zip file contains a recompiled version of Rasta-opthack5 with this
border fix. It also includes a patched version of no_name.asq that adds the
missile borders on the left and right of the image. Although
counter-intuitive, this lets you use the /noborder mode to use all four
players for detail while still getting a nice border. You can unzip this over
your existing rasta work area at your own risk.

Linux Support
-------------

Added ivop's Linux Makefile.  The following contributions were rolled into
Rasta-opthack5.

### rastahacklinux  2012-05-12  frogstar_robot

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__200#entry2516222

I've applied Ivop's patch, tweaked the source, and built Phaeron's optimized
version on 64 bit Ubuntu Oneiric. The amd64 binary and the linux buildable
source is included.

### rastahacklinux.tar.gz  2012-05-06  frogstar_robot

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__225#entry2516790

Source patched to build on Linux. 64 bit amd64 binary included.

### patch  2012-05-07  ivop

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__225#entry2517118

Linux users need this small patch and add -std=c++0x to CXXFLAGS (needed for
the auto keyword).  Great work Phaeron and thanks for including the portability
patches.

### rasta-linux.patch  2012-04-26  ivop

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__150#entry2513277

Here's a patch to compile this on Linux. Just a few small fixes to make it more
standards compliant and a simple Makefile. Did not fix the pathsep issue, but
you can just specify the input file and palette file on the command line.

Thanks for using portable libraries like allegro and freeimage.

Max Evaluations
---------------

Added /max_evals flag.

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

RastaConverterBeta2  2012-04-20  ilmenit
----------------------------------------

* http://www.atariage.com/forums/topic/156160-quantizator/page__st__50#entry2505402

* fixed bug in the generator (thanks to Jakub Husak)
* new options: /cdither, /dither, /init=less, /euclid

RastaConverter-0.9  2012-04-18  ilmenit
---------------------------------------

* http://atarionline.pl/forum/comments.php?DiscussionID=1611&page=7#Item_28



