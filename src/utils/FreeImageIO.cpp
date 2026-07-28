#include "FreeImageIO.h"

#include "Utf8Path.h"

FREE_IMAGE_FORMAT FreeImageFormatUtf8(const std::string& path)
{
#if defined(_WIN32)
	const std::filesystem::path native = Utf8Path(path);
	FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeU(native.c_str(), 0);
	if (format == FIF_UNKNOWN)
		format = FreeImage_GetFIFFromFilenameU(native.c_str());
#else
	FREE_IMAGE_FORMAT format = FreeImage_GetFileType(path.c_str(), 0);
	if (format == FIF_UNKNOWN)
		format = FreeImage_GetFIFFromFilename(path.c_str());
#endif
	return format;
}

FIBITMAP* FreeImageLoadUtf8(const std::string& path, int flags)
{
	const FREE_IMAGE_FORMAT format = FreeImageFormatUtf8(path);
	if (format == FIF_UNKNOWN)
		return nullptr;
#if defined(_WIN32)
	const std::filesystem::path native = Utf8Path(path);
	return FreeImage_LoadU(format, native.c_str(), flags);
#else
	return FreeImage_Load(format, path.c_str(), flags);
#endif
}
bool FreeImageSaveUtf8(FREE_IMAGE_FORMAT format, FIBITMAP* bitmap,
	const std::string& path, int flags)
{
#if defined(_WIN32)
	const std::filesystem::path native = Utf8Path(path);
	return FreeImage_SaveU(format, bitmap, native.c_str(), flags) != 0;
#else
	return FreeImage_Save(format, bitmap, path.c_str(), flags) != 0;
#endif
}
