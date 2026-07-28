#include "Utf8Path.h"

#if defined(_WIN32)
#include <windows.h>
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
