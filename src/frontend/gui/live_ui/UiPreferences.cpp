#include "UiPreferences.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace rc_live_ui {
namespace {

constexpr int kMinimumWindowWidth = 800;
constexpr int kMaximumWindowWidth = 7680;
constexpr int kMinimumWindowHeight = 600;
constexpr int kMaximumWindowHeight = 4320;
constexpr float kMinimumFormWidth = 320.0f;
constexpr float kMaximumFormWidth = 2400.0f;
constexpr unsigned kValidSectionMask = 0x1Fu;

std::string PreferencePath()
{
	char* pref = SDL_GetPrefPath("RastaConverter", "RastaConverter");
	if (pref == nullptr)
		return std::string();
	const std::string path = std::string(pref) + "ui-preferences.txt";
	SDL_free(pref);
	return path;
}

bool ParseInt(std::string_view text, int& value)
{
	const char* first = text.data();
	const char* last = first + text.size();
	const auto result = std::from_chars(first, last, value);
	return result.ec == std::errc() && result.ptr == last;
}

bool ParseFloat(std::string_view text, float& value)
{
	std::string copy(text);
	char* end = nullptr;
	const float parsed = std::strtof(copy.c_str(), &end);
	if (end == copy.c_str() || *end != '\0' || !std::isfinite(parsed))
		return false;
	value = parsed;
	return true;
}

void Clamp(UiPreferences& preferences)
{
	preferences.setup_window_width = std::clamp(
		preferences.setup_window_width, kMinimumWindowWidth, kMaximumWindowWidth);
	preferences.setup_window_height = std::clamp(
		preferences.setup_window_height, kMinimumWindowHeight, kMaximumWindowHeight);
	preferences.setup_form_width = std::clamp(
		preferences.setup_form_width, kMinimumFormWidth, kMaximumFormWidth);
	preferences.setup_open_sections &= kValidSectionMask;
}

} // namespace

UiPreferences ParseUiPreferences(std::string_view text)
{
	UiPreferences preferences;
	std::istringstream input{std::string(text)};
	std::string line;
	while (std::getline(input, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const size_t separator = line.find('=');
		if (separator == std::string::npos)
			continue;
		const std::string_view key(line.data(), separator);
		const std::string_view value(
			line.data() + separator + 1, line.size() - separator - 1);
		if (key == "run_subfolder")
		{
			if (value == "1" || value == "true")
				preferences.run_subfolder = true;
			else if (value == "0" || value == "false")
				preferences.run_subfolder = false;
		}
		else if (key == "setup_window_width")
		{
			ParseInt(value, preferences.setup_window_width);
		}
		else if (key == "setup_window_height")
		{
			ParseInt(value, preferences.setup_window_height);
		}
		else if (key == "setup_form_width")
		{
			ParseFloat(value, preferences.setup_form_width);
		}
		else if (key == "setup_only_modified")
		{
			if (value == "1" || value == "true")
				preferences.setup_only_modified = true;
			else if (value == "0" || value == "false")
				preferences.setup_only_modified = false;
		}
		else if (key == "setup_open_sections")
		{
			int mask = static_cast<int>(preferences.setup_open_sections);
			if (ParseInt(value, mask) && mask >= 0)
				preferences.setup_open_sections =
					static_cast<unsigned>(mask);
		}
	}
	Clamp(preferences);
	return preferences;
}

std::string SerializeUiPreferences(const UiPreferences& value)
{
	UiPreferences preferences = value;
	Clamp(preferences);
	char buffer[256];
	const int length = std::snprintf(buffer, sizeof(buffer),
		"version=1\n"
		"run_subfolder=%d\n"
		"setup_window_width=%d\n"
		"setup_window_height=%d\n"
		"setup_form_width=%.1f\n"
		"setup_only_modified=%d\n"
		"setup_open_sections=%u\n",
		preferences.run_subfolder ? 1 : 0,
		preferences.setup_window_width,
		preferences.setup_window_height,
		static_cast<double>(preferences.setup_form_width),
		preferences.setup_only_modified ? 1 : 0,
		preferences.setup_open_sections);
	return length > 0 ? std::string(buffer, static_cast<size_t>(length))
		: std::string();
}

UiPreferences LoadUiPreferences()
{
	const std::string path = PreferencePath();
	if (path.empty())
		return UiPreferences{};
	size_t size = 0;
	void* data = SDL_LoadFile(path.c_str(), &size);
	if (data == nullptr)
		return UiPreferences{};
	const std::string text(static_cast<const char*>(data), size);
	SDL_free(data);
	return ParseUiPreferences(text);
}

bool SaveUiPreferences(const UiPreferences& preferences)
{
	const std::string path = PreferencePath();
	if (path.empty())
		return false;
	const std::string text = SerializeUiPreferences(preferences);
	return !text.empty()
		&& SDL_SaveFile(path.c_str(), text.data(), text.size());
}

} // namespace rc_live_ui
