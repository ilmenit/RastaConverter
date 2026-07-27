// The editor's model half: the session, the layers, the drawing operations,
// undo/redo, the textures they feed and the payload an apply hands back.
// Everything that puts pixels on screen lives in PaintEditorPanels.cpp.

#include "PaintEditor.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "LiveTheme.h"
#include "RecentRuns.h"
#include "TargetPicture.h"
#include "rgb.h"

namespace rc_live_ui {

namespace {

// Per-pixel distance between two RGBA buffers, mapped blue -> red. The dashboard
// already owns both pictures, so this costs one pass over 38 400 pixels.
std::uint32_t HeatColor(std::uint32_t a, std::uint32_t b)
{
	const int dr = static_cast<int>((a & 0xFF) - (b & 0xFF));
	const int dg = static_cast<int>(((a >> 8) & 0xFF) - ((b >> 8) & 0xFF));
	const int db = static_cast<int>(((a >> 16) & 0xFF) - ((b >> 16) & 0xFF));
	const float error = std::sqrt(static_cast<float>(dr * dr + dg * dg + db * db))
		/ 441.7f;
	const float t = std::min(1.0f, error * 2.4f);
	const std::uint32_t r = static_cast<std::uint32_t>(255.0f * std::min(1.0f, t * 2.0f));
	const std::uint32_t g = static_cast<std::uint32_t>(180.0f * (1.0f - std::abs(t - 0.5f) * 2.0f));
	const std::uint32_t bl = static_cast<std::uint32_t>(255.0f * std::max(0.0f, 1.0f - t * 2.0f));
	return r | (g << 8) | (bl << 16) | 0xFF000000u;
}

} // namespace

PaintEditor::PaintEditor(SDL_Renderer* renderer) : renderer_(renderer) {}

PaintEditor::~PaintEditor()
{
	if (reference_texture_ != nullptr)
		SDL_DestroyTexture(reference_texture_);
	if (layer_texture_ != nullptr)
		SDL_DestroyTexture(layer_texture_);
}

std::vector<unsigned char>& PaintEditor::layer()
{
	return target_ == Target::Mask ? mask_values_ : destination_values_;
}

const std::vector<unsigned char>& PaintEditor::layer() const
{
	return target_ == Target::Mask ? mask_values_ : destination_values_;
}

const std::vector<unsigned char>& PaintEditor::baseline() const
{
	return target_ == Target::Mask ? mask_baseline_ : destination_baseline_;
}

void PaintEditor::Open(Target target)
{
	target_ = target;
	active_ = true;
	mask_baseline_ = mask_values_;
	destination_baseline_ = destination_values_;
	parameter_baseline_ = parameters_;
	history_.clear();
	redo_.clear();
	active_changes_.clear();
	stroke_active_ = false;
	tool_ = Tool::Brush;
	if (target_ == Target::Mask) {
		primary_ = 192;
		secondary_ = 0;
		reference_ = 1;
	} else {
		primary_ = layer().empty() ? 0 : layer()[0];
		secondary_ = 0;
		reference_ = 2;
	}
	layer_dirty_ = true;
	reference_dirty_ = true;
	branch_label_.clear();
}

void PaintEditor::Close()
{
	active_ = false;
	// Uncommitted work never survives the session; the caller decides whether
	// it was applied first.
	mask_values_ = mask_baseline_;
	destination_values_ = destination_baseline_;
	parameters_ = parameter_baseline_;
	history_.clear();
	redo_.clear();
	active_changes_.clear();
	stroke_active_ = false;
}

void PaintEditor::SetContent(const PreviewResult& content)
{
	content_ = content;
	++content_revision_;
	reference_dirty_ = true;
	if (content_.source.valid()) {
		width_ = content_.source.width;
		height_ = content_.source.height;
	}
}

void PaintEditor::SetMaskLayer(const std::vector<unsigned char>& values,
	int width, int height)
{
	if (values.empty())
		return;
	if (width > 0 && height > 0) {
		width_ = width;
		height_ = height;
	}
	mask_baseline_ = values;
	if (!active_ || target_ != Target::Mask) {
		mask_values_ = values;
		if (target_ == Target::Mask)
			layer_dirty_ = true;
	}
}

void PaintEditor::SetDestinationLayer(const std::vector<unsigned char>& values,
	int width, int height)
{
	if (values.empty())
		return;
	if (width > 0 && height > 0) {
		width_ = width;
		height_ = height;
	}
	destination_baseline_ = values;
	if (!active_ || target_ != Target::Destination) {
		destination_values_ = values;
		if (target_ == Target::Destination)
			layer_dirty_ = true;
	}
}

void PaintEditor::SetStats(const LiveStats& stats)
{
	stats_ = stats;
	if (!active_) {
		parameters_.mode = stats.details_mode.empty() ? "legacy" : stats.details_mode;
		parameters_.strength = stats.details_strength;
		parameters_.floor = stats.details_floor;
		parameters_.feather = static_cast<int>(stats.details_feather);
		parameters_.score = stats.details_score;
		parameter_baseline_ = parameters_;
	}
}

// ---- painting -------------------------------------------------------------

void PaintEditor::PaintPixel(int x, int y, unsigned char value)
{
	if (x < 0 || y < 0 || x >= width_ || y >= height_)
		return;
	const size_t offset = static_cast<size_t>(y) * width_ + x;
	std::vector<unsigned char>& values = layer();
	if (offset >= values.size())
		return;
	auto found = active_changes_.find(offset);
	if (found == active_changes_.end()) {
		GuiMaskPixelChange change;
		change.x = static_cast<unsigned>(x);
		change.y = static_cast<unsigned>(y);
		change.before = values[offset];
		change.after = value;
		active_changes_.emplace(offset, change);
	} else {
		found->second.after = value;
	}
	values[offset] = value;
	layer_dirty_ = true;
}

void PaintEditor::PaintOffset(int offset, unsigned char value)
{
	if (offset < 0 || width_ <= 0)
		return;
	PaintPixel(offset % width_, offset / width_, value);
}

// Brush size is a diameter in screen terms, not buffer terms. An Atari pixel is
// twice as wide as it is tall, so a footprint that spans N rows has to span
// half as many columns to look round - measuring in the buffer is what made the
// old brush a wide oval. The square brush is square on screen for the same
// reason.
bool PaintEditor::BrushCovers(int dx, int dy) const
{
	const float ry = std::max(0.5f, brush_size_ * 0.5f);
	const float rx = ry / kEditorPixelAspect;
	if (!round_brush_) {
		return std::abs(static_cast<float>(dx)) <= rx + 0.001f
			&& std::abs(static_cast<float>(dy)) <= ry + 0.001f;
	}
	const float nx = dx / rx;
	const float ny = dy / ry;
	return nx * nx + ny * ny <= 1.001f;
}

void PaintEditor::CollectStamp(int cx, int cy, std::vector<int>& out) const
{
	if (width_ <= 0 || height_ <= 0)
		return;
	const int reach = std::max(1, (brush_size_ + 1) / 2);
	for (int dy = -reach; dy <= reach; ++dy) {
		const int y = cy + dy;
		if (y < 0 || y >= height_)
			continue;
		for (int dx = -reach; dx <= reach; ++dx) {
			const int x = cx + dx;
			if (x < 0 || x >= width_ || !BrushCovers(dx, dy))
				continue;
			out.push_back(y * width_ + x);
		}
	}
}

void PaintEditor::CollectLine(int x0, int y0, int x1, int y1,
	std::vector<int>& out) const
{
	const int dx = x1 - x0;
	const int dy = y1 - y0;
	const int steps = std::max(1, std::max(std::abs(dx), std::abs(dy)));
	for (int step = 0; step <= steps; ++step)
		CollectStamp(x0 + dx * step / steps, y0 + dy * step / steps, out);
}

void PaintEditor::CollectShape(int x0, int y0, int x1, int y1,
	std::vector<int>& out) const
{
	const int left = std::min(x0, x1);
	const int right = std::max(x0, x1);
	const int top = std::min(y0, y1);
	const int bottom = std::max(y0, y1);
	auto cell = [&](int x, int y) {
		if (x >= 0 && y >= 0 && x < width_ && y < height_)
			out.push_back(y * width_ + x);
	};

	if (tool_ == Tool::Line) {
		CollectLine(x0, y0, x1, y1, out);
		return;
	}
	if (tool_ == Tool::Rectangle) {
		if (fill_shapes_) {
			for (int y = top; y <= bottom; ++y)
				for (int x = left; x <= right; ++x)
					cell(x, y);
		} else {
			// Through CollectLine, so the outline carries the brush width.
			CollectLine(left, top, right, top, out);
			CollectLine(right, top, right, bottom, out);
			CollectLine(right, bottom, left, bottom, out);
			CollectLine(left, bottom, left, top, out);
		}
		return;
	}
	if (tool_ != Tool::Ellipse)
		return;

	const double cx = (left + right) * 0.5;
	const double cy = (top + bottom) * 0.5;
	const double rx = std::max(0.5, (right - left) * 0.5);
	const double ry = std::max(0.5, (bottom - top) * 0.5);
	for (int y = top; y <= bottom; ++y)
		for (int x = left; x <= right; ++x) {
			const double nx = (x - cx) / rx;
			const double ny = (y - cy) / ry;
			const double d = nx * nx + ny * ny;
			if (fill_shapes_) {
				if (d <= 1.0)
					cell(x, y);
				continue;
			}
			// One cell either side of the curve, then stamped, so the outline
			// is as thick as the brush rather than hairline.
			const double edge = std::max(1.0 / rx, 1.0 / ry) * 1.6;
			if (std::abs(d - 1.0) <= edge)
				CollectStamp(x, y, out);
		}
}

void PaintEditor::Stamp(int cx, int cy, unsigned char value)
{
	stroke_cells_.clear();
	CollectStamp(cx, cy, stroke_cells_);
	for (int offset : stroke_cells_) {
		unsigned char painted = value;
		if (tool_ == Tool::Revert) {
			if (static_cast<size_t>(offset) < baseline().size())
				painted = baseline()[offset];
		}
		PaintOffset(offset, painted);
	}
}

void PaintEditor::StrokeLine(int x0, int y0, int x1, int y1, unsigned char value)
{
	const int dx = x1 - x0;
	const int dy = y1 - y0;
	const int steps = std::max(1, std::max(std::abs(dx), std::abs(dy)));
	for (int step = 0; step <= steps; ++step)
		Stamp(x0 + dx * step / steps, y0 + dy * step / steps, value);
}

void PaintEditor::StrokeShape(int x0, int y0, int x1, int y1, unsigned char value)
{
	stroke_cells_.clear();
	CollectShape(x0, y0, x1, y1, stroke_cells_);
	for (int offset : stroke_cells_)
		PaintOffset(offset, value);
}

void PaintEditor::BucketFill(int x, int y, unsigned char value)
{
	if (x < 0 || y < 0 || x >= width_ || y >= height_ || layer().empty())
		return;
	const unsigned char target = layer()[static_cast<size_t>(y) * width_ + x];
	if (target == value)
		return;
	std::vector<size_t> stack{static_cast<size_t>(y) * width_ + static_cast<size_t>(x)};
	while (!stack.empty()) {
		const size_t offset = stack.back();
		stack.pop_back();
		if (offset >= layer().size() || layer()[offset] != target)
			continue;
		const int px = static_cast<int>(offset % width_);
		const int py = static_cast<int>(offset / width_);
		PaintPixel(px, py, value);
		if (px > 0) stack.push_back(offset - 1);
		if (px + 1 < width_) stack.push_back(offset + 1);
		if (py > 0) stack.push_back(offset - width_);
		if (py + 1 < height_) stack.push_back(offset + width_);
	}
}

void PaintEditor::CommitStroke(const char* label)
{
	HistoryEntry entry;
	entry.label = label;
	entry.pixels.reserve(active_changes_.size());
	for (const auto& item : active_changes_)
		if (item.second.before != item.second.after)
			entry.pixels.push_back(item.second);
	active_changes_.clear();
	if (entry.pixels.empty())
		return;
	history_.push_back(std::move(entry));
	redo_.clear();
}

void PaintEditor::PushParameterChange(const MaskParameters& before, const char* label)
{
	if (before == parameters_)
		return;
	HistoryEntry entry;
	entry.label = label;
	entry.parameters = true;
	entry.before = before;
	entry.after = parameters_;
	history_.push_back(std::move(entry));
	redo_.clear();
}

void PaintEditor::Undo()
{
	if (history_.empty())
		return;
	HistoryEntry entry = history_.back();
	history_.pop_back();
	if (entry.parameters) {
		parameters_ = entry.before;
	} else {
		for (const GuiMaskPixelChange& pixel : entry.pixels) {
			const size_t offset = static_cast<size_t>(pixel.y) * width_ + pixel.x;
			if (offset < layer().size())
				layer()[offset] = pixel.before;
		}
		layer_dirty_ = true;
	}
	redo_.push_back(std::move(entry));
}

void PaintEditor::Redo()
{
	if (redo_.empty())
		return;
	HistoryEntry entry = redo_.back();
	redo_.pop_back();
	if (entry.parameters) {
		parameters_ = entry.after;
	} else {
		for (const GuiMaskPixelChange& pixel : entry.pixels) {
			const size_t offset = static_cast<size_t>(pixel.y) * width_ + pixel.x;
			if (offset < layer().size())
				layer()[offset] = pixel.after;
		}
		layer_dirty_ = true;
	}
	history_.push_back(std::move(entry));
}

std::vector<GuiMaskPixelChange> PaintEditor::PendingPixels() const
{
	std::vector<GuiMaskPixelChange> pixels;
	const std::vector<unsigned char>& current = layer();
	const std::vector<unsigned char>& original = baseline();
	if (current.size() != original.size() || width_ <= 0)
		return pixels;
	for (size_t offset = 0; offset < current.size(); ++offset) {
		if (current[offset] == original[offset])
			continue;
		GuiMaskPixelChange change;
		change.x = static_cast<unsigned>(offset % width_);
		change.y = static_cast<unsigned>(offset / width_);
		change.before = original[offset];
		change.after = current[offset];
		pixels.push_back(change);
	}
	return pixels;
}

bool PaintEditor::TakeApply(GuiEditorApply& request)
{
	if (!has_pending_)
		return false;
	request = std::move(pending_);
	pending_ = GuiEditorApply{};
	has_pending_ = false;
	return true;
}

// ---- textures -------------------------------------------------------------

void PaintEditor::RebuildReferenceTexture()
{
	const PreviewImage* image = nullptr;
	switch (reference_) {
	case 1: image = &content_.quantized; break;
	case 2: image = &content_.source; break;
	case 3: image = &content_.dithered; break;
	default: break;
	}
	const bool want_heat = heatmap_ && content_.dithered.valid()
		&& content_.quantized.valid()
		&& content_.dithered.pixels.size() == content_.quantized.pixels.size();
	if ((image == nullptr || !image->valid()) && !want_heat) {
		reference_dirty_ = false;
		return;
	}
	const int w = want_heat ? content_.dithered.width : image->width;
	const int h = want_heat ? content_.dithered.height : image->height;
	std::vector<std::uint32_t> pixels;
	if (image != nullptr && image->valid() && image->width == w && image->height == h)
		pixels = image->pixels;
	else
		pixels.assign(static_cast<size_t>(w) * h, 0xFF101217u);
	if (want_heat) {
		for (size_t i = 0; i < pixels.size() && i < content_.dithered.pixels.size(); ++i) {
			const std::uint32_t heat = HeatColor(content_.dithered.pixels[i],
				content_.quantized.pixels[i]);
			// Half-blended over the reference, so the picture stays legible.
			const std::uint32_t base = pixels[i];
			std::uint32_t mixed = 0xFF000000u;
			for (int channel = 0; channel < 3; ++channel) {
				const int shift = channel * 8;
				const int a = (base >> shift) & 0xFF;
				const int b = (heat >> shift) & 0xFF;
				mixed |= static_cast<std::uint32_t>((a + b * 2) / 3) << shift;
			}
			pixels[i] = mixed;
		}
	}

	if (reference_texture_ != nullptr
		&& (texture_width_ != w || texture_height_ != h)) {
		SDL_DestroyTexture(reference_texture_);
		reference_texture_ = nullptr;
	}
	if (reference_texture_ == nullptr) {
		reference_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING, w, h);
		if (reference_texture_ == nullptr)
			return;
		SDL_SetTextureScaleMode(reference_texture_, SDL_SCALEMODE_NEAREST);
	}
	SDL_UpdateTexture(reference_texture_, nullptr, pixels.data(),
		w * static_cast<int>(sizeof(std::uint32_t)));
	texture_width_ = w;
	texture_height_ = h;
	reference_dirty_ = false;
	drawn_reference_ = reference_;
	drawn_heatmap_ = heatmap_;
	drawn_revision_ = content_revision_;
}

void PaintEditor::RebuildLayerTexture()
{
	const std::vector<unsigned char>& values = layer();
	if (values.empty() || width_ <= 0 || height_ <= 0)
		return;
	std::vector<std::uint32_t> pixels(values.size());
	if (target_ == Target::Mask) {
		for (size_t i = 0; i < values.size(); ++i) {
			const std::uint32_t level = values[i];
			pixels[i] = level | (level << 8) | (level << 16) | (level << 24);
		}
	} else {
		for (size_t i = 0; i < values.size(); ++i) {
			const rgb& color = atari_palette[values[i] % 128];
			pixels[i] = static_cast<std::uint32_t>(color.r)
				| (static_cast<std::uint32_t>(color.g) << 8)
				| (static_cast<std::uint32_t>(color.b) << 16) | 0xFF000000u;
		}
	}
	if (layer_texture_ != nullptr) {
		float tw = 0.0f;
		float th = 0.0f;
		SDL_GetTextureSize(layer_texture_, &tw, &th);
		if (static_cast<int>(tw) != width_ || static_cast<int>(th) != height_) {
			SDL_DestroyTexture(layer_texture_);
			layer_texture_ = nullptr;
		}
	}
	if (layer_texture_ == nullptr) {
		layer_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING, width_, height_);
		if (layer_texture_ == nullptr)
			return;
		SDL_SetTextureScaleMode(layer_texture_, SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(layer_texture_, SDL_BLENDMODE_BLEND);
	}
	SDL_UpdateTexture(layer_texture_, nullptr, pixels.data(),
		width_ * static_cast<int>(sizeof(std::uint32_t)));
	layer_dirty_ = false;
}

void PaintEditor::BuildApply(bool branch)
{
	pending_ = GuiEditorApply{};
	pending_.destination = target_ == Target::Destination;
	pending_.branch = branch;
	pending_.pixels = PendingPixels();
	if (target_ == Target::Mask) {
		// Parameters ride along with the pixels: one retarget covers both.
		pending_.has_mask_parameters = true;
		pending_.details_mode = parameters_.mode;
		pending_.details_strength = parameters_.strength;
		pending_.details_floor = parameters_.floor;
		pending_.details_feather = static_cast<unsigned>(parameters_.feather);
		pending_.details_score = parameters_.score;
	}
	has_pending_ = true;
}

void PaintEditor::TestStroke(unsigned char value)
{
	if (!active_ || layer().empty())
		return;
	PaintPixel(0, 0, value);
	CommitStroke("test");
}

void PaintEditor::TestRequestApply(bool branch)
{
	BuildApply(branch);
}
} // namespace rc_live_ui
