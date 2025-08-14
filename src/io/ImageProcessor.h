#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <string>
#include <vector>
#include <set>
#include <mutex>
#include "FreeImage.h"
#include "color/rgb.h"
#include "color/distance.h"
#include "raster/Program.h"
#include "config.h"

/**
 * Handles image loading, processing, and dithering operations
 */
class ImageProcessor {
public:
    /**
     * Constructor
     */
    ImageProcessor();
    
    /**
     * Destructor
     */
    ~ImageProcessor();
    
    /**
     * Initialize the processor with configuration
     * 
     * @param config Configuration parameters
     * @return True if initialization succeeded
     */
    bool Initialize(const Configuration& config);
    
    /**
     * Load input bitmap from configuration
     * 
     * @return True if loading succeeded
     */
    bool LoadInputBitmap();
    
    /**
     * Process the input bitmap according to configuration settings
     * (brightness, contrast, gamma, dithering)
     */
    void PrepareDestinationPicture();
    
    /**
     * Generate an error map for color distances
     */
    void GeneratePictureErrorMap();
    
    /**
     * Load details map from file
     */
    void LoadDetailsMap();
    
    /**
     * Apply Knoll dithering to the image
     */
    void KnollDithering();
    
    /**
     * Apply other dithering methods (Floyd-Steinberg, etc)
     */
    void OtherDithering();
    
    /**
     * Generate a batched dithering plan for a color
     */
    struct MixingPlan {
        unsigned colors[64];
    };
    MixingPlan DeviseBestMixingPlan(rgb color);
    
    /**
     * Diffuse error for dithering algorithms
     */
    void DiffuseError(int x, int y, double quant_error, double e_r, double e_g, double e_b);
    
    /**
     * Initialize local structures for processing
     */
    void InitLocalStructure();
    
    /**
     * Clear error map used in dithering
     */
    void ClearErrorMap();
    
    /**
     * Find possible colors for each screen line
     */
    void FindPossibleColors();
    
    /**
     * Get the set of color indexes used in the destination picture
     */
    const std::set<unsigned char>& GetColorIndexesOnDstPicture() const { return m_color_indexes_on_dst_picture; }
    
    /**
     * Get the processed input bitmap
     */
    FIBITMAP* GetInputBitmap() const { return m_input_bitmap; }
    
    /**
     * Get the destination bitmap
     */
    FIBITMAP* GetDestinationBitmap() const { return m_destination_bitmap; }
    
    /**
     * Get the width of the processed image
     */
    int GetWidth() const { return m_width; }
    
    /**
     * Get the height of the processed image
     */
    int GetHeight() const { return m_height; }
    
    /**
     * Get the picture data
     */
    const std::vector<screen_line>& GetPicture() const { return m_picture; }

    /**
     * Get the original source picture (pre-quantization), if available.
     * In current pipeline this is identical to GetPicture() until destination overwrite.
     */
    const std::vector<screen_line>& GetSourcePicture() const { return m_source_picture.empty() ? m_picture : m_source_picture; }
    
    /**
     * Get error map for all colors
     */
    const std::vector<distance_t>* GetPictureAllErrors() const { return m_picture_all_errors; }
    
    /**
     * Get the possible colors for each line
     */
    const std::vector<std::vector<unsigned char>>& GetPossibleColorsForEachLine() const { return m_possible_colors_for_each_line; }
    
    /**
     * Get details data
     */
    const std::vector<std::vector<unsigned char>>& GetDetailsData() const { return m_details_data; }
    
    // Row-progress API for progressive preview
    void ResetRowProgress()
    {
        m_row_done.assign(m_height, (unsigned char)0);
    }
    bool IsRowDone(int y) const
    {
        return (y >= 0 && y < m_height && !m_row_done.empty()) ? (m_row_done[y] != 0) : false;
    }
    
private:
    // Processing for Knoll dithering
    void KnollDitheringParallel(int from, int to);
    static void* KnollDitheringParallelHelper(void* arg);
    void ParallelFor(int from, int to, void* (*start_routine)(void*));
    
    struct parallel_for_arg_t {
        int from;
        int to;
        void* this_ptr;
    };
    
private:
    // Configuration
    Configuration m_config;
    
    // Image data
    FIBITMAP* m_input_bitmap;
    FIBITMAP* m_destination_bitmap;
    int m_width;
    int m_height;
    
    // Picture data
    std::vector<screen_line> m_picture;
    // Preserve a copy of the pre-quantized, pre-destination source picture for dual-mode targets
    std::vector<screen_line> m_source_picture;
    std::vector<distance_t> m_picture_all_errors[128];
    std::vector<std::vector<unsigned char>> m_details_data;
    std::vector<std::vector<rgb_error>> m_error_map;
    std::set<unsigned char> m_color_indexes_on_dst_picture;
    std::mutex m_color_indexes_mutex;
    
    // Possible colors
    std::vector<std::vector<unsigned char>> m_possible_colors_for_each_line;
    
    // Row completion flags for progressive display during preprocessing (0/1)
    std::vector<unsigned char> m_row_done;
};

#endif // IMAGE_PROCESSOR_H