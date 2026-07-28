#include "Utf8Path.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

std::filesystem::path Utf8Path(const std::string& path)
{
	return std::filesystem::u8path(path);
}

std::string Utf8String(const std::filesystem::path& path)
{
	return path.u8string();
}

FILE* FopenUtf8(const std::string& path, const char* mode)
{
#if defined(_WIN32)
	const std::filesystem::path native = Utf8Path(path);
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		mode, -1, nullptr, 0);
	if (length <= 0)
		return nullptr;
	std::wstring wide_mode(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1,
		wide_mode.data(), length);
	return _wfopen(native.c_str(), wide_mode.c_str());
#else
	return std::fopen(path.c_str(), mode);
#endif
}

#if defined(_WIN32)
bool LoadWindowsUtf8Arguments(std::vector<std::string>& arguments)
{
	int native_count = 0;
	wchar_t** native = CommandLineToArgvW(GetCommandLineW(), &native_count);
	if (native == nullptr)
		return false;
	arguments.clear();
	arguments.reserve(static_cast<std::size_t>(native_count));
	for (int index = 0; index < native_count; ++index) {
		const int byte_count = WideCharToMultiByte(CP_UTF8,
			WC_ERR_INVALID_CHARS, native[index], -1, nullptr, 0,
			nullptr, nullptr);
		if (byte_count <= 0) {
			LocalFree(native);
			arguments.clear();
			return false;
		}
		std::string value(static_cast<std::size_t>(byte_count), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native[index],
			-1, value.data(), byte_count, nullptr, nullptr);
		value.pop_back();
		arguments.push_back(std::move(value));
	}
	LocalFree(native);
	return true;
}
#endif
