#include "ImageProcessor.h"
#include "../mt19937int.h"
#include "../TargetPicture.h"
#include "../string_conv.h"
#include "../utils/RandomUtils.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <cmath>

static rgb PIXEL2RGB(RGBQUAD& q)
{
    rgb x;
    x.b = q.rgbBlue;
    x.g = q.rgbGreen;
    x.r = q.rgbRed;
    x.a = q.rgbReserved;
    return x;
}

static RGBQUAD RGB2PIXEL(rgb& val)
{
    RGBQUAD fpixel;
    fpixel.rgbRed = val.r;
    fpixel.rgbGreen = val.g;
    fpixel.rgbBlue = val.b;
    return fpixel;
}

/* 8x8 threshold map */
static const unsigned char threshold_map[8 * 8] = {
    0,48,12,60, 3,51,15,63,
    32,16,44,28,35,19,47,31,
    8,56, 4,52,11,59, 7,55,
    40,24,36,20,43,27,39,23,
    2,50,14,62, 1,49,13,61,
    34,18,46,30,33,17,45,29,
    10,58, 6,54, 9,57, 5,53,
    42,26,38,22,41,25,37,21
};

/* Luminance for each palette entry, to be initialized as soon as the program begins */
static unsigned luma[128];

bool PaletteCompareLuma(unsigned index1, unsigned index2)
{
    return luma[index1] < luma[index2];
}

double random_plus_minus(double val)
{
    double result;
    int val2 = static_cast<int>(std::round(100.0 * val));
    result = random(val2);
    if (random(2))
        result *= -1;
    return result / 100.0;
}

ImageProcessor::ImageProcessor()
    : m_input_bitmap(nullptr)
    , m_destination_bitmap(nullptr)
    , m_width(0)
    , m_height(0)
{
}

ImageProcessor::~ImageProcessor()
{
    // Ensure resources are freed
    if (m_input_bitmap) {
        FreeImage_Unload(m_input_bitmap);
        m_input_bitmap = nullptr;
    }
    
    if (m_destination_bitmap) {
        FreeImage_Unload(m_destination_bitmap);
        m_destination_bitmap = nullptr;
    }
}

bool ImageProcessor::Initialize(const Configuration& config)
{
    m_config = config;
    return true;
}

bool ImageProcessor::LoadInputBitmap()
{
    m_input_bitmap = FreeImage_Load(FreeImage_GetFileType(m_config.input_file.c_str()), m_config.input_file.c_str(), 0);
    if (!m_input_bitmap)
        return false;

    unsigned int input_width = FreeImage_GetWidth(m_input_bitmap);
    unsigned int input_height = FreeImage_GetHeight(m_input_bitmap);

    if (m_config.height == -1) // set height automatic to keep screen proportions
    {
        double iw = static_cast<double>(input_width);
        double ih = static_cast<double>(input_height);
        if (iw / ih > (320.0 / 240.0)) // 4:3 = 320:240
        {
            ih = static_cast<double>(input_height) / (static_cast<double>(input_width) / 320.0);
            m_config.height = static_cast<int>(ih);
        }
        else
            m_config.height = 240;
    }

    {
        FIBITMAP* tmp = FreeImage_Rescale(m_input_bitmap, m_config.width, m_config.height, m_config.rescale_filter);
        if (tmp) {
            FreeImage_Unload(m_input_bitmap);
            m_input_bitmap = tmp;
        }
    }
    {
        FIBITMAP* tmp = FreeImage_ConvertTo24Bits(m_input_bitmap);
        if (tmp) {
            FreeImage_Unload(m_input_bitmap);
            m_input_bitmap = tmp;
        }
    }

    // Apply image adjustments
    FreeImage_AdjustBrightness(m_input_bitmap, m_config.brightness);
    FreeImage_AdjustContrast(m_input_bitmap, m_config.contrast);
    FreeImage_AdjustGamma(m_input_bitmap, m_config.gamma);

    FreeImage_FlipVertical(m_input_bitmap);

    m_height = (int)m_config.height;
    m_width = (int)m_config.width;

    return true;
}

void ImageProcessor::InitLocalStructure()
{
    unsigned x, y;

    // Set our structure size
    unsigned width = FreeImage_GetWidth(m_input_bitmap);
    unsigned height = FreeImage_GetHeight(m_input_bitmap);
    
    // Resize the picture to match the input dimensions
    m_picture.resize(height);
    for (y = 0; y < height; ++y)
    {
        m_picture[y].Resize(width);
    }
    // Also prepare a source-picture buffer to preserve pre-quantized input
    m_source_picture.resize(height);
    for (y = 0; y < height; ++y) {
        m_source_picture[y].Resize(width);
    }

    // Copy data from input_bitmap to our structure
    RGBQUAD fpixel;
    rgb atari_color;
    for (y = 0; y < height; ++y)
    {
        for (x = 0; x < width; ++x)
        {
            FreeImage_GetPixelColor(m_input_bitmap, x, y, &fpixel);
            atari_color = PIXEL2RGB(fpixel);
            m_picture[y][x] = atari_color;
            m_source_picture[y][x] = atari_color;
            fpixel.rgbRed = static_cast<BYTE>(atari_color.r);
            fpixel.rgbGreen = static_cast<BYTE>(atari_color.g);
            fpixel.rgbBlue = static_cast<BYTE>(atari_color.b);
            FreeImage_SetPixelColor(m_input_bitmap, x, y, &fpixel);
        }
    }
}

void ImageProcessor::LoadDetailsMap()
{
    FIBITMAP* fbitmap = FreeImage_Load(FreeImage_GetFileType(m_config.details_file.c_str()), m_config.details_file.c_str(), 0);
    if (!fbitmap)
        return;
        
    {
        FIBITMAP* tmp = FreeImage_Rescale(fbitmap, m_config.width, m_config.height, FILTER_BOX);
        if (tmp) {
            FreeImage_Unload(fbitmap);
            fbitmap = tmp;
        }
    }
    {
        FIBITMAP* tmp = FreeImage_ConvertTo24Bits(fbitmap);
        if (tmp) {
            FreeImage_Unload(fbitmap);
            fbitmap = tmp;
        }
    }

    FreeImage_FlipVertical(fbitmap);

    RGBQUAD fpixel;
    int x, y;

    m_details_data.resize(m_height);
    for (y = 0; y < m_height; ++y)
    {
        m_details_data[y].resize(m_width);

        for (x = 0; x < m_width; ++x)
        {
            FreeImage_GetPixelColor(fbitmap, x, y, &fpixel);
            // average as brightness
            m_details_data[y][x] = (unsigned char)((int)((int)fpixel.rgbRed + (int)fpixel.rgbGreen + (int)fpixel.rgbBlue) / 3);
        }
    }
    
    FreeImage_Unload(fbitmap);
}

void ImageProcessor::GeneratePictureErrorMap()
{
    if (!m_config.details_file.empty())
        LoadDetailsMap();

    unsigned int details_multiplier = 255;

    const int w = (int)FreeImage_GetWidth(m_input_bitmap);
    const int h = (int)FreeImage_GetHeight(m_input_bitmap);

    for (int i = 0; i < 128; ++i)
    {
        m_picture_all_errors[i].resize(w * h);

        const rgb ref = atari_palette[i];

        distance_t* dst = &m_picture_all_errors[i][0];
        for (int y = 0; y < h; ++y)
        {
            const screen_line& srcrow = m_picture[y];

            if (!m_details_data.empty())
            {
                for (int x = 0; x < w; ++x)
                {
                    details_multiplier = 255u + static_cast<unsigned int>(
                        std::lround(static_cast<double>(m_details_data[y][x]) * m_config.details_strength)
                    );
                    *dst++ = (details_multiplier * distance_function(srcrow[x], ref)) / 255;
                }
            }
            else
            {
                for (int x = 0; x < w; ++x)
                {
                    *dst++ = distance_function(srcrow[x], ref);
                }
            }
        }
    }
}

void ImageProcessor::PrepareDestinationPicture()
{
    int width = static_cast<int>(FreeImage_GetWidth(m_input_bitmap));
    int height = static_cast<int>(FreeImage_GetHeight(m_input_bitmap));
    int bpp = FreeImage_GetBPP(m_input_bitmap); // Bits per pixel

    // Allocate a new bitmap with the same dimensions and bpp
    m_destination_bitmap = FreeImage_Allocate(width, height, bpp);

    RGBQUAD black = { 0, 0, 0, 255 }; // Black color with alpha

    // Fill the new bitmap with black color
    FreeImage_FillBackground(m_destination_bitmap, &black, 0);

    // Reset destination-used color set (fresh run)
    {
        std::lock_guard<std::mutex> lock(m_color_indexes_mutex);
        m_color_indexes_on_dst_picture.clear();
    }

    // Process the image based on dithering method
    if (m_config.dither != E_DITHER_NONE)
    {
        if (m_config.dither == E_DITHER_KNOLL)
            KnollDithering();
        else
        {
            ClearErrorMap();
            OtherDithering();
        }
    }
    else
    {
        // No dithering - direct color mapping
        for (int y = 0; y < m_height; ++y)
        {
            for (int x = 0; x < m_width; ++x)
            {
                rgb out_pixel = m_picture[y][x];
                unsigned char color_index = FindAtariColorIndex(out_pixel);
                {
                    std::lock_guard<std::mutex> lock(m_color_indexes_mutex);
                    m_color_indexes_on_dst_picture.insert(color_index);
                }
                out_pixel = atari_palette[color_index];
                RGBQUAD color = RGB2PIXEL(out_pixel);
                FreeImage_SetPixelColor(m_destination_bitmap, x, y, &color);
            }
        }
    }

    // Only copy destination back to m_picture in single-frame mode.
    // In dual mode, we want m_picture to remain the original source image so
    // that any sampling (mutator seeding/fallbacks) uses the source, not the
    // dithered/quantized destination.
    if (!m_config.dual_mode) {
        int w = static_cast<int>(FreeImage_GetWidth(m_input_bitmap));
        int h = static_cast<int>(FreeImage_GetHeight(m_input_bitmap));
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                RGBQUAD color;
                FreeImage_GetPixelColor(m_destination_bitmap, x, y, &color);
                rgb out_pixel = PIXEL2RGB(color);
                m_picture[y][x] = out_pixel; // used by the color distance cache
            }
        }
    }
}

void ImageProcessor::OtherDithering()
{
    int y;

    const int w = FreeImage_GetWidth(m_input_bitmap);
    const int h = FreeImage_GetHeight(m_input_bitmap);
    const int w1 = w - 1;

    for (y = 0; y < h; ++y)
    {
        const bool flip = y & 1;

        for (int i = 0; i < w; ++i)
        {
            int x = flip ? w1 - i : i;

            rgb out_pixel = m_picture[y][x];

            if (m_config.dither != E_DITHER_NONE)
            {
                rgb_error p = m_error_map[y][x];
                p.r += out_pixel.r;
                p.g += out_pixel.g;
                p.b += out_pixel.b;

                if (p.r > 255)
                    p.r = 255;
                else if (p.r < 0)
                    p.r = 0;

                if (p.g > 255)
                    p.g = 255;
                else if (p.g < 0)
                    p.g = 0;

                if (p.b > 255)
                    p.b = 255;
                else if (p.b < 0)
                    p.b = 0;

                out_pixel.r = static_cast<unsigned char>(p.r + 0.5);
                out_pixel.g = static_cast<unsigned char>(p.g + 0.5);
                out_pixel.b = static_cast<unsigned char>(p.b + 0.5);

                out_pixel = atari_palette[FindAtariColorIndex(out_pixel)];

                rgb in_pixel = m_picture[y][x];
                rgb_error qe;
                qe.r = static_cast<double>(in_pixel.r) - static_cast<double>(out_pixel.r);
                qe.g = static_cast<double>(in_pixel.g) - static_cast<double>(out_pixel.g);
                qe.b = static_cast<double>(in_pixel.b) - static_cast<double>(out_pixel.b);

                if (m_config.dither == E_DITHER_FLOYD)
                {
                    /* Standard Floyd-Steinberg uses 4 pixels to diffuse */
                    DiffuseError(x - 1, y, 7.0 / 16.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 1, y + 1, 3.0 / 16.0, qe.r, qe.g, qe.b);
                    DiffuseError(x, y + 1, 5.0 / 16.0, qe.r, qe.g, qe.b);
                    DiffuseError(x - 1, y + 1, 1.0 / 16.0, qe.r, qe.g, qe.b);
                }
                else if (m_config.dither == E_DITHER_LINE)
                {
                    // line dithering that reduces number of colors in line
                    if (y % 2 == 0)
                    {
                        DiffuseError(x, y + 1, 0.5, qe.r, qe.g, qe.b);
                    }
                }
                else if (m_config.dither == E_DITHER_LINE2)
                {
                    // line dithering
                    DiffuseError(x, y + 1, 0.5, qe.r, qe.g, qe.b);
                }
                else if (m_config.dither == E_DITHER_CHESS)
                {
                    // Chessboard dithering
                    if ((x + y) % 2 == 0)
                    {
                        DiffuseError(x + 1, y, 0.5, qe.r, qe.g, qe.b);
                        DiffuseError(x, y + 1, 0.5, qe.r, qe.g, qe.b);
                    }
                }
                else if (m_config.dither == E_DITHER_SIMPLE)
                {
                    DiffuseError(x + 1, y, 1.0 / 3.0, qe.r, qe.g, qe.b);
                    DiffuseError(x, y + 1, 1.0 / 3.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 1, y + 1, 1.0 / 3.0, qe.r, qe.g, qe.b);
                }
                else if (m_config.dither == E_DITHER_2D)
                {
                    DiffuseError(x + 1, y, 2.0 / 4.0, qe.r, qe.g, qe.b);
                    DiffuseError(x, y + 1, 1.0 / 4.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 1, y + 1, 1.0 / 4.0, qe.r, qe.g, qe.b);
                }
                else if (m_config.dither == E_DITHER_JARVIS)
                {
                    DiffuseError(x + 1, y, 7.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 2, y, 5.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x - 1, y + 1, 3.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x - 2, y + 1, 5.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x, y + 1, 7.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 1, y + 1, 5.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 2, y + 1, 3.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x - 1, y + 2, 1.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x - 2, y + 2, 3.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x, y + 2, 5.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 1, y + 2, 3.0 / 48.0, qe.r, qe.g, qe.b);
                    DiffuseError(x + 2, y + 2, 1.0 / 48.0, qe.r, qe.g, qe.b);
                }
            }
            unsigned char color_index = FindAtariColorIndex(out_pixel);
            m_color_indexes_on_dst_picture.insert(color_index);
            out_pixel = atari_palette[color_index];
            RGBQUAD color = RGB2PIXEL(out_pixel);
            FreeImage_SetPixelColor(m_destination_bitmap, x, y, &color);
        }
    }
}

void ImageProcessor::ClearErrorMap()
{
    // Resize the error map if it's empty
    if (m_error_map.empty())
    {
        m_error_map.resize(m_height);
        for (int y = 0; y < m_height; ++y)
        {
            m_error_map[y].resize(m_width + 1);
        }
    }
    
    // Clear the error map
    for (int y = 0; y < m_height; ++y)
    {
        for (int x = 0; x < m_width; ++x)
        {
            m_error_map[y][x].zero();
        }
    }
}

void ImageProcessor::DiffuseError(int x, int y, double quant_error, double e_r, double e_g, double e_b)
{
    if (!(x >= 0 && x < m_width && y >= 0 && y < m_height))
        return;

    rgb_error p = m_error_map[y][x];
    p.r += e_r * quant_error * m_config.dither_strength * (1 + random_plus_minus(m_config.dither_randomness));
    p.g += e_g * quant_error * m_config.dither_strength * (1 + random_plus_minus(m_config.dither_randomness));
    p.b += e_b * quant_error * m_config.dither_strength * (1 + random_plus_minus(m_config.dither_randomness));
    
    // Clamp values to valid range
    if (p.r > 255) p.r = 255;
    else if (p.r < 0) p.r = 0;
    
    if (p.g > 255) p.g = 255;
    else if (p.g < 0) p.g = 0;
    
    if (p.b > 255) p.b = 255;
    else if (p.b < 0) p.b = 0;
    
    m_error_map[y][x] = p;
}

void* ImageProcessor::KnollDitheringParallelHelper(void* arg)
{
    parallel_for_arg_t* param = (parallel_for_arg_t*)arg;
    ((ImageProcessor*)param->this_ptr)->KnollDitheringParallel(param->from, param->to);
    return NULL;
}

void ImageProcessor::ParallelFor(int from, int to, void* (*start_routine)(void*))
{
    const int range = std::abs(to - from);
    if (range <= 0) {
        return; // nothing to do
    }

    const int max_threads = std::max(1, m_config.threads);
    const int num_threads = std::min(max_threads, range);

    std::vector<std::thread> threads;
    std::vector<parallel_for_arg_t> threads_arg;

    threads.reserve(num_threads);
    threads_arg.resize(num_threads);

    const int base_step = range / num_threads;           // at least 1
    int remainder = range % num_threads;                 // distribute extras

    int cur_from = from;
    for (int t = 0; t < num_threads; ++t)
    {
        const int step = base_step + (remainder > 0 ? 1 : 0);
        if (remainder > 0) remainder--;

        threads_arg[t].this_ptr = this;
        threads_arg[t].from = cur_from;
        threads_arg[t].to = cur_from + step;

        threads.emplace_back(std::bind(start_routine, (void*)&threads_arg[t]));
        cur_from += step;
    }
    for (int t = 0; t < num_threads; ++t)
    {
        threads[t].join();
    }
}

void ImageProcessor::KnollDithering()
{
    // Initialize luminance values for palette colors
    for (unsigned c = 0; c < 128; ++c)
    {
        luma[c] = atari_palette[c].r * 299 + atari_palette[c].g * 587 + atari_palette[c].b * 114;
    }
    
    // Use parallel processing for Knoll dithering
    ParallelFor(0, m_height, KnollDitheringParallelHelper);
}

void ImageProcessor::KnollDitheringParallel(int from, int to)
{
    // Write rows using scanline pointers for thread safety and speed.
    // FreeImage scanlines are independent per y, so per-row parallelism is safe.
    for (int y = from; y < to; ++y)
    {
        BYTE* row = FreeImage_GetScanLine(m_destination_bitmap, y);
        if (!row) continue;
        for (unsigned x = 0; x < (unsigned)m_width; ++x)
        {
            const rgb src_color = m_picture[y][x];
            const unsigned map_value = threshold_map[(x & 7) + ((y & 7) << 3)];
            const MixingPlan plan = DeviseBestMixingPlan(src_color);
            const unsigned char color_index = plan.colors[map_value];
            {
                std::lock_guard<std::mutex> lock(m_color_indexes_mutex);
                m_color_indexes_on_dst_picture.insert(color_index);
            }
            const rgb out_pixel = atari_palette[color_index];
            BYTE* p = row + x * 3; // 24-bit BGR
            p[0] = out_pixel.b;
            p[1] = out_pixel.g;
            p[2] = out_pixel.r;
        }
    }
}

ImageProcessor::MixingPlan ImageProcessor::DeviseBestMixingPlan(rgb color)
{
    MixingPlan result = { {0} };
    const double X = m_config.dither_strength / 100; // Error multiplier
    rgb src = color;
    rgb_error e;
    e.zero(); // Error accumulator
    
    for (unsigned c = 0; c < 64; ++c)
    {
        // Current temporary value
        rgb_error temp;
        temp.r = src.r + e.r * X * (1 + random_plus_minus(m_config.dither_randomness));
        temp.g = src.g + e.g * X * (1 + random_plus_minus(m_config.dither_randomness));
        temp.b = src.b + e.b * X * (1 + random_plus_minus(m_config.dither_randomness));

        // Clamp it in the allowed RGB range
        if (temp.r < 0) temp.r = 0; else if (temp.r > 255) temp.r = 255;
        if (temp.g < 0) temp.g = 0; else if (temp.g > 255) temp.g = 255;
        if (temp.b < 0) temp.b = 0; else if (temp.b > 255) temp.b = 255;
        
        // Find the closest color from the palette
        double least_penalty = 1e99;
        unsigned chosen = c % 128;
        for (unsigned index = 0; index < 128; ++index)
        {
            rgb color2;
            color2.r = static_cast<unsigned char>(temp.r);
            color2.g = static_cast<unsigned char>(temp.g);
            color2.b = static_cast<unsigned char>(temp.b);

            double penalty = distance_function(atari_palette[index], color2);
            if (penalty < least_penalty)
            {
                least_penalty = penalty;
                chosen = index;
            }
        }
        
        // Add it to candidates and update the error
        result.colors[c] = chosen;
        rgb color = atari_palette[chosen];
        e.r += (double)src.r - (double)color.r;
        e.g += (double)src.g - (double)color.g;
        e.b += (double)src.b - (double)color.b;
    }
    
    // Sort the colors according to luminance
    std::sort(result.colors, result.colors + 64, PaletteCompareLuma);
    return result;
}

void ImageProcessor::FindPossibleColors()
{
    m_possible_colors_for_each_line.resize(m_height);
    std::set<unsigned char> set_of_colors;

    // For each screen line, find the possible colors
    std::vector<unsigned char> vector_of_colors;
    for (int l = m_height - 1; l >= 0; --l)
    {
        // Clear the set for this line
        set_of_colors.clear();
        
        // Find all colors used in this line
        for (int x = 0; x < m_width; ++x)
            set_of_colors.insert(FindAtariColorIndex(m_picture[l][x]) * 2);

        // Convert set to vector for easier access
        vector_of_colors.resize(set_of_colors.size());
        std::copy(set_of_colors.begin(), set_of_colors.end(), vector_of_colors.data());
        m_possible_colors_for_each_line[l] = vector_of_colors;
    }
}