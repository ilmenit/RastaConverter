#pragma once

// Background computation of the stage-2 target preview (design §7.6).
//
// Produces four views of the same pixels - source, colour-corrected, quantized
// and dithered - so the user can see what the palette and the dither actually
// do before committing to a multi-hour run. Everything runs on one worker
// thread; the UI thread only ever swaps a finished result in.
//
// The quantization and dithering go through rasta::BuildQuantizedTarget, the
// same code the real conversion uses, so preview and result cannot disagree.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config.h"

namespace rc_live_ui {

// A preview surface in SDL_PIXELFORMAT_ABGR8888 order, at Atari resolution
// (160 x height). Display doubles the width to honour the pixel aspect.
struct PreviewImage {
	int width = 0;
	int height = 0;
	std::vector<std::uint32_t> pixels;

	bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// Which point in the pipeline the viewer is showing.
enum class PreviewStage {
	Source,
	Corrected,
	Quantized,
	Dithered,
};

const char* PreviewStageName(PreviewStage stage);

struct PreviewResult {
	// Which job produced this; increases per dispatched job.
	std::uint64_t generation = 0;
	// Increases on every publish, including the partial ones a single job
	// emits as its stages finish. This is what Fetch() compares against.
	std::uint64_t revision = 0;
	PreviewImage source;
	PreviewImage corrected;
	PreviewImage quantized;
	PreviewImage dithered;
	// The effective details-mask weights, as an overlay: grey level is the
	// weight, alpha rises with it so unmasked areas stay out of the way.
	// Produced by DetailsMask itself, so what is shown is what scoring uses.
	PreviewImage mask;
	std::string mask_error;

	// Per-palette-entry pixel counts of the dithered target, for the
	// utilization grid. Index is the Atari palette entry.
	std::uint32_t palette_histogram[128] = {};
	int colors_used = 0;

	// False while only the early stages are filled in. The worker publishes
	// the source and corrected views as soon as it has them so a slow
	// quantization never leaves the viewer empty.
	bool complete = false;

	// True when the shown target is not what the conversion would produce.
	// Set for Knoll, and for the slow distance functions while the preview is
	// running in its fast approximation.
	bool approximate = false;
	std::string approximate_reason;
	// True when an exact recomputation is available on request.
	bool exact_available = false;

	// Milliseconds the last full computation took, used to warn about slow
	// option combinations.
	int compute_ms = 0;

	std::string error;

	const PreviewImage& Stage(PreviewStage stage) const;
};

class TargetPreview {
public:
	TargetPreview();
	~TargetPreview();

	TargetPreview(const TargetPreview&) = delete;
	TargetPreview& operator=(const TargetPreview&) = delete;

	// Requests a recomputation if anything the preview depends on changed.
	// Cheap to call every frame; the debounce and change detection live here.
	void Request(const Configuration& cfg);

	// Forces a recomputation even when nothing changed (the "Update preview"
	// button for slow option combinations).
	void ForceRefresh();

	// Copies the newest finished result out. Returns false when nothing new
	// has arrived since the previous call.
	bool Fetch(PreviewResult* out);

	bool Busy() const;

	// Stops the worker. Called before the Setup window closes so the global
	// palette and distance function are left alone once the real run starts.
	void Shutdown();

private:
	// Everything the preview output depends on. Compared to decide whether a
	// recomputation is needed.
	struct Inputs {
		std::string input_file;
		std::string palette_file;
		int height = -1;
		FREE_IMAGE_FILTER filter = FILTER_BOX;
		int brightness = 0;
		int contrast = 0;
		double gamma = 1.0;
		int saturation = 0;
		int vibrance = 0;
		e_distance_function pre_dstf = E_DISTANCE_CIEDE;
		e_dither_type dither = E_DITHER_NONE;
		double dither_strength = 1.0;
		double dither_randomness = 0.0;
		// In dual mode the destination stays high-colour, so the preview must
		// not show a quantized target it would never produce.
		bool dual_mode = false;
		// Details mask, so the overlay tracks the settings that shape it.
		std::string details_file;
		std::string details_mode;
		double details_strength = 0.5;
		double details_floor = 0.25;
		unsigned details_feather = 1;
		double details_refine_mix = 0.5;
		// Whether to compute the slow paths for real. Deliberately excluded
		// from operator==: it is pinned separately, so the per-frame Request()
		// (which always asks for the fast path) cannot cancel an exact job the
		// moment it is queued.
		bool exact = false;

		bool operator==(const Inputs& other) const;
		bool operator!=(const Inputs& other) const { return !(*this == other); }
	};

	static Inputs Extract(const Configuration& cfg);

	void WorkerMain();
	// Runs one job, calling Publish() with partial results as stages finish so
	// the viewer fills in progressively. Abandons early when superseded.
	void Compute(const Inputs& inputs, std::uint64_t generation);
	void Publish(const PreviewResult& result);

	std::thread worker_;
	mutable std::mutex mutex_;
	std::condition_variable wake_;

	Inputs pending_;         // what the UI last asked for
	Inputs in_flight_;       // what the worker is computing
	bool has_pending_ = false;
	bool stopping_ = false;
	std::atomic<bool> busy_{false};
	std::atomic<std::uint64_t> generation_{0};
	// Bumped when a newer request arrives, so the worker can abandon its job.
	std::atomic<std::uint64_t> cancel_token_{0};
	// Sticky until one of the preview inputs actually changes.
	bool exact_pinned_ = false;

	PreviewResult ready_;
	bool has_ready_ = false;
	std::uint64_t revision_ = 0;
	std::uint64_t fetched_revision_ = 0;

	// Debounce: a request is only handed to the worker once the inputs have
	// been stable for a moment, so dragging a slider does not queue a job per
	// frame.
	double pending_since_ = 0.0;
};

} // namespace rc_live_ui
