#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rasta::live {

struct ProgressSample {
	std::uint64_t evaluations;
	double normalized_distance;
};

class ProgressHistory {
public:
	explicit ProgressHistory(std::size_t capacity = 2048);
	void Push(ProgressSample sample);
	void Clear();
	std::vector<ProgressSample> Samples() const;
	std::size_t Capacity() const noexcept { return samples_.size(); }
	std::size_t Size() const noexcept { return size_; }

private:
	std::vector<ProgressSample> samples_;
	std::size_t next_ = 0;
	std::size_t size_ = 0;
};

} // namespace rasta::live
