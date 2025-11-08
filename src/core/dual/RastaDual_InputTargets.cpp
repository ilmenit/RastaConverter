// Build input-based per-pixel YUV targets from destination image for post-bootstrap dual optimization
// Uses m_picture (destination) which is either quantized source or dithered source, matching Single Frame behavior
#include "rasta.h"
#include <cassert>

void RastaConverter::PrecomputeInputTargets()
{
#ifdef _DEBUG
    assert(!m_picture.empty());
#endif
    const unsigned total = (unsigned)(m_width * m_height);
    m_input_target_y.resize(total); m_input_target_u.resize(total); m_input_target_v.resize(total);
    m_input_target_y8.resize(total); m_input_target_u8.resize(total); m_input_target_v8.resize(total);

    auto rgb_to_yuv = [](const rgb& c, float& y, float& u, float& v){
        float r=(float)c.r, g=(float)c.g, b=(float)c.b;
        y = 0.299f*r + 0.587f*g + 0.114f*b;
        u = (b - y) * 0.565f;
        v = (r - y) * 0.713f;
    };
    auto q8 = [](float v, float offset, float scale)->unsigned char{
        float t = (v + offset) * scale; if (t < 0.0f) t = 0.0f; if (t > 255.0f) t = 255.0f; return (unsigned char)(t + 0.5f);
    };

    unsigned idx = 0;
    for (int y = 0; y < m_height; ++y) {
        const screen_line& row = m_picture[y];  // Use destination image (quantized or dithered)
        for (int x = 0; x < m_width; ++x) {
            float Y,U,V; rgb_to_yuv(row[x], Y,U,V);
            m_input_target_y[idx] = Y; m_input_target_u[idx] = U; m_input_target_v[idx] = V;
            m_input_target_y8[idx] = q8(Y, 0.0f, 1.0f);
            m_input_target_u8[idx] = q8(U, 160.0f, 1.0f);
            m_input_target_v8[idx] = q8(V, 200.0f, 1.0f);
            ++idx;
        }
    }
}
