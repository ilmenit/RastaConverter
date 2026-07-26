#include "RecentGallery.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "LiveTheme.h"

namespace rc_live_ui {

namespace {

// Card sizing. The grid reflows to whatever width the column has.
constexpr float kCardWidth = 232.0f;
constexpr float kThumbHeight = 116.0f;

std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string WhenText(std::int64_t seconds)
{
	if (seconds <= 0)
		return std::string();
	const std::time_t when = static_cast<std::time_t>(seconds);
	std::tm local{};
#if defined(_WIN32)
	localtime_s(&local, &when);
#else
	localtime_r(&when, &local);
#endif
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
	return buffer;
}

std::string Magnitude(unsigned long long value)
{
	char buffer[32];
	if (value >= 1000000000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.2fG", value / 1e9);
	else if (value >= 1000000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.1fM", value / 1e6);
	else if (value >= 1000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.1fk", value / 1e3);
	else
		std::snprintf(buffer, sizeof(buffer), "%llu", value);
	return buffer;
}

} // namespace

RecentGallery::RecentGallery(SDL_Renderer* renderer) : renderer_(renderer) {}

RecentGallery::~RecentGallery()
{
	for (SDL_Texture* texture : textures_) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
}

void RecentGallery::Refresh()
{
	for (SDL_Texture* texture : textures_) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
	textures_.clear();
	runs_ = LoadRecentRuns(/*load_thumbnails*/ true);
	textures_.assign(runs_.size(), nullptr);
	loaded_ = true;
}

SDL_Texture* RecentGallery::TextureFor(size_t index)
{
	if (index >= runs_.size())
		return nullptr;
	if (textures_[index] != nullptr)
		return textures_[index];
	const PreviewImage& image = runs_[index].thumbnail;
	if (!image.valid())
		return nullptr;
	SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (texture == nullptr)
		return nullptr;
	SDL_UpdateTexture(texture, nullptr, image.pixels.data(),
		image.width * static_cast<int>(sizeof(std::uint32_t)));
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	textures_[index] = texture;
	return texture;
}

RecentGallery::Result RecentGallery::Draw(bool closable)
{
	Result result;
	if (!loaded_)
		Refresh();

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("Recent conversions");
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::Text("(%d)", static_cast<int>(runs_.size()));
	ImGui::PopStyleColor();

	{
		const float buttons = ImGui::CalcTextSize("Refresh").x
			+ (closable ? ImGui::CalcTextSize("Close").x : 0.0f)
			+ ImGui::GetStyle().FramePadding.x * (closable ? 4.0f : 2.0f)
			+ ImGui::GetStyle().ItemSpacing.x * 2.0f;
		const float right = ImGui::GetContentRegionMax().x - buttons;
		if (right > ImGui::GetCursorPosX())
			ImGui::SameLine(right);
		else
			ImGui::SameLine(0.0f, 12.0f);
		if (ImGui::SmallButton("Refresh"))
			Refresh();
		if (closable) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Close"))
				result.action = Action::Dismiss;
		}
	}

	if (runs_.empty()) {
		ImGui::Spacing();
		InlineNote("No previous conversions yet. Each run writes into its own "
			"rc-<image>-NNN folder beside the source image, and shows up here "
			"for as long as that folder exists.", theme::kTextMuted);
		return result;
	}

	ImGui::Spacing();
	ImGui::BeginChild("recent_scroll", ImVec2(0.0f, 0.0f));

	const float available = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1,
		static_cast<int>(available / (kCardWidth + ImGui::GetStyle().ItemSpacing.x)));
	const float card_width = std::max(kCardWidth,
		(available - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns);

	for (size_t i = 0; i < runs_.size(); ++i) {
		const RunSummary& run = runs_[i];
		if (i % static_cast<size_t>(columns) != 0)
			ImGui::SameLine();

		ImGui::PushID(static_cast<int>(i));
		ImGui::BeginGroup();

		// Thumbnail band, four text lines (folder, source, score, date), the
		// button row, and the child's own padding. Measured rather than
		// guessed so nothing is clipped at a different font size.
		const ImGuiStyle& style = ImGui::GetStyle();
		const float card_height = kThumbHeight
			+ ImGui::GetTextLineHeightWithSpacing() * 4.0f
			+ ImGui::GetFrameHeight()
			+ style.WindowPadding.y * 2.0f
			+ style.ItemSpacing.y * 2.0f;
		ImGui::BeginChild("card", ImVec2(card_width - 4.0f, card_height),
			ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);

		// Thumbnail, letterboxed into a fixed band so the grid stays even.
		SDL_Texture* texture = TextureFor(i);
		const ImVec2 band(ImGui::GetContentRegionAvail().x, kThumbHeight);
		const ImVec2 band_min = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(band_min, ImVec2(band_min.x + band.x, band_min.y + band.y),
			theme::kSurfaceLow, 4.0f);
		if (texture != nullptr && run.thumbnail.valid()) {
			const float scale = std::min(band.x / run.thumbnail.width,
				band.y / run.thumbnail.height);
			const ImVec2 size(run.thumbnail.width * scale, run.thumbnail.height * scale);
			const ImVec2 min(band_min.x + (band.x - size.x) * 0.5f,
				band_min.y + (band.y - size.y) * 0.5f);
			draw->AddImage(reinterpret_cast<ImTextureID>(texture), min,
				ImVec2(min.x + size.x, min.y + size.y));
		} else {
			const char* missing = "no picture yet";
			const ImVec2 text = ImGui::CalcTextSize(missing);
			draw->AddText(ImVec2(band_min.x + (band.x - text.x) * 0.5f,
				band_min.y + (band.y - text.y) * 0.5f), theme::kTextFaint, missing);
		}
		ImGui::Dummy(band);
		// The picture is the natural thing to point at when asking "what were
		// the settings here?", and the whole recipe is one line of text.
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
			if (run.command_line.empty()) {
				ImGui::TextUnformatted("This run recorded no settings, so only its "
					"picture and score are known.");
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
				ImGui::TextUnformatted("Settings");
				ImGui::PopStyleColor();
				ImGui::TextUnformatted(run.command_line.c_str());
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
		ImGui::TextUnformatted(run.label.c_str());
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", run.folder.c_str());
		if (run.mask_edited) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kWarning));
			if (run.snapshots > 0)
				ImGui::Text("edited · %u snap%s", run.snapshots,
					run.snapshots == 1 ? "" : "s");
			else
				ImGui::TextUnformatted("edited");
			ImGui::PopStyleColor();
		}

		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted(run.input_file.empty()
			? "source unknown" : FileName(run.input_file).c_str());
		if (ImGui::IsItemHovered() && !run.input_file.empty())
			ImGui::SetTooltip("%s", run.input_file.c_str());
		ImGui::PopStyleColor();

		// The two numbers that say whether this run is worth going back to.
		if (run.has_score) {
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kAccent));
			ImGui::Text("%.4f", run.score);
			ImGui::PopStyleColor();
			ImGui::SameLine();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
		if (run.evaluations > 0)
			ImGui::Text("%s evals", Magnitude(run.evaluations).c_str());
		else
			ImGui::TextUnformatted("not started");
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && run.has_score) {
			ImGui::SetTooltip("Normalized distance %.6f - lower is closer to the target.",
				run.score);
		}

		const std::string when = WhenText(run.modified_time);
		if (!when.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
			ImGui::TextUnformatted(when.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		const float half = (ImGui::GetContentRegionAvail().x
			- ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(!run.resumable);
		if (ImGui::Button("Continue", ImVec2(half, 0.0f))) {
			result.action = Action::Continue;
			result.run = run;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip(run.resumable
				? "Carry on optimizing this run, writing to the same folder."
				: "This folder has no saved raster program to resume from.");
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(run.command_line.empty() && run.input_file.empty());
		if (ImGui::Button("Reuse", ImVec2(half, 0.0f))) {
			result.action = Action::UseSettings;
			result.run = run;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
			if (run.command_line.empty()) {
				// Runs from before the settings were recorded, and runs whose
				// .opt header was never written, would otherwise look as if
				// they had restored something.
				ImGui::TextUnformatted("This run recorded no settings; Reuse loads "
					"its image only and leaves the current options alone.");
			} else {
				ImGui::TextUnformatted("Start a new conversion in a fresh folder with "
					"these settings, leaving this run untouched. Every option not "
					"listed goes back to its default.");
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
				ImGui::TextUnformatted(run.command_line.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		ImGui::EndChild();
		ImGui::EndGroup();
		ImGui::PopID();
	}

	ImGui::EndChild();
	return result;
}

} // namespace rc_live_ui
