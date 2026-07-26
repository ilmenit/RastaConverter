#include "ProgressHistory.h"

#include <algorithm>

namespace rasta::live {

ProgressHistory::ProgressHistory(std::size_t capacity)
	: samples_(std::max<std::size_t>(1, capacity))
{
}

void ProgressHistory::Push(ProgressSample sample)
{
	samples_[next_] = sample;
	next_ = (next_ + 1) % samples_.size();
	size_ = std::min(size_ + 1, samples_.size());
}

void ProgressHistory::Clear()
{
	next_ = 0;
	size_ = 0;
}

std::vector<ProgressSample> ProgressHistory::Samples() const
{
	std::vector<ProgressSample> result;
	result.reserve(size_);
	const std::size_t first = (next_ + samples_.size() - size_) % samples_.size();
	for (std::size_t i = 0; i < size_; ++i)
		result.push_back(samples_[(first + i) % samples_.size()]);
	return result;
}

} // namespace rasta::live
