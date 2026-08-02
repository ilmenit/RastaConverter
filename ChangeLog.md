ChangeLog
=========

RastaConverter1.0-RC6      2026-08-02 [RELEASE CANDIDATE]
* Fixed dual-frame executables showing garbage below images shorter than 240
  scanlines. Their ANTIC display lists now stop at the configured picture
  height while preserving the required screen-memory alignment at scanline 204.
* Dual-frame executables now initialize each optimized raster program from its
  matching `.opt.ini` data instead of mixing optimized code with `.rp.ini`.

RastaConverter1.0-RC5      2026-08-01 [RELEASE CANDIDATE]
* Fixed a cross-platform crash when Continue was selected for some conversions
  in Recent. Resumed runs with saved optimizer state now rebuild their rendered
  picture before the UI can display it, including runs whose first evaluation
  ties the restored best score instead of publishing an improvement.

RastaConverter1.0-RC4      2026-07-31 [RELEASE CANDIDATE]
* Added ANTIC 4 character-mode output. Select it in the Live UI or with
  `/graphics_mode=antic4`; ANTIC E remains the default.
* Added Normal and Wide playfield selection to both ANTIC E and ANTIC 4.
  Normal is the default; use `/playfield=wide` when the extra horizontal image
  area is preferred.
* Corrected the ANTIC/GTIA model and generated XEX timing for both playfield
  widths, including display-list DMA, LMS and character-fetch badlines,
  refresh deferral, CHBASE transitions, and horizontal register changes.
* Fixed blinking ANTIC 4 executables, incorrect colours at the left edge of a
  wide playfield, and differences between optimizer output and Altirra.
* Normal-width executables cover the unused side regions with missiles. ANTIC 4
  uses fifth-player priority conflicts to produce black independently of
  COLPF3, while ANTIC E uses its corresponding missile border schedule.
* Added complete ANTIC 4 export: screen and character data, PMG data, raster
  programs, optimizer state, and a dedicated MADS template. Existing
  width-less ANTIC 4 state remains compatible and is interpreted as Wide.
* Added GUI support throughout setup, preview, copied command lines, recent-run
  badges, and XEX freshness detection. Dual-frame controls remain disabled for
  ANTIC 4 because it is currently single-frame only.
* Recent Conversion cards now identify both graphics mode (TEXT/GFX) and
  playfield width (WIDE/NORM), with higher-contrast badge palettes tailored to
  the Dark, High Contrast, and Light themes.
* Editor preferences now survive restarts independently of conversion recipes:
  theme, font size, setup window and splitter sizes, collapsed sections,
  "Only changed", and whether new conversions use their own subfolder.
* The in-conversion viewer now hides its Mask display controls when no details
  mask is active, while retaining the neutral layer needed to start live
  painting.
* Expanded automated coverage for the four mode/width timing profiles, command
  configuration, line-cache isolation, and persistent UI preferences. ANTIC E
  and ANTIC 4 output was also checked against Altirra.
* Removed an accidentally committed Python bytecode cache and corrected the
  ANTIC 4 proof tool to use the released wide-playfield timing schedule.

RastaConverter1.0-RC3      2026-07-30 [RELEASE CANDIDATE]
* Added experimental ANTIC 4 character-mode output. Select it in the Live UI
  or with /graphics_mode=antic4; ANTIC E remains the default.
  ANTIC 4 uses a 168-colour-clock wide playfield and optimizes all five
  playfield colours, with each 4x8 cell selecting COLPF2 or COLPF3 for its
  fourth colour. Heights are complete 8-scanline character rows from 8 to 240.
  Added hardware-aware ANTIC 4 scheduling for LMS badlines, ordinary badlines,
  continuation lines, deferred refresh, and CHBASE transitions. The timing
  model and randomized five-seed differential suite reproduce Altirra output
  exactly.
* Added complete ANTIC 4 export: padded screen data, packed character sets,
  PMG data, raster programs, optimizer state, and a dedicated MADS template.
  The Recent view recognizes text-mode runs and its XEX action assembles them.
  Added GUI support throughout setup, preview, copied command lines, recent-run
  badges, and XEX freshness detection. Dual-frame controls are disabled because
  ANTIC 4 is currently single-frame only.
* The in-conversion viewer now hides its Mask display controls when no details
  mask is active, while retaining the neutral layer needed to start live
  painting.
* Preserved the ANTIC E code path and merged the RC2 portability fixes. Unit
  tests, ANTIC E and ANTIC 4 conversion smoke tests, and ANTIC 4 XEX assembly
  all pass after integration.
* Removed an accidentally committed Python bytecode cache and corrected the
  ANTIC 4 proof tool to use the released wide-playfield timing schedule.

RastaConverter1.0-RC2      2026-07-28 [RELEASE CANDIDATE]
* Added a persistent Font size control beside Reset in the Live UI. It scales
  every in-program window from the existing 100% default up to 200%; 200% is
  the practical limit at the supported 1420x900 minimum window size.
* Improved UI readability while keeping the warm Atari-inspired visual
  language. Muted and faint text now have substantially stronger contrast, and
  the persistent Style menu offers Dark, High contrast and Light themes across
  Setup, Dashboard, Recent and the editor.
* Fixed XEX builds on Windows. The assembler and each path argument are now
  passed directly to SDL's process API, so cmd.exe can no longer reinterpret
  Generator as a command or break paths containing spaces.
* Fixed non-ASCII Windows paths throughout image, palette, state, preview and
  recent-run I/O by using the native wide-character filesystem and FreeImage
  APIs behind one UTF-8 interface.
* Fixed absolute Unix paths supplied as separate option values, such as
  --output /tmp/picture.png, being mistaken for command-line options.
* Fixed XEX assembler selection on unsupported CPU/OS combinations: native
  bundled MADS is used only where its binary matches the host, otherwise the
  portable launcher reports the platform limitation explicitly.
* Made /quiet conversions genuinely headless even from the GUI build, fixed
  failures opening a native error dialog, propagated initialization and
  conversion failures to a non-zero process exit status, and corrected
  recent-run registration for Windows directory separators.
* Fixed the installed layout so runtime palettes, generators and the UI font
  are beside the executable where the application expects them.
* Removed obsolete bundled SDL2 DLLs and the old monolithic FreeImage DLL from
  Windows packages. Release packages now contain the consistent vcpkg runtime
  dependency set actually used by the executable.
* Expanded smoke coverage for Unicode paths, console-only Windows conversion,
  installation layout, and command-line parsing across platforms.

RastaConverterBeta22      2026-07-27 [AI RELEASE]
* New interactive Live UI (built with /livegui, or by launching with no command line):
  - Setup screen with the image picker on top, one continuous grouped form covering
    every conversion option beside a large live preview, native system file pickers,
    option search, an "Only changed" audit view, and "Copy command line" for exact
    GUI/CLI parity.
  - Live target preview: source, colour-corrected, quantized and dithered views with
    split-wipe and blink comparison, plus a palette-utilization readout showing how
    many of the 128 colours the target actually reaches.
  - Run dashboard replacing the plain stats text: convergence chart, plateau readout,
    mutation-operator breakdown, island diagnostics, dual-frame phase and schedule,
    a configuration recap, and explicit Save / Stop and save / Abort actions.
  - Every conversion writes into its own folder beside the source image,
    rc-<image>-NNN (rc-photo-001, rc-photo-002, ...), instead of scattering a
    dozen artifacts across the image's folder. The counter steps up when a name
    is taken, so a re-run never disturbs an earlier one.
  - "Recent" browser listing previous conversions with their picture, score and
    evaluation count, offering Continue (resume that run in place) or Reuse
    (same settings, fresh folder). It reads the header the .opt file already
    writes, so runs from earlier versions are listed too, and it appears by
    itself in the empty preview column before an image is chosen.
  - Finishing a conversion returns to the setup screen with the settings still
    in place, opening on the history so the run just completed is the first
    card, instead of quitting the program.
  - Live editing is one paused session behind a single "Pause & Edit" button,
    replacing the earlier paint checkbox and separate destination button. It
    takes the whole window - the dashboard panels are frozen while the optimizer
    is paused anyway - and offers a two-segment target selector, eight tools with
    drawn icons (pan, brush, line, rectangle, ellipse, bucket, eyedropper, revert
    brush), a scrolling canvas with Ctrl+wheel zoom about the cursor, a reference
    layer to paint over (target, source, best output) with an optional error
    heatmap, per-stroke undo/redo, and an apply bar that states what applying
    will cost before it is pressed.
  - The picker matches what is being painted: a 0-255 value ramp with presets for
    the details mask, each value annotated with the error multiplier it produces,
    and the hardware palette in hardware order for the destination - 16 hues by 8
    luminances, which is what the 128-entry palette actually is, with the colours
    the picture already uses ringed.
  - Mask strength, mode, floor, feather and scoring are editable in the editor
    and commit with the strokes. One retarget covers both, so they stopped being
    decisions frozen at Setup.
  - Applying commits to this run, or to a fresh rc-<image>-NNN through the apply
    menu; the old "Branch" button, which read as an editor tool and named no
    outcome, is gone.
  - Fixed the preview canvas leaving its child window unclosed, which Dear ImGui
    reported as a red-bordered error window over the setup screen and the panel
    beside it as soon as an image was loaded. The palette readout was being drawn
    inside the canvas rather than below it, which is what let the mismatch through.
  - Slider values are labelled beside the track instead of inside it, so a value
    parked near the middle is no longer hidden under the grab.
  - The editor's brush is measured on screen rather than in the buffer: an Atari
    pixel is twice as wide as it is tall, so a round brush now covers half as
    many columns as rows instead of painting a wide oval, and a square brush is
    square. The cursor draws the exact pixels the brush would touch, and follows
    the round/square choice, which the old circle outline did not.
  - Line, rectangle and ellipse preview the pixels they would paint, at the
    brush width, while the mouse is still down. Previously they showed a
    hairline rubber band and the real, thick shape only appeared on release.
  - The picture drawn beneath the mask has its own visibility slider, and the
    whole control is gone when editing the destination, where an opaque layer
    covers whatever is under it anyway.
  - "Recent" cards make a run into an Atari executable: XEX assembles the run
    with the bundled MADS - single-frame or dual, detected from the folder - into
    <run>/<name>.xex and opens it with whatever the system uses for .xex files.
    It runs off the UI thread, reports the assembler's own message when it
    fails, and afterwards just opens the file unless Shift asks for a rebuild.
  - Names on a "Recent" card are links: the folder opens in the file manager,
    the source name in the image editor, and the thumbnail opens the picture the
    run produced. Right-clicking a card offers all of it in one menu, along with
    "Copy command line".
  - The convergence chart says what it is showing. It had no scale at all, and
    a vertical range of two percent either side of the value, so once the steep
    early drop scrolled out of its five-minute window - or after resuming an
    already-converged run - it drew a horizontal line and left no way to tell
    whether that meant "finished" or "broken". It now labels both ends of the
    curve with the values they reached and the evaluation range beneath them,
    scales vertically to the data actually plotted, says "unchanged across this
    view" outright when there is nothing to plot, and reads out any point on
    hover. The whole run is kept rather than the last few minutes: when the
    buffer fills, every other sample is dropped and the sampling stride doubles,
    so the early drop never disappears and memory stays fixed. "Zoom to recent"
    switches to the last four minutes at full resolution, where late progress is
    too small to see against the run as a whole.
  - The setup form is five numbered sections - Source, Algorithm, Colour,
    Dithering, Details mask - instead of eight numbered by pipeline stage, which
    put three sections at "2" and four at "3" and read as a numbering bug.
    Objective and Dual frame are no longer sections of their own: they are the
    last and first groups of Algorithm, because neither is a decision anyone
    makes without the optimizer settings in front of them. The section reads
    frames, then search, then objective.
  - The objective control says what each choice costs and buys, including the
    two things that were invisible: scoring the source directly costs nothing
    extra per evaluation and lands measurably closer to the original, while the
    paused editor can only repaint the destination in the default target mode.
    Measured over two images and three seeds at three million evaluations,
    /objective=source was 2-15% closer to the source in mean OKLab distance than
    the default, and never worse; the gap is widest with dithering off.
  - The objective is named after what it scores against, matching the two
    pictures the viewer already shows: "Score against: Target picture / Source
    picture". /objective=legacy is now /objective=target - "legacy" described the
    option's history rather than its behaviour, and implied it was the deprecated
    choice when it is the default and the only one that allows repainting the
    destination mid-run. The old token is still accepted, and produces
    byte-identical runs.
  - The four structural objectives (source-spatial, source-composite, source-edge,
    source-region) are deprecated and no longer offered in the setup form. They
    add a full-frame term to every evaluation, which costs about sixty times the
    throughput, and measured on a photograph and on illustrated artwork at 240
    lines none of them beat plain source scoring on colour error, 95th-percentile
    error, SSIM or gradient fidelity - at equal wall clock or at equal evaluation
    counts. They still run from the command line, and a configuration that
    already selects one still shows it in the form, so no existing recipe becomes
    unreachable.
  - The four structural objectives are gone from the code, not just from the
    form: source-spatial, source-composite, source-edge and source-region, their
    three weight options, the full-frame scoring pass in the evaluator and about
    170 lines of VisualObjective. The four names now select /objective=source
    with a warning - that is what they were built on and what beat them - and
    /spatial_weight, /edge_weight and /region_weight are accepted and ignored,
    so an existing recipe still runs rather than failing on an unknown option.
  - A run started from the command line shows the dashboard too. /livegui now
    decides only whether the setup screen appears first; the legacy three-blit
    display with statistics under three thumbnails survives solely in builds
    made with ENABLE_LIVE_UI=OFF, which have nothing else to draw with.
  - The palette is chosen from a list of the ones shipped with the program,
    with Browse for anything else, instead of a path you had to know how to fill
    in.
  - Every value beside a form slider is an editable field: click, type, Enter.
    The value is clamped to the option's range when it is committed rather than
    per keystroke, so a half-typed number never reaches the conversion, and
    hovering the field states the range.
  - History length reaches 50000 instead of 64. The engine never had the lower
    limit - it was a slider bound - and the slider is logarithmic now, because
    the useful settings span 1 to tens of thousands and no linear track can
    serve both ends.
  - New /subfolder=on|off, and an "Own folder" checkbox in the run bar beside
    "Preprocess only": on, a run writes into its own rc-<image>-NNN folder; off,
    it writes beside the source image, taking the plain name when free and
    numbering it otherwise so nothing is overwritten.
  - The progress panel plots throughput instead of score. A convergence chart
    starts at whatever distance a random program happens to have - a number with
    no ceiling, often ten times where the run ends up - so it spent the whole run
    showing the first two seconds and a flat line for the rest. Evaluations per
    second has a natural scale, sits under the Rate it explains, marks the
    average, and shows a stall or the cost of a live edit as a visible dip.
  - "Clear all" in the Recent browser empties the history, with a confirmation
    that offers - unchecked - to delete the run folders too. Only directories
    named rc-... are ever removed, so a history entry pointing somewhere
    unexpected is forgotten rather than deleted, and the dialog says which of
    the two the button is about to do.
* Fixed two ways a run could sit forever instead of finishing. Interrupt
  handling left the loop without re-taking the lock that the shutdown path
  waits under, which is undefined behaviour and can wait forever. And the main
  loop waited for updates that only workers produce, so if the workers all
  stopped it waited for news that would never come - two runs were found still
  ticking over five and a half hours later, at 5% CPU, with nothing written but
  the preprocessed images. It now notices there is nothing left to wait for and
  finishes.
* A picture can be given by path again. `rasta photos/pic.png` was rejected
  because the caller discarded any positional argument containing a slash, and
  `rasta /home/me/pic.png` never reached it because the parser reads a leading
  '/' as an option prefix - so on Linux and macOS the program could only be
  handed a file in the current directory. A '/'-prefixed token is now an option
  only where it names one; everywhere else it is a path. Covered by tests.
* Windows runtime DLLs are no longer copied next to Linux and macOS binaries,
  where they were dead weight - and one of them was a stale SDL2 left over from
  before the SDL3 port.
* Documentation caught up with the program: a quick start in the README, a table
  in BUILD.md saying which build configuration produces what, concrete package
  commands for Fedora and Debian/Ubuntu, and corrected claims about the
  interfaces, the six colour-distance functions and their two independent
  defaults.
* Ctrl+C, a kill or the terminal closing now stop a conversion the way the Stop
  button does: the run saves and exits. Previously the process died where it
  stood and everything since the last autosave was lost - and in GUI builds the
  signal was swallowed entirely, so a run could not be stopped from outside at
  all. Handled in single-frame and dual-mode loops and in the setup screen.
* The version stamp said Beta20; it now matches this release, so .opt headers
  written today no longer claim the previous version.
* src/Makefile builds again. It listed thirteen of the twenty-six sources and
  had not produced a working program in a long time; it now builds the
  console-only target, which is the one a plain Makefile can honestly build,
  and says so.
  - GUI-only conveniences: a warning before overwriting an existing run's
    artifacts, and palette lookup relative to the executable so launching from a
    shortcut works.
  - A run started from the GUI now records its own settings in the "; CmdLine:"
    header of the .opt file. They were never written there before, because the
    form's values never passed through the command-line parser - so a GUI run's
    header described the launch arguments, which is nothing at all for a
    double-clicked program. Everything that reads that header was affected:
    Reuse in the Recent browser restored only the input image, and resuming a
    GUI run came back with default settings rather than the ones it was started
    with, mask and all. Job paths are also made absolute when the run starts, so
    a recipe still resolves from a different working directory.
  - Reuse and resume no longer drop back to the old three-blit display:
    re-parsing the stored settings decided which interface to use by looking for
    /livegui among them, which a recorded recipe never contains.
  - Hovering a card in the Recent browser shows the full command line that
    produced it, and the Reuse button says plainly when a run recorded no
    settings to restore.
  - Correct handling of scaled (HiDPI) displays: without it the interface was drawn
    undersized while mouse hit-testing used unscaled coordinates, so hovering one
    control highlighted a different one and clipping cut through text.
  - Searching the option list filters the live form, so a match is the actual
    control ready to edit. An option that exists but is gated by the current
    configuration explains itself instead of reporting no match.
  - Knoll dithering can now be previewed exactly on request, not just approximated:
    its ordered-dither core is shared with the converter (verified byte-identical).
  - The details mask is visible at last, in both the setup preview and during the
    conversion: Off / Overlay (with an opacity slider) / Only. It shows the
    *effective* weight map produced by DetailsMask for the selected mode, not a
    redraw of the source file - in normalized and refined modes those differ a lot.
  - The details strength slider reaches 15 instead of stopping at 1. Legacy mode
    weights white areas by 1 + strength, so the classic heavy-emphasis settings
    (3 for 4x, 15 for 16x) were unreachable from the GUI; the engine never had an
    upper limit. The form now states the resulting multiplier for the active mode.
* /saturation and /vibrance were implemented but undocumented; they now have help.txt
  entries explaining when to reach for each.
* Fixed a crash on starting a conversion: input_bitmap, output_bitmap and
  destination_bitmap had no initializers and were only ever zeroed because the
  single RastaConverter happened to be a global with static storage. Once the live
  UI began creating one per run, those pointers held heap garbage, and
  ShowInputBitmap's "if (destination_bitmap)" guard passed it straight to FreeImage.
* /preprocess now exits with status 0 instead of 1: it reported a successful
  preprocessing run as a failure, and terminated the process outright, which would
  have ended a live-UI session rather than returning to the setup screen.
* Removed the unused global Evaluator and documented which conversion globals need
  resetting between runs, which is what allowed several conversions to run in one
  process safely (only color_indexes_on_dst_picture accumulates).
* Fixed /continue ignoring an output path chosen through the GUI: main.cpp re-read
  /output from the parsed command line, which sent a resume started from the live UI
  to "output.png" instead of the run's own folder.
* Fixed a colour corruption in dithering: the blue channel of the diffused error was
  clamped by testing and writing the green channel, leaving blue unbounded and
  corrupting green whenever blue saturated. All error-diffusion dither modes
  (floyd, line, line2, chess, simple, 2d, jarvis) produce slightly different, more
  correct output as a result. /dither=none and /dither=rfloyd are unaffected.
* Quantization and dithering of the target picture moved into a shared implementation
  (TargetBuilder.h) so the Setup preview and the real conversion cannot diverge.
  Verified byte-identical to the previous implementation across every dither mode.
* Improved Dual Mode to use Input image as Target image - should improve output quality in dual mode
* During dithering phase user can now quit the app (useful for slow knoll dithering)
* Dithering preview is shown on the right side of screen.
* Added input dithering for Dual Mode (/dual_dither=knoll|random|chess|line|line2) - adds noise to guide color distribution and remove banding
* Removed the experimental alternating-scanline coarse search, its command switches, environment gates, GUI status, and optimizer lifecycle after broader evidence failed to establish a dependable advantage; preserved its implementation and results in benchmark documentation.
* Moved GUI optimizer status lines below the source/destination captions so the labels are no longer overwritten.


RastaConverterBeta20      2025-10-29 [AI RELEASE]
* When using /continue you can override in the command line parameters e.g., add /opt=legacy in parallel to /continue to switch optimizer to 'legacy'
* Image file name in command line doesn't need to be the first parameter anymore.
* Command line accepts now paths with spaces when defined within " "
* Fixes to taskbar icon caching on Windows 10+
* Saving proper RC version number to .rp/.opt files
* Default /predistance reverted to 'ciede' (testing proven that it's more reliable). Default /distance stays 'rasta'.

RastaConverterBeta19      2025-10-20 [AI RELEASE]
* Added extra color distance functions: oklab, cie94 and rasta (boosts chroma for dark grey colors to prevent too gray destination picture)
* "rasta" color distance is now default for both /predistance and /distance - should bring better conversion results with default parameters
* Running GUI conversion is getting a taskbar icon equal to converted image (a gimmick for more parallel conversions).

RastaConverterBeta18      2025-09-23 [AI RELEASE]
* Reworked acceptance criteria in optimization algorithms
* Default drift values significantly increased to /ua=1000 /ud=0.1 (for high /s it may be worth /ua=100000 /ud=0.00001)
* /opt=legacy mode adjusted to reflect Beta10 algorithm

RastaConverterBeta17      2025-09-19 [AI RELEASE]
* SDL GUI version on Linux and flickering of input/destination images fixed
* Showing conversion rate fixed on Linux
* Fixed /continue - shouldn't crash anymore
* Fix to saving numbers to be more than 32bit (like number of evaluations for long run)
* check NO_GUI build on Linux - shouldn't open an SDL window.
* Fixed knoll dithering
* add to Linux build process copying ttf font to output directory so GUI version can work
* Added /opt=legacy option - legacy LAHC behavior
* Added /unstuck_after and /unstuck_drift options to help with leaving local optimum
* Added /v /version parameter to return the current version (useful for making GUI)

RastaConverterBeta16      2025-08-24 [AI RELEASE]
* Fix to ommited Change Value mutation - should reach better Norm. Distance.
* Fix to DLAS optimization algorithm - should also reach better Norm. Distance.
* Added Profile Guided Optimization to build system to improve performance even more.
* Increased default thread cache size from 16MB to 64MB. Lower with /cache if taking too much RAM.

RastaConverterBeta15      2025-08-23 [AI RELEASE]
* Dual mode - first official version, check documentation
* Fixing SDL window handling on Linux
* Improved performance with Intel C++ compiler                                     

RastaConverterBeta14      2025-08-20 [AI RELEASE]
* Revert to codebase of Beta12
  - too much indirections and extra layers introduced non-fixable performance drop
* Move to CMake for more modern and cross platform building
* Returning LAHC algorithm and making it default one (better for long-runs than DLAS)
* Removed new mutation operators - they did not positively impact conversion rate nor reaching quality
* Dual-frame mode (CRT blending) - not finished, work in progress    

RastaConverterBeta13.1    2025-08-15 [AI RELEASE]
Fixing regression:
* Added /mutation_base=best|current (default: best) to control mutation baseline
  - best: mutate from global best each step (legacy behavior)
  - current: mutate from currently accepted solution (exploratory alternative)
* Improved optimizer RNG to be per-thread for stable, reproducible multi-threaded runs
* Adjusted low-color initialization to align with legacy behavior

RastaConverterBeta13      2025-08-14 [AI RELEASE]
* Significant code refactoring for easier project maintanance
* New cross-platform build system
* Returning LAHC algorithm as it provides better results than DLAS in very long runs
* New mutation operators
* Further caching improvements
* Dual-frame mode (CRT blending) - not finished, work in progress    

RastaConverterBeta12      2025-03-06
* Improved multi-threaded performance with region-based work distribution
* Implemented LRU (Least Recently Used) cache eviction for significantly reduced cache overhead
* Optimized mutation selection using adaptive success rate tracking
* Added batch mutation processing for faster convergence
* Replaced random number generator with faster XorShift algorithm
* Reduced memory allocation overhead with optimized vector operations
* Improved cache coordination between threads
* Better work distribution to minimize thread contention

RastaConverterBeta11      2025-03-05
* Implemented Diversified Late Acceptance Search (DLAS) algorithm replacing LAHC
* Much faster convergence to quality solutions
* Better quality results with fewer solutions (5-10 solutions work well)

RastaConverterBeta10      
* Added command line parameter to window title
* Changed default auto-save to "auto" to save SSD disks life

RastaConverterBeta9      2024-04-17 
* fixed sprite repositioning bug (sheddy, phaeron)
* removed dependency on Allegro 4 library, replaced by an SDL-based GUI
* 64bit version released
* removed threads limit for /threads param
* by default auto-save enabled (each 100K iterations)
* improved build for Linux (dmsc, polluks)
* improved help file (snicklin99)
* LLVM compiler used for extra speed (sheddy)
* removed --help parameter that was pointing to help.txt file
* Conditional compilation with NO_GUI directive that removes the SDL GUI dependency (for developers who want to run RC on GUI-less devices)


RastaConverterBeta9      2024-04-17 
* fixed sprite repositioning bug (sheddy, phaeron)
* removed dependency on Allegro 4 library, replaced by an SDL-based GUI
* 64bit version released
* removed threads limit for /threads param
* by default auto-save enabled (each 100K iterations)
* improved build for Linux (dmsc, polluks)
* improved help file (snicklin99)
* LLVM compiler used for extra speed (sheddy)
* removed --help parameter that was pointing to help.txt file
* Conditional compilation with NO_GUI directive that removes the SDL GUI dependency (for developers who want to run RC on GUI-less devices)

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
