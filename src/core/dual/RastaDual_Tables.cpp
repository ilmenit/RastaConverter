// Dual-mode precomputed tables extracted from RastaDual.cpp
#include "rasta.h"
#include "TargetPicture.h"
#include <cassert>
#include <cmath>

void RastaConverter::PrecomputeDualTables()
{
#ifdef _DEBUG
	assert(cfg.dual_mode);
#endif
	if (m_dual_tables_ready) return;
	// Palette
	auto rgb_to_yuv = [](const rgb& c, float& y, float& u, float& v){
		float r=(float)c.r, g=(float)c.g, b=(float)c.b;
		y = 0.299f*r + 0.587f*g + 0.114f*b;
		u = (b - y) * 0.565f;
		v = (r - y) * 0.713f;
	};
	for (int i=0;i<128;++i) {
		float y,u,v; rgb_to_yuv(atari_palette[i], y,u,v);
		m_palette_y[i]=y; m_palette_u[i]=u; m_palette_v[i]=v;
	}
	// Target YUV per pixel
	const unsigned total = (unsigned)(m_width*m_height);
	m_target_y.resize(total); m_target_u.resize(total); m_target_v.resize(total);
	m_target_y8.resize(total); m_target_u8.resize(total); m_target_v8.resize(total);
	unsigned idx=0;
	for (int y=0;y<m_height;++y) {
		const screen_line& row = m_picture[y];
		for (int x=0;x<m_width;++x) {
			float Y,U,V; rgb_to_yuv(row[x], Y,U,V);
			m_target_y[idx]=Y; m_target_u[idx]=U; m_target_v[idx]=V; ++idx;
		}
	}
	// Pair YUV averages
	m_pair_Ysum.resize(128*128); m_pair_Usum.resize(128*128); m_pair_Vsum.resize(128*128);
	m_pair_Ydiff.resize(128*128); m_pair_Udiff.resize(128*128); m_pair_Vdiff.resize(128*128);
	m_pair_Ysum8.resize(128*128); m_pair_Usum8.resize(128*128); m_pair_Vsum8.resize(128*128);
	m_pair_Ydiff8.resize(128*128); m_pair_Udiff8.resize(128*128); m_pair_Vdiff8.resize(128*128);
	for (int a=0;a<128;++a) {
		for (int b=0;b<128;++b) {
			int p=(a<<7)|b;
			m_pair_Ysum[p] = 0.5f*(m_palette_y[a] + m_palette_y[b]);
			m_pair_Usum[p] = 0.5f*(m_palette_u[a] + m_palette_u[b]);
			m_pair_Vsum[p] = 0.5f*(m_palette_v[a] + m_palette_v[b]);
			m_pair_Ydiff[p] = fabsf(m_palette_y[a] - m_palette_y[b]);
			m_pair_Udiff[p] = fabsf(m_palette_u[a] - m_palette_u[b]);
			m_pair_Vdiff[p] = fabsf(m_palette_v[a] - m_palette_v[b]);
			auto q8 = [](float v, float offset, float scale)->unsigned char{
				float t = (v + offset) * scale; if (t < 0.0f) t = 0.0f; if (t > 255.0f) t = 255.0f; return (unsigned char)(t + 0.5f);
			};
			// Y is 0..255 already; U ~ [-144,144], V ~ [-182,182] roughly for Atari palette; use conservative ranges
			m_pair_Ysum8[p] = q8(m_pair_Ysum[p], 0.0f, 1.0f);
			m_pair_Usum8[p] = q8(m_pair_Usum[p], 160.0f, 1.0f);
			m_pair_Vsum8[p] = q8(m_pair_Vsum[p], 200.0f, 1.0f);
			m_pair_Ydiff8[p] = q8(m_pair_Ydiff[p], 0.0f, 1.0f);
			m_pair_Udiff8[p] = q8(m_pair_Udiff[p], 0.0f, 1.0f);
			m_pair_Vdiff8[p] = q8(m_pair_Vdiff[p], 0.0f, 1.0f);
		}
	}
	// Quantize target YUV to 8-bit with same shifts
	for (unsigned i=0;i<total;++i) {
		auto q8 = [](float v, float offset, float scale)->unsigned char{
			float t = (v + offset) * scale; if (t < 0.0f) t = 0.0f; if (t > 255.0f) t = 255.0f; return (unsigned char)(t + 0.5f);
		};
		m_target_y8[i] = q8(m_target_y[i], 0.0f, 1.0f);
		m_target_u8[i] = q8(m_target_u[i], 160.0f, 1.0f);
		m_target_v8[i] = q8(m_target_v[i], 200.0f, 1.0f);
	}
	// Blended sRGB pair table for output (3 bytes per pair)
	m_pair_srgb.resize(128*128*3);
	for (int a=0;a<128;++a) {
		for (int b=0;b<128;++b) {
			int p=(a<<7)|b; int off = p*3;
			if (cfg.dual_blending == "rgb") {
				// average in sRGB
				m_pair_srgb[off+0] = (unsigned char)((atari_palette[a].r + atari_palette[b].r)>>1);
				m_pair_srgb[off+1] = (unsigned char)((atari_palette[a].g + atari_palette[b].g)>>1);
				m_pair_srgb[off+2] = (unsigned char)((atari_palette[a].b + atari_palette[b].b)>>1);
			} else {
				// yuv blend -> convert back to sRGB with simple inverse (approx)
				float Y = m_pair_Ysum[p], U = m_pair_Usum[p], V = m_pair_Vsum[p];
				float R = Y + 1.403f*V;
				float B = Y + 1.773f*U;
				float G = (Y - 0.299f*R - 0.114f*B)/0.587f;
				auto clamp255 = [](float v)->unsigned char{ if (v<0) v=0; if (v>255) v=255; return (unsigned char)(v+0.5f); };
				m_pair_srgb[off+0] = clamp255(R);
				m_pair_srgb[off+1] = clamp255(G);
				m_pair_srgb[off+2] = clamp255(B);
			}
		}
	}
	m_dual_tables_ready = true;
}


