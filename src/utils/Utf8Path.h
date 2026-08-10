#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// Paths entering the program from SDL and the command line are UTF-8. On
// Windows, std::filesystem::u8path converts them to native UTF-16; passing the
// same bytes to fopen/ifstream directly would instead use the active ANSI code
// page and reject perfectly valid names selected by the native file dialog.
std::filesystem::path Utf8Path(const std::string& path);
std::string Utf8String(const std::filesystem::path& path);
std::string Utf8FileName(const std::string& path);
std::string Utf8ParentPath(const std::string& path);
std::string Utf8JoinPath(const std::string& parent, const std::string& child);
std::string Utf8AbsolutePath(const std::string& path);
FILE* FopenUtf8(const std::string& path, const char* mode);

#if defined(_WIN32)
bool LoadWindowsUtf8Arguments(std::vector<std::string>& arguments);
#endif
