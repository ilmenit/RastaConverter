#include "TargetPreview.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <unordered_map>

#include "ColorCorrection.h"
#include "TargetBuilder.h"
#include "DetailsMask.h"
#include "Program.h"
#include "TargetPicture.h"
#include "FreeImageIO.h"
#include "rgb.h"

namespace rc_live_ui {

namespace {

// Milliseconds of input stability before a job is dispatched. Long enough that
// a slider drag produces a handful of jobs rather than one per frame, short
// enough to feel immediate.
constexpr double kDebounceMs = 90.0;

double NowMs()
{
	return static_cast<double>(SDL_GetTicksNS()) / 1.0e6;
}

std::uint32_t PackRGBA(unsigned char r, unsigned char g, unsigned char b)
{
	// SDL_PIXELFORMAT_ABGR8888 on a little-endian host is 0xAABBGGRR.
	return static_cast<std::uint32_t>(r)
		| (static_cast<std::uint32_t>(g) << 8)
		| (static_cast<std::uint32_t>(b) << 16)
		| 0xFF000000u;
}

struct BitmapDeleter {
	void operator()(FIBITMAP* bitmap) const
	{
		if (bitmap != nullptr)
			FreeImage_Unload(bitmap);
	}
};
using BitmapPtr = std::unique_ptr<FIBITMAP, BitmapDeleter>;

// Caches colour->palette-index lookups. A 160x240 frame has at most 38400
// distinct colours and usually far fewer, so this turns the expensive distance
// functions (CIEDE2000 in particular) into a handful of real evaluations.
class PaletteLookup {
public:
	unsigned char operator()(const rgb& color)
	{
		const std::uint32_t key = (static_cast<std::uint32_t>(color.r) << 16)
			| (static_cast<std::uint32_t>(color.g) << 8)
			| static_cast<std::uint32_t>(color.b);
		auto it = cache_.find(key);
		if (it != cache_.end())
			return it->second;
		const unsigned char index = FindAtariColorIndex(color);
		cache_.emplace(key, index);
		return index;
	}

private:
	std::unordered_map<std::uint32_t, unsigned char> cache_;
};

} // namespace

int ResolveTargetHeight(int configured, int input_width, int input_height)
{
	if (configured > 0)
		return std::min(240, configured);
	if (input_width <= 0 || input_height <= 0)
		return 240;
	const double iw = input_width;
	const double ih = input_height;
	if (iw / ih > (320.0 / 240.0))
		return std::max(1, static_cast<int>(ih / (iw / 320.0)));
	return 240;
}

int ResolveOutputHeight(GraphicsMode mode, int configured,
	int input_width, int input_height)
{
	int height = configured > 0 ? std::min(240, configured) : 240;
	if (configured <= 0 && input_width > 0 && input_height > 0)
	{
		const double outputWidth =
			mode == GraphicsMode::Antic4 ? 336.0 : 320.0;
		const double aspect = static_cast<double>(input_width) / input_height;
		if (aspect > outputWidth / 240.0)
			height = std::max(1, static_cast<int>(
				input_height / (input_width / outputWidth)));
	}
	return mode == GraphicsMode::Antic4
		? NormalizeAntic4Height(height) : height;
}

const char* PreviewStageName(PreviewStage stage)
{
	switch (stage) {
	case PreviewStage::Source:    return "Source";
	case PreviewStage::Corrected: return "Corrected";
	case PreviewStage::Quantized: return "Quantized";
	case PreviewStage::Dithered:
	default:                      return "Target";
	}
}

const PreviewImage& PreviewResult::Stage(PreviewStage stage) const
{
	switch (stage) {
	case PreviewStage::Source:    return source;
	case PreviewStage::Corrected: return corrected;
	case PreviewStage::Quantized: return quantized;
	case PreviewStage::Dithered:
	default:                      return dithered;
	}
}

bool TargetPreview::Inputs::operator==(const Inputs& other) const
{
	return input_file == other.input_file
		&& palette_file == other.palette_file
		&& graphics_mode == other.graphics_mode
		&& height == other.height
		&& filter == other.filter
		&& brightness == other.brightness
		&& contrast == other.contrast
		&& gamma == other.gamma
		&& saturation == other.saturation
		&& vibrance == other.vibrance
		&& pre_dstf == other.pre_dstf
		&& dither == other.dither
		&& dither_strength == other.dither_strength
		&& dither_randomness == other.dither_randomness
		&& dual_mode == other.dual_mode
		&& details_file == other.details_file
		&& details_mode == other.details_mode
		&& details_strength == other.details_strength
		&& details_floor == other.details_floor
		&& details_feather == other.details_feather
		&& details_refine_mix == other.details_refine_mix;
}

TargetPreview::Inputs TargetPreview::Extract(const Configuration& cfg)
{
	Inputs inputs;
	inputs.input_file = cfg.input_file;
	inputs.palette_file = cfg.palette_file;
	inputs.graphics_mode = cfg.graphics_mode;
	inputs.height = cfg.height;
	inputs.filter = cfg.rescale_filter;
	inputs.brightness = cfg.brightness;
	inputs.contrast = cfg.contrast;
	inputs.gamma = cfg.gamma;
	inputs.saturation = cfg.saturation;
	inputs.vibrance = cfg.vibrance;
	inputs.pre_dstf = cfg.pre_dstf;
	inputs.dither = cfg.dither;
	inputs.dither_strength = cfg.dither_strength;
	inputs.dither_randomness = cfg.dither_randomness;
	inputs.dual_mode = cfg.dual_mode;
	// The mask is single-frame only, so dual mode has nothing to overlay.
	if (!cfg.dual_mode) {
		inputs.details_file = cfg.details_file;
		inputs.details_mode = cfg.details_mode;
		inputs.details_strength = cfg.details_strength;
		inputs.details_floor = cfg.details_floor;
		inputs.details_feather = cfg.details_feather;
		inputs.details_refine_mix = cfg.details_refine_mix;
	}
	return inputs;
}

TargetPreview::TargetPreview()
{
	worker_ = std::thread(&TargetPreview::WorkerMain, this);
}

TargetPreview::~TargetPreview()
{
	Shutdown();
}

void TargetPreview::Shutdown()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_)
			return;
		stopping_ = true;
	}
	cancel_token_.fetch_add(1);
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

void TargetPreview::Request(const Configuration& cfg)
{
	Inputs inputs = Extract(cfg);
	std::lock_guard<std::mutex> lock(mutex_);
	if (inputs.input_file.empty()) {
		has_pending_ = false;
		return;
	}
	if (has_pending_ && pending_ == inputs)
		return; // Already queued, keep waiting out the debounce.
	if (!has_pending_ && inputs == in_flight_ && (has_ready_ || busy_.load()))
		return; // Already displayed, or already being computed.

	// A genuine change invalidates any pinned request for the exact result.
	exact_pinned_ = false;
	inputs.exact = false;
	pending_ = std::move(inputs);
	has_pending_ = true;
	pending_since_ = NowMs();
	// Abandon whatever the worker is doing; its output is already stale.
	cancel_token_.fetch_add(1);
	wake_.notify_all();
}

void TargetPreview::ForceRefresh()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (in_flight_.input_file.empty() && pending_.input_file.empty())
		return;
	if (!has_pending_)
		pending_ = in_flight_;
	exact_pinned_ = true;
	pending_.exact = true;
	has_pending_ = true;
	pending_since_ = 0.0; // dispatch immediately
	cancel_token_.fetch_add(1);
	wake_.notify_all();
}

bool TargetPreview::Fetch(PreviewResult* out)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!has_ready_ || ready_.revision == fetched_revision_)
		return false;
	fetched_revision_ = ready_.revision;
	if (out != nullptr)
		*out = ready_;
	return true;
}

bool TargetPreview::Busy() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return busy_.load() || has_pending_;
}

void TargetPreview::Publish(const PreviewResult& result)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (stopping_)
		return;
	// Never let an older job overwrite a newer one, and never replace a
	// complete result with a partial one from the same job.
	if (result.generation < ready_.generation)
		return;
	if (result.generation == ready_.generation && ready_.complete && !result.complete)
		return;
	ready_ = result;
	ready_.revision = ++revision_;
	has_ready_ = true;
}

void TargetPreview::WorkerMain()
{
	for (;;) {
		Inputs job;
		std::uint64_t generation = 0;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			wake_.wait(lock, [this] { return stopping_ || has_pending_; });
			if (stopping_)
				return;
			// Honour the debounce without holding the lock in a spin.
			const double waited = NowMs() - pending_since_;
			if (waited < kDebounceMs) {
				const auto remaining = std::chrono::milliseconds(
					static_cast<int>(kDebounceMs - waited) + 1);
				wake_.wait_for(lock, remaining);
				if (stopping_)
					return;
				continue; // Re-check: the request may have been replaced.
			}
			job = pending_;
			job.exact = exact_pinned_;
			has_pending_ = false;
			in_flight_ = job;
			generation = generation_.fetch_add(1) + 1;
			busy_.store(true);
		}

		Compute(job, generation);
		busy_.store(false);
	}
}

void TargetPreview::Compute(const Inputs& inputs, std::uint64_t generation)
{
	PreviewResult result;
	result.generation = generation;
	const std::uint64_t token = cancel_token_.load();
	auto cancelled = [this, token] { return cancel_token_.load() != token; };
	const double started = NowMs();

	auto fail = [&](const std::string& message) {
		result.error = message;
		result.complete = true;
		Publish(result);
	};

	FREE_IMAGE_FORMAT format = FreeImageFormatUtf8(inputs.input_file);
	if (format == FIF_UNKNOWN) {
		fail("Unsupported or unreadable image format.");
		return;
	}
	BitmapPtr loaded(FreeImageLoadUtf8(inputs.input_file));
	if (!loaded) {
		fail("Could not open " + inputs.input_file);
		return;
	}
	if (cancelled())
		return;

	const int source_width = static_cast<int>(FreeImage_GetWidth(loaded.get()));
	const int source_height = static_cast<int>(FreeImage_GetHeight(loaded.get()));
	result.input_width = source_width;
	result.input_height = source_height;
	const int height = ResolveOutputHeight(inputs.graphics_mode,
		inputs.height, source_width, source_height);
	const int width = inputs.graphics_mode == GraphicsMode::Antic4
		? antic4_visible_width : 160;

	// Same rescale + 24-bit conversion the converter performs.
	BitmapPtr scaled(FreeImage_Rescale(loaded.get(), width, height, inputs.filter));
	if (!scaled) {
		fail("Could not rescale the source image.");
		return;
	}
	BitmapPtr rgb24(FreeImage_ConvertTo24Bits(scaled.get()));
	if (!rgb24) {
		fail("Could not convert the source image to 24-bit.");
		return;
	}
	if (cancelled())
		return;

	auto make_image = [&](PreviewImage& image) {
		image.width = width;
		image.height = height;
		image.pixels.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	};
	make_image(result.source);

	// FreeImage rows are bottom-up; the preview is stored top-down.
	auto row_of = [height](int y) { return height - 1 - y; };

	for (int y = 0; y < height; ++y) {
		const BYTE* row = FreeImage_GetScanLine(rgb24.get(), row_of(y));
		for (int x = 0; x < width; ++x) {
			const BYTE* pixel = row + x * 3;
			result.source.pixels[static_cast<size_t>(y) * width + x] =
				PackRGBA(pixel[FI_RGBA_RED], pixel[FI_RGBA_GREEN], pixel[FI_RGBA_BLUE]);
		}
	}

	// --- Colour correction, in the converter's order -----------------------
	FreeImage_AdjustBrightness(rgb24.get(), inputs.brightness);
	FreeImage_AdjustContrast(rgb24.get(), inputs.contrast);
	FreeImage_AdjustGamma(rgb24.get(), inputs.gamma);

	std::vector<std::vector<rgb>> corrected(height, std::vector<rgb>(width));
	make_image(result.corrected);
	for (int y = 0; y < height; ++y) {
		const BYTE* row = FreeImage_GetScanLine(rgb24.get(), row_of(y));
		for (int x = 0; x < width; ++x) {
			const BYTE* pixel = row + x * 3;
			rasta::RGB8 color{pixel[FI_RGBA_RED], pixel[FI_RGBA_GREEN], pixel[FI_RGBA_BLUE]};
			if (inputs.saturation != 0 || inputs.vibrance != 0)
				color = rasta::AdjustSaturationAndVibrance(color, inputs.saturation, inputs.vibrance);
			rgb& target = corrected[y][x];
			target.r = color.r;
			target.g = color.g;
			target.b = color.b;
			target.a = 0;
			result.corrected.pixels[static_cast<size_t>(y) * width + x] =
				PackRGBA(color.r, color.g, color.b);
		}
	}
	if (cancelled())
		return;

	// --- Details mask overlay ---------------------------------------------
	// Loaded through DetailsMask with the configured mode and parameters, so
	// the overlay is the effective map the optimizer weights errors by - not a
	// re-drawing of the source file, which in normalized and refined modes
	// looks nothing like what actually applies.
	if (!inputs.details_file.empty()) {
		DetailsMask mask;
		std::string mask_error;
		bool loaded = false;
		if (inputs.details_mode == "refined") {
			// Refined mode derives importance from the corrected source.
			std::vector<screen_line> rows(height);
			for (int y = 0; y < height; ++y) {
				rows[y].Resize(width);
				for (int x = 0; x < width; ++x)
					rows[y][x] = corrected[y][x];
			}
			loaded = mask.LoadRefined(inputs.details_file, width, height,
				rows.data(), inputs.details_strength, inputs.details_floor,
				inputs.details_feather, inputs.details_refine_mix, &mask_error);
		} else if (inputs.details_mode == "normalized") {
			loaded = mask.LoadNormalized(inputs.details_file, width, height,
				inputs.details_strength, inputs.details_floor,
				inputs.details_feather, &mask_error);
		} else {
			loaded = mask.LoadLegacy(inputs.details_file, width, height,
				&mask_error);
		}

		if (!loaded) {
			result.mask_error = mask_error.empty()
				? std::string("Could not read the details mask.") : mask_error;
		} else if (!mask.Empty()) {
			make_image(result.mask);
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					const unsigned char level = mask.At(
						static_cast<unsigned>(x), static_cast<unsigned>(y));
					// Alpha follows the weight so unemphasised regions stay
					// readable underneath the overlay.
					result.mask.pixels[static_cast<size_t>(y) * width + x] =
						static_cast<std::uint32_t>(level)
						| (static_cast<std::uint32_t>(level) << 8)
						| (static_cast<std::uint32_t>(level) << 16)
						| (static_cast<std::uint32_t>(level) << 24);
				}
			}
		}
	}

	// The colour-corrected view costs under a millisecond, so it goes out
	// immediately; the quantized stages follow when they are ready.
	result.compute_ms = static_cast<int>(NowMs() - started);
	Publish(result);

	// In dual mode PrepareDestinationPicture copies the high-colour source
	// straight through: quantization only happens inside the search. Showing a
	// quantized target here would be a preview of something the converter
	// never builds.
	if (inputs.dual_mode) {
		result.quantized = result.corrected;
		result.dithered = result.corrected;
		result.approximate = true;
		result.approximate_reason = "Dual-frame mode keeps the destination in full "
			"colour and quantizes inside the search, so there is no quantized target "
			"picture to preview.";
		result.compute_ms = static_cast<int>(NowMs() - started);
		result.complete = true;
		Publish(result);
		return;
	}

	// --- Quantization ------------------------------------------------------
	// The palette and distance function are process globals. Nothing else runs
	// while Setup is open, and the worker is joined before the conversion
	// starts, so writing them here is safe.
	// Same fallback the run uses: try the working directory, then the folder
	// the executable lives in.
	std::string palette_path = inputs.palette_file;
	if (!palette_path.empty() && !SDL_GetPathInfo(palette_path.c_str(), nullptr)) {
		if (const char* base = SDL_GetBasePath()) {
			const std::string candidate = std::string(base) + palette_path;
			if (SDL_GetPathInfo(candidate.c_str(), nullptr))
				palette_path = candidate;
		}
	}
	if (!::LoadAtariPalette(palette_path)) {
		fail("Could not read palette " + inputs.palette_file
			+ " - the preview needs it to show target colours.");
		return;
	}

	// CIEDE2000 and CIE94 cost seconds over a full frame, which is not a live
	// preview. Interactive updates use the fast Rasta metric and say so; the
	// exact target is one button away (design §7.6, cost tier C).
	const bool slow_distance = inputs.pre_dstf == E_DISTANCE_CIEDE
		|| inputs.pre_dstf == E_DISTANCE_CIE94;
	const bool approximate_distance = slow_distance && !inputs.exact;
	SetDistanceFunction(approximate_distance ? E_DISTANCE_RASTA : inputs.pre_dstf);
	if (approximate_distance) {
		result.approximate = true;
		result.exact_available = true;
		result.approximate_reason = std::string("Using the fast Rasta metric while you "
			"adjust. The chosen ")
			+ (inputs.pre_dstf == E_DISTANCE_CIEDE ? "CIEDE2000" : "CIE94")
			+ " metric takes seconds per update; press Update for the exact target.";
	}

	make_image(result.quantized);
	{
		PaletteLookup lookup;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const rgb mapped = atari_palette[lookup(corrected[y][x])];
				result.quantized.pixels[static_cast<size_t>(y) * width + x] =
					PackRGBA(mapped.r, mapped.g, mapped.b);
			}
			if (cancelled())
				return;
		}
	}
	result.compute_ms = static_cast<int>(NowMs() - started);
	Publish(result);

	// --- Dithering ---------------------------------------------------------
	rasta::DitherParams params;
	params.type = inputs.dither;
	params.strength = inputs.dither_strength;
	params.randomness = inputs.dither_randomness;

	// Knoll costs 64 x 128 distance evaluations per distinct colour, so it is
	// only computed on request. Until then the undithered mapping stands in.
	const bool knoll = inputs.dither == E_DITHER_KNOLL;
	const bool approximate_knoll = knoll && !inputs.exact;
	if (approximate_knoll) {
		params.type = E_DITHER_NONE;
		result.approximate = true;
		result.exact_available = true;
		result.approximate_reason = "Knoll dithering takes several seconds to "
			"compute; the preview shows the undithered palette mapping until you "
			"press Update.";
	}

	std::vector<std::vector<rgb>> target(height, std::vector<rgb>(width));
	std::set<unsigned char> used;
	// A private RNG keeps the preview from disturbing the run's reproducible
	// sequence, and keeps a dragged slider from changing the noise every frame.
	std::uint32_t rng_state = 0x9E3779B9u;
	auto jitter = [&rng_state](double value) -> double {
		rng_state = rng_state * 1664525u + 1013904223u;
		const double unit = ((rng_state >> 8) & 0xFFFF) / 65535.0;
		return (unit * 2.0 - 1.0) * value;
	};
	auto on_row = [&](int) { return !cancelled(); };

	// Knoll has its own ordered-dither core; everything else goes through the
	// error-diffusion path. Both are the code the conversion itself runs.
	const bool cancelled_during_build = (knoll && !approximate_knoll)
		? rasta::BuildKnollTarget(corrected, width, height, params,
			target, used, on_row, jitter)
		: rasta::BuildQuantizedTarget(corrected, width, height, params,
			target, used, on_row, jitter);
	if (cancelled_during_build)
		return;

	make_image(result.dithered);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const rgb& pixel = target[y][x];
			result.dithered.pixels[static_cast<size_t>(y) * width + x] =
				PackRGBA(pixel.r, pixel.g, pixel.b);
		}
	}

	// Palette utilization, counted over the final target.
	{
		PaletteLookup lookup;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x)
				++result.palette_histogram[lookup(target[y][x])];
		}
	}
	result.colors_used = 0;
	for (std::uint32_t count : result.palette_histogram) {
		if (count > 0)
			++result.colors_used;
	}

	result.compute_ms = static_cast<int>(NowMs() - started);
	result.complete = true;
	Publish(result);
}

} // namespace rc_live_ui
