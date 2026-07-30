#include "LiveUI.h"

#include "Dashboard.h"
#include "LiveTheme.h"
#include "SetupScreen.h"
#include "config.h"

#if defined(__APPLE__)
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif
#include <FreeImage.h>
#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <vector>
#include <deque>

namespace rc_live_ui {
namespace {

bool BeginImGui(SDL_Window* window, SDL_Renderer* renderer)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;
	const float pixel_density = DetectPixelDensity(window);
	LoadFonts(pixel_density);
	ApplyTheme();
	return ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) &&
		ImGui_ImplSDLRenderer3_Init(renderer);
}

void EndImGui()
{
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

PreviewStage StageOf(ImageSlot slot)
{
	switch (slot) {
	case ImageSlot::Source: return PreviewStage::Source;
	case ImageSlot::Target: return PreviewStage::Quantized;
	case ImageSlot::Output:
	default:                return PreviewStage::Dithered;
	}
}

} // namespace

bool RunSetup(Configuration& cfg, bool show_recent)
{
	return RunSetupScreen(cfg, show_recent);
}

struct Overlay::Impl {
	SDL_Window* window = nullptr;
	SDL_Renderer* renderer = nullptr;
	bool initialized = false;
	std::unique_ptr<Dashboard> dashboard;
	std::deque<LiveCommand> pending_commands;
	// Scratch buffer reused for bitmap conversion, so a periodic refresh does
	// not churn the allocator.
	std::vector<std::uint32_t> scratch;
};

Overlay::Overlay() : impl_(new Impl) {}

Overlay::~Overlay()
{
	if (impl_->initialized) {
		impl_->dashboard.reset();
		EndImGui();
	}
}

bool Overlay::Initialize(SDL_Window* window, SDL_Renderer* renderer)
{
	impl_->window = window;
	impl_->renderer = renderer;
	impl_->initialized = BeginImGui(window, renderer);
	if (impl_->initialized)
		impl_->dashboard = std::make_unique<Dashboard>(renderer);
	return impl_->initialized;
}

void Overlay::ProcessEvent(const SDL_Event& event)
{
	if (impl_->initialized)
		ImGui_ImplSDL3_ProcessEvent(&event);
}

void Overlay::PublishStats(const LiveStats& stats)
{
	if (impl_->dashboard)
		impl_->dashboard->SetStats(stats);
}

void Overlay::PublishBitmap(ImageSlot slot, FIBITMAP* bitmap)
{
	if (!impl_->dashboard || bitmap == nullptr)
		return;
	const int width = static_cast<int>(FreeImage_GetWidth(bitmap));
	const int height = static_cast<int>(FreeImage_GetHeight(bitmap));
	if (width <= 0 || height <= 0)
		return;

	FIBITMAP* converted = nullptr;
	FIBITMAP* source = bitmap;
	if (FreeImage_GetBPP(bitmap) != 24) {
		converted = FreeImage_ConvertTo24Bits(bitmap);
		if (converted == nullptr)
			return;
		source = converted;
	}

	impl_->scratch.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	for (int y = 0; y < height; ++y) {
		// No vertical flip here. The converter reads its source with
		// FreeImage_GetPixelColor(x, y) and writes every result back with
		// FreeImage_SetPixelColor(x, y), so all of its bitmaps share one
		// scanline-index space; scanline y is display row y, which is exactly
		// what the legacy display assumes. Flipping produced an upside-down
		// picture in the dashboard.
		const BYTE* row = FreeImage_GetScanLine(source, y);
		for (int x = 0; x < width; ++x) {
			const BYTE* pixel = row + x * 3;
			impl_->scratch[static_cast<size_t>(y) * width + x] =
				static_cast<std::uint32_t>(pixel[FI_RGBA_RED])
				| (static_cast<std::uint32_t>(pixel[FI_RGBA_GREEN]) << 8)
				| (static_cast<std::uint32_t>(pixel[FI_RGBA_BLUE]) << 16)
				| 0xFF000000u;
		}
	}
	impl_->dashboard->SetImage(StageOf(slot), width, height, impl_->scratch.data());

	if (converted != nullptr)
		FreeImage_Unload(converted);
}

void Overlay::PublishDetailsMask(const unsigned char* values,
	const unsigned char* editable_values, int width, int height, bool active)
{
	if (!impl_->dashboard)
		return;
	PreviewImage mask;
	if (values != nullptr && width > 0 && height > 0) {
		mask.width = width;
		mask.height = height;
		mask.pixels.resize(static_cast<size_t>(width) * height);
		for (size_t i = 0; i < mask.pixels.size(); ++i) {
			// Grey level in RGB, and the same value as alpha so unweighted
			// regions stay out of the way when blended.
			const std::uint32_t level = values[i];
			mask.pixels[i] = level | (level << 8) | (level << 16) | (level << 24);
		}
	}
	std::vector<unsigned char> editable;
	if (editable_values != nullptr && width > 0 && height > 0)
		editable.assign(editable_values,
			editable_values + static_cast<size_t>(width) * height);
	impl_->dashboard->SetMask(mask, editable, active);
}

bool Overlay::TakeEditorApply(GuiEditorApply& request)
{
	return impl_->dashboard && impl_->dashboard->TakeEditorApply(request);
}

bool Overlay::EditorWantsDestination() const
{
	return impl_->dashboard && impl_->dashboard->EditorWantsDestination();
}

void Overlay::PublishDestinationLayer(const unsigned char* palette_indices,
	int width, int height)
{
	if (!impl_->dashboard || palette_indices == nullptr || width <= 0 || height <= 0)
		return;
	impl_->dashboard->SetDestinationLayer(std::vector<unsigned char>(
		palette_indices, palette_indices + static_cast<size_t>(width) * height),
		width, height);
}

LiveCommand Overlay::TakeCommand()
{
	if (impl_->pending_commands.empty())
		return LiveCommand::None;
	const LiveCommand command = impl_->pending_commands.front();
	impl_->pending_commands.pop_front();
	return command;
}

void Overlay::Render()
{
	if (!impl_->initialized || !impl_->dashboard)
		return;
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	const LiveCommand command = impl_->dashboard->Draw();
	if (command != LiveCommand::None)
		impl_->pending_commands.push_back(command);
	ImGui::Render();
	ApplyRenderScale(impl_->renderer, DetectPixelDensity(impl_->window));
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), impl_->renderer);
}

bool Overlay::WantsKeyboard() const
{
	return impl_->initialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool Overlay::WantsMouse() const
{
	return impl_->initialized && ImGui::GetIO().WantCaptureMouse;
}

} // namespace rc_live_ui
