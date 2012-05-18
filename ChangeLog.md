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
