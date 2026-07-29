#include "ConfigModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>

// Optimizer history length is a process global rather than a Configuration
// field; see rasta.cpp:77.
extern int solutions;

namespace rc_live_ui {

const char* const kDistanceLabels[6] = {
	"Euclidean", "YUV", "CIEDE2000", "CIE94", "OKLab", "Rasta"};
const char* const kDistanceTokens[6] = {
	"euclid", "yuv", "ciede", "cie94", "oklab", "rasta"};

const char* const kDitherLabels[10] = {
	"None", "Floyd-Steinberg", "Random Floyd", "Line", "Line 2",
	"Chessboard", "Simple", "2D", "Jarvis", "Knoll"};
const char* const kDitherTokens[10] = {
	"none", "floyd", "rfloyd", "line", "line2",
	"chess", "simple", "2d", "jarvis", "knoll"};

// Named after the two pictures the viewer already shows, because that is
// exactly what the choice is: score a candidate against the Target tab or
// against the Source tab. "Legacy" named the option's history rather than its
// behaviour, and implied it was the deprecated one when it is the default.
// The old token is still accepted; see config.cpp.
const char* const kObjectiveLabels[2] = {"Target picture", "Source picture"};
const char* const kObjectiveTokens[2] = {"target", "source"};
const char* const kGraphicsModeLabels[2] = {
	"ANTIC 4 (text mode)", "ANTIC E (gfx mode)"
};
const char* const kGraphicsModeTokens[2] = {"antic4", "e"};

const char* const kOptimizerLabels[3] = {"DLAS", "LAHC", "Legacy LAHC"};
const char* const kOptimizerTokens[3] = {"dlas", "lahc", "legacy"};

const char* const kInitLabels[4] = {"Random", "Smart", "Empty", "Less"};
const char* const kInitTokens[4] = {"random", "smart", "empty", "less"};

const char* const kFilterLabels[6] = {
	"Box", "Bilinear", "Bicubic", "B-spline", "Catmull-Rom", "Lanczos3"};
const char* const kFilterTokens[6] = {
	"box", "bilinear", "bicubic", "bspline", "catmullrom", "lanczos3"};
const FREE_IMAGE_FILTER kFilterValues[6] = {
	FILTER_BOX, FILTER_BILINEAR, FILTER_BICUBIC,
	FILTER_BSPLINE, FILTER_CATMULLROM, FILTER_LANCZOS3};

const char* const kDualDitherLabels[6] = {
	"None", "Knoll", "Random", "Chessboard", "Line", "Line 2"};
const char* const kDualDitherTokens[6] = {
	"none", "knoll", "random", "chess", "line", "line2"};

const char* const kDetailsModeLabels[3] = {"Legacy", "Normalized", "Refined"};
const char* const kDetailsModeTokens[3] = {"legacy", "normalized", "refined"};

namespace {

// Shortest round-trippable decimal, so /gamma=1 rather than /gamma=1.000000.
std::string Num(double value)
{
	char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "%.6g", value);
	return buffer;
}

std::string Num(unsigned long long value)
{
	return std::to_string(value);
}

std::string Num(int value)
{
	return std::to_string(value);
}

bool NearlyEqual(double a, double b)
{
	return std::fabs(a - b) < 1e-9;
}

std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// Quotes a path only when it needs it, so the copied command line stays
// readable in the common case.
std::string QuoteIfNeeded(const std::string& text)
{
	if (text.find_first_of(" \t\"'") == std::string::npos)
		return text;
	std::string quoted = "\"";
	for (char c : text) {
		if (c == '"' || c == '\\')
			quoted.push_back('\\');
		quoted.push_back(c);
	}
	quoted.push_back('"');
	return quoted;
}

const Configuration& Defaults()
{
	static const Configuration defaults = DefaultConfiguration();
	return defaults;
}

// Builds the table once. Every entry's `modified` predicate compares against
// Defaults(), so adding an option here is all that is needed for it to appear
// in search, the Modified view and the generated command line.
std::vector<OptionDesc> BuildTable()
{
	std::vector<OptionDesc> table;
	auto add = [&table](const char* id, const char* cli, const char* label,
		const char* help, Category category, Tier tier, bool is_flag,
		std::function<bool(const Configuration&)> modified,
		std::function<std::string(const Configuration&)> value,
		std::function<bool(const Configuration&)> available = {},
		const char* unavailable_hint = "",
		bool in_form = true, const char* location_hint = "") {
		table.push_back(OptionDesc{id, cli, label, help, category, tier, is_flag,
			std::move(modified), std::move(value), std::move(available),
			unavailable_hint, in_form, location_hint});
	};

	// --- 1 Source and destination -----------------------------------------
	add("graphics_mode", "graphics_mode", "Graphics mode",
		"Destination display format. ANTIC 4 uses five-colour character cells; "
		"ANTIC E uses the traditional four-colour bitmap.",
		Category::Source, Tier::Restart, false,
		[](const Configuration& c) {
			return c.graphics_mode != Defaults().graphics_mode;
		},
		[](const Configuration& c) {
			return kGraphicsModeTokens[GraphicsModeIndex(c.graphics_mode)];
		});
	add("input", "i", "Input image",
		"Source image to convert. Any format FreeImage reads; drag and drop works too.",
		Category::Source, Tier::Restart, false,
		[](const Configuration& c) { return !c.input_file.empty(); },
		[](const Configuration& c) { return c.input_file; },
		{}, "", /*in_form*/ false, "Chosen at the top of the window.");
	add("height", "h", "Height",
		"Target height in scanlines, up to 240. Auto derives it from the source aspect ratio.",
		Category::Source, Tier::Restart, false,
		[](const Configuration& c) { return c.height != Defaults().height; },
		[](const Configuration& c) { return Num(c.height); });
	add("filter", "filter", "Resize filter",
		"Filter used to rescale the source to the Atari's 160-pixel width.",
		Category::Source, Tier::Restart, false,
		[](const Configuration& c) { return c.rescale_filter != Defaults().rescale_filter; },
		[](const Configuration& c) { return kFilterTokens[FilterIndex(c.rescale_filter)]; });
	add("palette", "pal", "Palette",
		"Atari palette .act file describing the 128 hardware colours.",
		Category::Source, Tier::Restart, false,
		[](const Configuration& c) { return c.palette_file != Defaults().palette_file; },
		[](const Configuration& c) { return c.palette_file; });

	// --- 2 Target: colour --------------------------------------------------
	add("brightness", "brightness", "Brightness",
		"Brightness applied to the source before quantization [-100..100].",
		Category::Colour, Tier::Staged, false,
		[](const Configuration& c) { return c.brightness != Defaults().brightness; },
		[](const Configuration& c) { return Num(c.brightness); });
	add("contrast", "contrast", "Contrast",
		"Contrast applied to the source before quantization [-100..100].",
		Category::Colour, Tier::Staged, false,
		[](const Configuration& c) { return c.contrast != Defaults().contrast; },
		[](const Configuration& c) { return Num(c.contrast); });
	add("gamma", "gamma", "Gamma",
		"Gamma applied to the source before quantization [0..8].",
		Category::Colour, Tier::Staged, false,
		[](const Configuration& c) { return !NearlyEqual(c.gamma, Defaults().gamma); },
		[](const Configuration& c) { return Num(c.gamma); });
	add("saturation", "saturation", "Saturation",
		"Uniform chroma scale [-100..100]. Raising it past what the palette can "
		"represent collapses colours together; watch the palette usage readout.",
		Category::Colour, Tier::Staged, false,
		[](const Configuration& c) { return c.saturation != Defaults().saturation; },
		[](const Configuration& c) { return Num(c.saturation); });
	add("vibrance", "vibrance", "Vibrance",
		"Chroma scale weighted against existing saturation [-100..100], so dull "
		"regions gain most. Usually the better answer to a grey-looking result.",
		Category::Colour, Tier::Staged, false,
		[](const Configuration& c) { return c.vibrance != Defaults().vibrance; },
		[](const Configuration& c) { return Num(c.vibrance); });

	// --- 2 Target: dithering ----------------------------------------------
	add("predistance", "predistance", "Colour distance",
		"How the closest Atari colour is chosen for each pixel when the target "
		"picture is built. Not the same as the search distance, which scores "
		"candidates during the run.",
		Category::Colour, Tier::Restart, false,
		[](const Configuration& c) { return c.pre_dstf != Defaults().pre_dstf; },
		[](const Configuration& c) { return kDistanceTokens[c.pre_dstf]; });
	add("dither", "dither", "Dithering",
		"Dithering algorithm used when building the target picture.",
		Category::Colour, Tier::Restart, false,
		[](const Configuration& c) { return c.dither != Defaults().dither; },
		[](const Configuration& c) { return kDitherTokens[c.dither]; });
	add("dither_val", "dither_val", "Dither strength",
		"Scales the diffused error. 1.0 is the algorithm's natural strength.",
		Category::Colour, Tier::Restart, false,
		[](const Configuration& c) { return !NearlyEqual(c.dither_strength, Defaults().dither_strength); },
		[](const Configuration& c) { return Num(c.dither_strength); },
		[](const Configuration& c) { return c.dither != E_DITHER_NONE; },
		"Applies once a dithering algorithm is selected.");
	add("dither_rand", "dither_rand", "Dither randomness",
		"Blends random noise into the dither pattern [0..1].",
		Category::Colour, Tier::Restart, false,
		[](const Configuration& c) { return !NearlyEqual(c.dither_randomness, Defaults().dither_randomness); },
		[](const Configuration& c) { return Num(c.dither_randomness); },
		[](const Configuration& c) { return c.dither != E_DITHER_NONE; },
		"Applies once a dithering algorithm is selected.");

	// --- 3 Objective -------------------------------------------------------
	add("objective", "objective", "Score against",
		"Which picture a candidate is measured against: the quantized, dithered "
		"target, or the original source picture. Scoring the "
		"source directly costs no more per evaluation and usually lands closer "
		"to the original; the target objective is the default because it keeps "
		"scores comparable with older runs and is the only one that allows "
		"repainting the destination mid-run.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.visual_objective != Defaults().visual_objective; },
		[](const Configuration& c) { return kObjectiveTokens[c.visual_objective]; });
	add("distance", "distance", "Search distance",
		"Colour-distance function used to score candidates during the search.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.dstf != Defaults().dstf; },
		[](const Configuration& c) { return kDistanceTokens[c.dstf]; });
	// --- 3 Details mask ----------------------------------------------------
	add("details", "details", "Mask image",
		"Greyscale image marking where detail matters most. Brighter means "
		"higher priority.",
		Category::Details, Tier::Restart, false,
		[](const Configuration& c) { return !c.details_file.empty(); },
		[](const Configuration& c) { return c.details_file; });
	add("details_mode", "details_mode", "Mask mode",
		"How the mask is interpreted: legacy arithmetic, normalized with a "
		"background floor, or refined blending with source importance.",
		Category::Details, Tier::Restart, false,
		[](const Configuration& c) { return c.details_mode != Defaults().details_mode; },
		[](const Configuration& c) { return c.details_mode; });
	add("details_val", "details_val", "Mask strength",
		"How strongly the mask biases scoring. In legacy mode white areas weigh 1 + this value, so 3 gives 4x and 15 gives 16x; there is no upper limit.",
		Category::Details, Tier::Live, false,
		[](const Configuration& c) { return !NearlyEqual(c.details_strength, Defaults().details_strength); },
		[](const Configuration& c) { return Num(c.details_strength); });
	add("details_floor", "details_floor", "Background floor",
		"Normalized mode only: the priority floor applied outside the mask "
		"[0.01..1].",
		Category::Details, Tier::Live, false,
		[](const Configuration& c) { return !NearlyEqual(c.details_floor, Defaults().details_floor); },
		[](const Configuration& c) { return Num(c.details_floor); },
		[](const Configuration& c) { return c.details_mode == "normalized"; },
		"Normalized details-mask mode only.");
	add("details_feather", "details_feather", "Feather radius",
		"Normalized mode only: softens the mask edge by this many pixels [0..8].",
		Category::Details, Tier::Restart, false,
		[](const Configuration& c) { return c.details_feather != Defaults().details_feather; },
		[](const Configuration& c) { return Num(static_cast<int>(c.details_feather)); },
		[](const Configuration& c) { return c.details_mode == "normalized"; },
		"Normalized details-mask mode only.");
	add("details_refine_mix", "details_refine_mix", "Refine mix",
		"Refined mode only: blend between the mask and source-derived "
		"importance [0..1].",
		Category::Details, Tier::Restart, false,
		[](const Configuration& c) { return !NearlyEqual(c.details_refine_mix, Defaults().details_refine_mix); },
		[](const Configuration& c) { return Num(c.details_refine_mix); },
		[](const Configuration& c) { return c.details_mode == "refined"; },
		"Refined details-mask mode only.");
	add("details_score", "details_score", "Apply to scoring",
		"Whether the mask weights per-pixel scoring. Turn off to use it only "
		"for mutation targeting.",
		Category::Details, Tier::Restart, false,
		[](const Configuration& c) { return c.details_score != Defaults().details_score; },
		[](const Configuration& c) { return std::string(c.details_score ? "on" : "off"); });
	add("details_allocate", "details_allocate", "Bias mutation choice",
		"Steers which scanlines get mutated towards the masked regions.",
		Category::Details, Tier::Restart, true,
		[](const Configuration& c) { return c.details_allocate != Defaults().details_allocate; },
		[](const Configuration&) { return std::string(); });
	add("details_global_period", "details_global_period", "Global sweep every",
		"With mutation biasing on, force an unbiased global choice every N "
		"mutations so nothing is starved.",
		Category::Details, Tier::Restart, false,
		[](const Configuration& c) { return c.details_global_period != Defaults().details_global_period; },
		[](const Configuration& c) { return Num(static_cast<int>(c.details_global_period)); },
		[](const Configuration& c) { return c.details_allocate && c.details_mode != "legacy"; },
		"Applies when mutation biasing is on, outside legacy mask mode.");

	// --- 3 Algorithm -------------------------------------------------------
	add("optimizer", "opt", "Optimizer",
		"Acceptance strategy: LAHC (late acceptance), DLAS (delayed acceptance) "
		"or the legacy LAHC behaviour.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.optimizer != Defaults().optimizer; },
		[](const Configuration& c) { return kOptimizerTokens[c.optimizer]; });
	add("solutions", "s", "History length",
		"Acceptance-history length for LAHC/DLAS, 1 to 50000. Longer accepts "
		"worse moves for longer, exploring more before it settles.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration&) { return GetSolutions() != 1; },
		[](const Configuration&) { return Num(GetSolutions()); });
	add("init", "init", "Initial state",
		"How the first raster program is seeded before optimization starts.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.init_type != Defaults().init_type; },
		[](const Configuration& c) { return kInitTokens[c.init_type]; });
	add("unstuck_after", "unstuck_after", "Escalate after",
		"Evaluations without improvement before the search widens its "
		"acceptance. 0 disables escalation.",
		Category::Algorithm, Tier::Live, false,
		[](const Configuration& c) { return c.unstuck_after != Defaults().unstuck_after; },
		[](const Configuration& c) { return Num(c.unstuck_after); });
	add("unstuck_drift", "unstuck_drift", "Escalation drift",
		"Normalized distance added to the acceptance threshold per evaluation "
		"while stuck.",
		Category::Algorithm, Tier::Live, false,
		[](const Configuration& c) { return !NearlyEqual(c.unstuck_drift_norm, Defaults().unstuck_drift_norm); },
		[](const Configuration& c) { return Num(c.unstuck_drift_norm); });
	add("seed", "seed", "RNG seed",
		"Fixed seed makes a run reproducible; otherwise it is taken from the "
		"clock.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.initial_seed != 0; },
		[](const Configuration& c) { return Num(static_cast<unsigned long long>(c.initial_seed)); });
	add("onoff", "onoff", "Register on/off file",
		"Restricts which colour and player registers the search may use, per "
		"scanline range.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return !c.on_off_file.empty(); },
		[](const Configuration& c) { return c.on_off_file; });

	// --- 3 Dual frame ------------------------------------------------------
	add("dual", "dual", "Dual-frame mode",
		"Produces two interleaved frames, trading flicker for apparent colour "
		"depth.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.dual_mode != Defaults().dual_mode; },
		[](const Configuration& c) { return std::string(c.dual_mode ? "on" : "off"); });
	add("first_dual_steps", "first_dual_steps", "Bootstrap A for",
		"Evaluations spent bootstrapping frame A before frame B exists.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.first_dual_steps != Defaults().first_dual_steps; },
		[](const Configuration& c) { return Num(c.first_dual_steps); },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("after_dual_steps", "after_dual_steps", "Frame B start",
		"After A's bootstrap: copy A to B, or generate a fresh B which costs a "
		"second bootstrap.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.after_dual_steps != Defaults().after_dual_steps; },
		[](const Configuration& c) { return c.after_dual_steps; },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("altering_dual_steps", "altering_dual_steps", "Alternate every",
		"Evaluations per alternation block once both frames exist.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.altering_dual_steps != Defaults().altering_dual_steps; },
		[](const Configuration& c) { return Num(c.altering_dual_steps); },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("dual_blending", "dual_blending", "Blend space",
		"Colour space used to blend the two frames for preview and export.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.dual_blending != Defaults().dual_blending; },
		[](const Configuration& c) { return c.dual_blending; },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("dual_luma", "dual_luma", "Temporal luma penalty",
		"Penalizes brightness differences between the two frames. Higher means "
		"less flicker.",
		Category::Algorithm, Tier::Live, false,
		[](const Configuration& c) { return !NearlyEqual(c.dual_luma, Defaults().dual_luma); },
		[](const Configuration& c) { return Num(c.dual_luma); },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("dual_chroma", "dual_chroma", "Temporal chroma penalty",
		"Penalizes colour differences between the two frames. Higher means "
		"less colour flicker.",
		Category::Algorithm, Tier::Live, false,
		[](const Configuration& c) { return !NearlyEqual(c.dual_chroma, Defaults().dual_chroma); },
		[](const Configuration& c) { return Num(c.dual_chroma); },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("dual_dither", "dual_dither", "Input dithering",
		"Dithers the source before dual optimization, adding noise the two "
		"frames can average out.",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return c.dual_dither != Defaults().dual_dither; },
		[](const Configuration& c) { return kDualDitherTokens[c.dual_dither]; },
		[](const Configuration& c) { return c.dual_mode; },
		"Dual-frame mode only.");
	add("dual_dither_val", "dual_dither_val", "Input dither strength",
		"Strength of the dual-mode input dithering [0..2].",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return !NearlyEqual(c.dual_dither_val, Defaults().dual_dither_val); },
		[](const Configuration& c) { return Num(c.dual_dither_val); },
		[](const Configuration& c) { return c.dual_mode && c.dual_dither != E_DUAL_DITHER_NONE; },
		"Applies once dual-mode input dithering is selected.");
	add("dual_dither_rand", "dual_dither_rand", "Input dither randomness",
		"Blends random noise into the dual-mode input dither pattern [0..1].",
		Category::Algorithm, Tier::Restart, false,
		[](const Configuration& c) { return !NearlyEqual(c.dual_dither_rand, Defaults().dual_dither_rand); },
		[](const Configuration& c) { return Num(c.dual_dither_rand); },
		[](const Configuration& c) { return c.dual_mode && c.dual_dither != E_DUAL_DITHER_NONE; },
		"Applies once dual-mode input dithering is selected.");

	// --- Run & output ------------------------------------------------------
	add("output", "o", "Output base name",
		"Base filename for every artifact the run writes.",
		Category::RunOutput, Tier::Restart, false,
		[](const Configuration& c) { return c.output_file != Defaults().output_file; },
		[](const Configuration& c) { return c.output_file; },
		{}, "", /*in_form*/ false, "Set in the run bar at the bottom of the window.");
	add("threads", "threads", "Worker threads",
		"Parallel search threads. More is faster up to the machine's hardware "
		"thread count.",
		Category::RunOutput, Tier::Restart, false,
		[](const Configuration& c) { return c.threads != Defaults().threads; },
		[](const Configuration& c) { return Num(c.threads); });
	add("cache", "cache", "Line cache",
		"Rendered-line cache per thread, in MB. More cache means fewer "
		"re-simulations.",
		Category::RunOutput, Tier::Restart, false,
		[](const Configuration& c) { return c.cache_size != Defaults().cache_size; },
		[](const Configuration& c) { return Num(c.cache_size / (1024 * 1024)); });
	add("max_evals", "max_evals", "Evaluation limit",
		"Stops the run after this many candidate evaluations. Unlimited means "
		"it runs until you stop it.",
		Category::RunOutput, Tier::Live, false,
		[](const Configuration& c) { return c.max_evals != Defaults().max_evals; },
		[](const Configuration& c) { return Num(c.max_evals); });
	add("save", "save", "Autosave period",
		"Evaluations between automatic saves, or auto for roughly every 30 "
		"seconds.",
		Category::RunOutput, Tier::Live, false,
		[](const Configuration& c) { return c.save_period != Defaults().save_period; },
		[](const Configuration& c) {
			return c.save_period < 0 ? std::string("auto") : Num(c.save_period);
		});
	add("continue", "continue", "Continue stopped run",
		"Resumes from the existing output files instead of starting over.",
		Category::RunOutput, Tier::Restart, true,
		[](const Configuration& c) { return c.continue_processing; },
		[](const Configuration&) { return std::string(); });
	add("subfolder", "subfolder", "Own folder per run",
		"On, a run writes into its own rc-<image>-NNN folder beside the source "
		"image. Off, it writes beside the source image directly.",
		Category::RunOutput, Tier::Restart, false,
		[](const Configuration& c) { return c.run_subfolder != Defaults().run_subfolder; },
		[](const Configuration& c) { return std::string(c.run_subfolder ? "on" : "off"); },
		{}, "", /*in_form*/ false, "Set in the run bar at the bottom of the window.");
	add("preprocess", "preprocess", "Preprocess only",
		"Writes the source and target images, then exits without searching.",
		Category::RunOutput, Tier::Restart, true,
		[](const Configuration& c) { return c.preprocess_only; },
		[](const Configuration&) { return std::string(); });

	return table;
}

} // namespace

const char* CategoryTitle(Category category)
{
	switch (category) {
	case Category::Source:    return "Source and destination";
	case Category::Algorithm: return "Algorithm";
	case Category::Colour:    return "Colour";
	case Category::Details:   return "Details mask";
	case Category::RunOutput: return "Run & output";
	default:                  return "";
	}
}

const char* CategoryOrdinal(Category category)
{
	switch (category) {
	case Category::Source:    return "1";
	case Category::Algorithm: return "2";
	case Category::Colour:    return "3";
	case Category::Details:   return "4";
	default:                  return "";
	}
}

const char* TierTooltip(Tier tier)
{
	switch (tier) {
	case Tier::Live:    return "Can be tuned live while a run is in progress.";
	case Tier::Staged:  return "Can be previewed live during a run; takes effect on Apply & Retarget.";
	case Tier::Restart:
	default:            return "Fixed once a run starts; changing it needs a restart.";
	}
}

Configuration DefaultConfiguration()
{
	// Mirrors the defaults registered in Configuration::Process. Kept as data
	// rather than by re-running the parser, because Process() also seeds the
	// global RNG and writes process globals.
	Configuration c;
	c.input_file.clear();
	c.output_file = "output.png";
	c.palette_file = "Palettes/laoo.act";
	c.details_file.clear();
	c.on_off_file.clear();
	c.dstf = E_DISTANCE_RASTA;
	c.pre_dstf = E_DISTANCE_CIEDE;
	c.visual_objective = E_OBJECTIVE_LEGACY_TARGET;
	c.continue_processing = false;
	c.dither = E_DITHER_NONE;
	c.dither_randomness = 0.0;
	c.dither_strength = 1.0;
	c.details_strength = 0.5;
	c.details_mode = "legacy";
	c.details_floor = 0.25;
	c.details_feather = 1;
	c.details_refine_mix = 0.5;
	c.details_score = true;
	c.details_allocate = false;
	c.details_global_period = 5;
	c.brightness = 0;
	c.contrast = 0;
	c.gamma = 1.0;
	c.saturation = 0;
	c.vibrance = 0;
	c.save_period = -1; // "auto"
	c.initial_seed = 0; // 0 stands for "random" in the UI
	c.cache_size = 64 * 1024 * 1024;
	c.preprocess_only = false;
	c.threads = 1;
	c.width = 160;
	c.height = -1; // auto
	c.graphics_mode = GraphicsMode::AnticE;
	c.max_evals = 1000000000000000000ULL;
	c.rescale_filter = FILTER_BOX;
	c.init_type = E_INIT_RANDOM;
	c.quiet = false;
	c.dual_mode = false;
	c.first_dual_steps = 100000;
	c.after_dual_steps = "copy";
	c.altering_dual_steps = 50000;
	c.dual_blending = "yuv";
	c.dual_luma = 0.2;
	c.dual_chroma = 0.1;
	c.dual_dither = E_DUAL_DITHER_NONE;
	c.dual_dither_val = 0.125;
	c.dual_dither_rand = 0.0;
	c.optimizer = Configuration::E_OPT_LAHC;
	c.unstuck_after = 0ULL;
	c.unstuck_drift_norm = 0.0;
	return c;
}

const std::vector<OptionDesc>& AllOptions()
{
	static const std::vector<OptionDesc> table = BuildTable();
	return table;
}

bool IsOptionAvailable(const OptionDesc& option, const Configuration& cfg)
{
	return !option.available || option.available(cfg);
}

const OptionDesc* FindOption(const std::string& id)
{
	for (const OptionDesc& option : AllOptions()) {
		if (option.id == id)
			return &option;
	}
	return nullptr;
}

std::vector<const OptionDesc*> ModifiedOptions(const Configuration& cfg)
{
	std::vector<const OptionDesc*> modified;
	for (const OptionDesc& option : AllOptions()) {
		// The job's own paths are not "changed settings"; see OptionDesc::in_form.
		if (option.in_form && option.modified(cfg))
			modified.push_back(&option);
	}
	return modified;
}

int ModifiedCountInCategory(const Configuration& cfg, Category category)
{
	int count = 0;
	for (const OptionDesc& option : AllOptions()) {
		if (option.in_form && option.category == category && option.modified(cfg))
			++count;
	}
	return count;
}

bool MatchesQuery(const OptionDesc& option, const std::string& query)
{
	if (query.empty())
		return true;
	std::string needle = ToLower(query);
	// A leading slash means the user is typing a CLI flag.
	if (!needle.empty() && (needle[0] == '/' || needle[0] == '-'))
		needle.erase(0, 1);
	if (needle.empty())
		return true;
	return ToLower(option.label).find(needle) != std::string::npos
		|| ToLower(option.cli).find(needle) != std::string::npos
		|| ToLower(option.id).find(needle) != std::string::npos
		|| ToLower(option.help).find(needle) != std::string::npos;
}

std::string BuildCommandLineArgs(const Configuration& cfg)
{
	std::ostringstream line;
	// The input path is positional-friendly but emitted explicitly so the
	// command survives being pasted anywhere.
	bool first = true;
	for (const OptionDesc& option : AllOptions()) {
		if (!option.modified(cfg))
			continue;
		if (!first)
			line << ' ';
		first = false;
		line << '/' << option.cli;
		if (!option.is_flag)
			line << '=' << QuoteIfNeeded(option.value(cfg));
	}
	return line.str();
}

std::string BuildCommandLine(const Configuration& cfg, const char* executable)
{
	const std::string args = BuildCommandLineArgs(cfg);
	if (args.empty())
		return executable;
	return std::string(executable) + " " + args;
}

std::string DisplayValue(const OptionDesc& option, const Configuration& cfg)
{
	if (option.is_flag)
		return option.modified(cfg) ? "on" : "off";
	if (option.id == "height")
		return cfg.height <= 0 ? "auto" : std::to_string(cfg.height);
	if (option.id == "graphics_mode")
		return kGraphicsModeLabels[GraphicsModeIndex(cfg.graphics_mode)];
	if (option.id == "seed")
		return cfg.initial_seed == 0 ? "random" : std::to_string(cfg.initial_seed);
	if (option.id == "max_evals")
		return cfg.max_evals >= Defaults().max_evals ? "unlimited" : std::to_string(cfg.max_evals);
	if (option.id == "filter")
		return kFilterLabels[FilterIndex(cfg.rescale_filter)];
	if (option.id == "dither")
		return kDitherLabels[cfg.dither];
	if (option.id == "dual_dither")
		return kDualDitherLabels[cfg.dual_dither];
	if (option.id == "distance")
		return kDistanceLabels[cfg.dstf];
	if (option.id == "predistance")
		return kDistanceLabels[cfg.pre_dstf];
	if (option.id == "objective")
		return kObjectiveLabels[cfg.visual_objective];
	if (option.id == "optimizer")
		return kOptimizerLabels[cfg.optimizer];
	if (option.id == "init")
		return kInitLabels[cfg.init_type];
	if (option.id == "details_mode")
		return kDetailsModeLabels[DetailsModeIndex(cfg.details_mode)];
	const std::string value = option.value(cfg);
	return value.empty() ? "none" : value;
}

int FilterIndex(FREE_IMAGE_FILTER filter)
{
	for (int i = 0; i < 6; ++i) {
		if (kFilterValues[i] == filter)
			return i;
	}
	return 0;
}

int GraphicsModeIndex(GraphicsMode mode)
{
	return mode == GraphicsMode::Antic4 ? 0 : 1;
}

void ApplyGraphicsModeChoice(Configuration& cfg, GraphicsMode mode,
	bool& antic_e_dual_mode)
{
	if (mode == cfg.graphics_mode)
		return;
	if (cfg.graphics_mode == GraphicsMode::AnticE)
	{
		antic_e_dual_mode = cfg.dual_mode;
	}
	cfg.graphics_mode = mode;
	if (mode == GraphicsMode::Antic4)
	{
		if (cfg.height > 0)
			cfg.height = NormalizeAntic4Height(cfg.height);
		cfg.dual_mode = false;
	}
	else
	{
		cfg.dual_mode = antic_e_dual_mode;
	}
}

int DetailsModeIndex(const std::string& mode)
{
	for (int i = 0; i < 3; ++i) {
		if (mode == kDetailsModeTokens[i])
			return i;
	}
	return 0;
}

int GetSolutions()
{
	return solutions;
}

void SetSolutions(int value)
{
	solutions = value < 1 ? 1 : value;
}

} // namespace rc_live_ui
