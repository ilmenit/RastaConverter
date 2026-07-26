#pragma once

#include <atomic>
#include <cstdint>

namespace rasta::live {

struct TunableSnapshot {
	std::uint64_t max_evaluations;
	int save_period;
	double objective_weight;
	double details_strength;
	bool paused;
};

// UI/optimizer bridge. It intentionally contains no GUI types and uses atomics
// because widgets and evaluator workers do not share a synchronization loop.
class LiveTunables {
public:
	std::atomic<std::uint64_t> max_evaluations{0};
	std::atomic<int> save_period{0};
	std::atomic<double> objective_weight{0.1};
	std::atomic<double> details_strength{0.5};
	std::atomic<bool> paused{false};

	TunableSnapshot Load() const noexcept
	{
		return {
			max_evaluations.load(std::memory_order_relaxed),
			save_period.load(std::memory_order_relaxed),
			objective_weight.load(std::memory_order_relaxed),
			details_strength.load(std::memory_order_relaxed),
			paused.load(std::memory_order_acquire)
		};
	}
};

} // namespace rasta::live
