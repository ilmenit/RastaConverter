// Dual-mode display routines extracted from RastaDual.cpp
#include "rasta.h"
#include "TargetPicture.h"
#include <cassert>

void RastaConverter::ShowLastCreatedPictureDual()
{
#ifdef _DEBUG
	assert(cfg.dual_mode && "ShowLastCreatedPictureDual called while dual_mode is off");
#endif
	if (!cfg.dual_mode) { ShowLastCreatedPicture(); return; }

	// Ensure bitmaps exist
	if (!output_bitmap_A) output_bitmap_A = FreeImage_Allocate(cfg.width, cfg.height, 24);
	if (!output_bitmap_B) output_bitmap_B = FreeImage_Allocate(cfg.width, cfg.height, 24);
	if (!output_bitmap_blended) output_bitmap_blended = FreeImage_Allocate(cfg.width, cfg.height, 24);

	// Render A and B outputs from created pictures
	for (int y = 0; y < m_height; ++y)
	{
		for (int x = 0; x < m_width; ++x)
		{
			unsigned char idxA = (y < (int)m_eval_gstate.m_created_picture.size() && x < (int)m_eval_gstate.m_created_picture[y].size()) ? m_eval_gstate.m_created_picture[y][x] : 0;
			unsigned char idxB = (y < (int)m_created_picture_B.size() && x < (int)m_created_picture_B[y].size()) ? m_created_picture_B[y][x] : 0;
			rgb colA = atari_palette[idxA];
			rgb colB = atari_palette[idxB];
			RGBQUAD qa = { colA.b, colA.g, colA.r, 0 };
			RGBQUAD qb = { colB.b, colB.g, colB.r, 0 };
			FreeImage_SetPixelColor(output_bitmap_A, x, y, &qa);
			FreeImage_SetPixelColor(output_bitmap_B, x, y, &qb);

			// Blended: use precomputed pair sRGB if available; else simple 50/50 rgb
			RGBQUAD qm;
			if (m_dual_tables_ready && !m_pair_srgb.empty()) {
				size_t pairIdx = ((size_t)idxA * 128 + (size_t)idxB) * 3;
#ifdef _DEBUG
				assert(pairIdx + 2 < m_pair_srgb.size());
#endif
				qm.rgbRed   = m_pair_srgb[pairIdx + 0];
				qm.rgbGreen = m_pair_srgb[pairIdx + 1];
				qm.rgbBlue  = m_pair_srgb[pairIdx + 2];
			} else {
				qm.rgbRed   = (unsigned char)((colA.r + colB.r) >> 1);
				qm.rgbGreen = (unsigned char)((colA.g + colB.g) >> 1);
				qm.rgbBlue  = (unsigned char)((colA.b + colB.b) >> 1);
			}
			FreeImage_SetPixelColor(output_bitmap_blended, x, y, &qm);
		}
	}

	// Display according to current dual display mode
	int w = FreeImage_GetWidth(input_bitmap);
	switch (m_dual_display) {
		case DualDisplayMode::A: gui.DisplayBitmap(w, 0, output_bitmap_A); break;
		case DualDisplayMode::B: gui.DisplayBitmap(w, 0, output_bitmap_B); break;
		case DualDisplayMode::MIX: default: gui.DisplayBitmap(w, 0, output_bitmap_blended); break;
	}
}


