#include "RecentGallery.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "Desktop.h"
#include "LiveTheme.h"

namespace rc_live_ui {

namespace {

// Card sizing. The grid reflows to whatever width the column has.
constexpr float kCardWidth = 258.0f;
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

// A line of text that behaves like a link: underlined and hand-cursored on
// hover, clickable, with an explanation of where it leads. Cards are full of
// names of real things on disk - a folder, a picture - and the shortest path to
// them is the name itself.
bool LinkLine(const char* text, ImU32 colour, const char* tooltip)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(colour));
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
	if (!ImGui::IsItemHovered())
		return false;
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y - 1.0f),
		ImVec2(max.x, max.y - 1.0f), colour);
	ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	ImGui::SetTooltip("%s", tooltip);
	return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
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
	xex_current_.assign(runs_.size(), 0);
	for (size_t i = 0; i < runs_.size(); ++i)
		xex_current_[i] = RunXexIsCurrent(runs_[i].output_base) ? 1 : 0;
	loaded_ = true;
}

void RecentGallery::Open(const std::string& path, bool folder)
{
	std::string error;
	const bool opened = folder ? ShowInFileManager(path, &error)
		: OpenWithDesktop(path, &error);
	if (opened) {
		xex_failed_ = false;
		xex_message_.clear();
		return;
	}
	// Same channel as a failed assembly: one line above the grid, because a
	// card has no room to explain itself and this is rare.
	xex_failed_ = true;
	xex_message_ = "Could not open " + FileName(path) + ": " + error;
	xex_detail_ = path;
}

void RecentGallery::StartXexBuild(size_t index)
{
	if (index >= runs_.size() || xex_future_.valid())
		return;
	xex_index_ = index;
	xex_folder_ = runs_[index].folder;
	xex_failed_ = false;
	xex_message_.clear();
	const std::string base = runs_[index].output_base;
	xex_future_ = std::async(std::launch::async,
		[base]() { return BuildRunXex(base); });
}

void RecentGallery::PollXexBuild()
{
	if (!xex_future_.valid())
		return;
	if (xex_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;
	const XexBuildResult result = xex_future_.get();
	xex_future_ = std::future<XexBuildResult>();
	if (!result.ok) {
		xex_failed_ = true;
		xex_message_ = "Could not build the executable for "
			+ FileName(xex_folder_) + ".";
		xex_detail_ = result.log;
		return;
	}
	if (xex_index_ < xex_current_.size())
		xex_current_[xex_index_] = 1;
	// Straight into whatever plays .xex files. Building one and then leaving it
	// sitting in a folder is not why anyone pressed the button.
	Open(result.xex_path, /*folder*/ false);
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

// Forgetting a list is cheap; deleting the folders it names is not. Both live
// behind the same confirmation, with the deletion off by default and the button
// saying which of the two is about to happen.
void RecentGallery::DrawClearPopup()
{
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f));
	if (!ImGui::BeginPopupModal("clear_recent", nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
		return;

	ImGui::PushTextWrapPos(0.0f);
	ImGui::Text("Clear the list of %d recent conversions?",
		static_cast<int>(runs_.size()));
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted("The list is only a history. Clearing it leaves every "
		"conversion where it is on disk.");
	ImGui::PopStyleColor();
	ImGui::Spacing();

	ImGui::Checkbox("Remove also output folders", &clear_folders_);
	if (clear_folders_) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kDanger));
		ImGui::TextUnformatted("This deletes the rc-... folders and everything "
			"in them - pictures, raster programs, saved state. It cannot be "
			"undone.");
		ImGui::PopStyleColor();
	}
	ImGui::PopTextWrapPos();
	ImGui::Spacing();

	const float width = (ImGui::GetContentRegionAvail().x
		- ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Cancel", ImVec2(width, 0.0f)))
		ImGui::CloseCurrentPopup();
	ImGui::SameLine();
	if (clear_folders_) {
		ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kDanger));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::ToVec4(theme::kDanger));
	}
	const char* confirm = clear_folders_ ? "Delete folders" : "Clear list";
	if (ImGui::Button(confirm, ImVec2(width, 0.0f))) {
		size_t skipped = 0;
		const size_t removed = ClearRecentRuns(clear_folders_, &skipped);
		if (clear_folders_) {
			char message[192];
			std::snprintf(message, sizeof(message),
				"Removed %d run folder%s%s.", static_cast<int>(removed),
				removed == 1 ? "" : "s",
				skipped > 0 ? " - some were left alone, see the tooltip" : "");
			xex_failed_ = skipped > 0;
			xex_message_ = message;
			xex_detail_ = skipped > 0
				? std::string("Folders not named rc-... are forgotten but never "
					"deleted, in case the history pointed somewhere unexpected.")
				: std::string();
		}
		Refresh();
		ImGui::CloseCurrentPopup();
	}
	if (clear_folders_)
		ImGui::PopStyleColor(2);
	ImGui::EndPopup();
}

RecentGallery::Result RecentGallery::Draw(bool closable)
{
	Result result;
	if (!loaded_)
		Refresh();
	PollXexBuild();

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("Recent conversions");
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::Text("(%d)", static_cast<int>(runs_.size()));
	ImGui::PopStyleColor();

	{
		const float buttons = ImGui::CalcTextSize("Clear all").x
			+ ImGui::CalcTextSize("Refresh").x
			+ (closable ? ImGui::CalcTextSize("Close").x : 0.0f)
			+ ImGui::GetStyle().FramePadding.x * (closable ? 6.0f : 4.0f)
			+ ImGui::GetStyle().ItemSpacing.x * 3.0f;
		const float right = ImGui::GetContentRegionMax().x - buttons;
		if (right > ImGui::GetCursorPosX())
			ImGui::SameLine(right);
		else
			ImGui::SameLine(0.0f, 12.0f);
		ImGui::BeginDisabled(runs_.empty());
		if (ImGui::SmallButton("Clear all")) {
			// The destructive half starts off on every open, so it can only be
			// reached deliberately and never inherited from a previous visit.
			clear_folders_ = false;
			ImGui::OpenPopup("clear_recent");
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip(runs_.empty()
				? "Nothing to clear."
				: "Empty this list. The conversions themselves stay on disk "
				  "unless you ask for them too.");
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Refresh"))
			Refresh();
		if (closable) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Close"))
				result.action = Action::Dismiss;
		}
		DrawClearPopup();
	}

	if (xex_future_.valid()) {
		ImGui::Spacing();
		InlineNote(("Assembling " + FileName(xex_folder_)
			+ " with the bundled MADS...").c_str(), theme::kInfo);
	} else if (xex_failed_) {
		ImGui::Spacing();
		InlineNote(xex_message_.c_str(), theme::kDanger);
		if (ImGui::IsItemHovered() && !xex_detail_.empty()) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
			ImGui::TextUnformatted(xex_detail_.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy details"))
			ImGui::SetClipboardText(xex_detail_.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Dismiss")) {
			xex_failed_ = false;
			xex_message_.clear();
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
		const char* mode_badge = run.text_mode ? "TEXT" : "GFX";
		const ImVec2 badge_text = ImGui::CalcTextSize(mode_badge);
		const ImVec2 badge_padding(7.0f, 3.0f);
		const ImVec2 badge_max(
			band_min.x + band.x - 7.0f, band_min.y + 7.0f
				+ badge_text.y + badge_padding.y * 2.0f);
		const ImVec2 badge_min(
			badge_max.x - badge_text.x - badge_padding.x * 2.0f,
			band_min.y + 7.0f);
		draw->AddRectFilled(badge_min, badge_max,
			run.text_mode ? theme::kBadgeText : theme::kBadgeGfx, 5.0f);
		draw->AddText(ImVec2(badge_min.x + badge_padding.x,
			badge_min.y + badge_padding.y), theme::kTextStrong, mode_badge);
		ImGui::Dummy(band);
		// The picture is the natural thing to point at when asking "what were
		// the settings here?", and the whole recipe is one line of text.
		if (ImGui::IsItemHovered()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				Open(run.output_base, /*folder*/ false);
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
			ImGui::TextUnformatted("Click to open this picture in your image viewer.");
			ImGui::PopStyleColor();
			ImGui::Spacing();
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

		if (LinkLine(run.label.c_str(), theme::kTextStrong,
				(run.folder + "\n\nClick to open this folder in your file manager.").c_str()))
			Open(run.folder, /*folder*/ true);
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

		if (run.input_file.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
			ImGui::TextUnformatted("source unknown");
			ImGui::PopStyleColor();
		} else if (LinkLine(FileName(run.input_file).c_str(), theme::kTextFaint,
				(run.input_file
					+ "\n\nClick to open the source image in your editor.").c_str())) {
			Open(run.input_file, /*folder*/ false);
		}

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
		const float third = (ImGui::GetContentRegionAvail().x
			- ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
		const float half = third;
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

		// The whole point of converting a picture is to see it on an Atari, and
		// everything the bundled generator needs is already in this folder. One
		// button: assemble if needed, then hand it to whatever opens .xex.
		ImGui::SameLine();
		const bool ready = i < xex_current_.size() && xex_current_[i] != 0;
		const bool busy = xex_future_.valid();
		const bool building = busy && xex_folder_ == run.folder;
		if (ready) {
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_Border, theme::ToVec4(theme::kAccent));
		}
		ImGui::BeginDisabled(busy || run.output_base.empty());
		if (ImGui::Button(building ? "..." : "XEX", ImVec2(third, 0.0f))) {
			// Shift forces a rebuild; otherwise an executable that is already
			// newer than the raster program just opens, which is instant.
			if (ready && !ImGui::GetIO().KeyShift)
				Open(RunXexPath(run.output_base), /*folder*/ false);
			else
				StartXexBuild(i);
		}
		ImGui::EndDisabled();
		if (ready)
			ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
			if (run.output_base.empty()) {
				ImGui::TextUnformatted("This run did not record where it wrote its "
					"files, so there is nothing to assemble.");
			} else if (ready) {
				ImGui::TextUnformatted("Open the Atari executable this run produced.");
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
				ImGui::TextUnformatted(RunXexPath(run.output_base).c_str());
				ImGui::TextUnformatted("Shift-click to assemble it again.");
				ImGui::PopStyleColor();
			} else {
				ImGui::TextUnformatted("Assemble this run into an Atari executable "
					"with the bundled MADS, then open it with whatever your system "
					"uses for .xex files - an emulator, usually.");
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
				ImGui::TextUnformatted(RunXexPath(run.output_base).c_str());
				ImGui::PopStyleColor();
			}
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		// Everything the card can do, in one place, for anyone who did not
		// notice that the names are links.
		if (ImGui::BeginPopupContextWindow("card_menu")) {
			if (ImGui::MenuItem("Open run folder"))
				Open(run.folder, /*folder*/ true);
			if (ImGui::MenuItem("Open picture", nullptr, false,
					!run.output_base.empty()))
				Open(run.output_base, /*folder*/ false);
			if (ImGui::MenuItem("Open source image", nullptr, false,
					!run.input_file.empty()))
				Open(run.input_file, /*folder*/ false);
			ImGui::Separator();
			if (ImGui::MenuItem(ready ? "Open .xex" : "Build .xex", nullptr, false,
					!busy && !run.output_base.empty())) {
				if (ready)
					Open(RunXexPath(run.output_base), /*folder*/ false);
				else
					StartXexBuild(i);
			}
			if (ImGui::MenuItem("Rebuild .xex", nullptr, false,
					!busy && !run.output_base.empty()))
				StartXexBuild(i);
			ImGui::Separator();
			if (ImGui::MenuItem("Copy command line", nullptr, false,
					!run.command_line.empty()))
				ImGui::SetClipboardText(run.command_line.c_str());
			ImGui::EndPopup();
		}

		ImGui::EndChild();
		ImGui::EndGroup();
		ImGui::PopID();
	}

	ImGui::EndChild();
	return result;
}

} // namespace rc_live_ui
