#pragma once

#include <string>

#if defined(__APPLE__)
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif
#include <FreeImage.h>
#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif

FREE_IMAGE_FORMAT FreeImageFormatUtf8(const std::string& path);
FIBITMAP* FreeImageLoadUtf8(const std::string& path, int flags = 0);
bool FreeImageSaveUtf8(FREE_IMAGE_FORMAT format, FIBITMAP* bitmap,
	const std::string& path, int flags = 0);
