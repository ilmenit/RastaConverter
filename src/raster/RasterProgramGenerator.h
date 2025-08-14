#ifndef RASTER_PROGRAM_GENERATOR_H
#define RASTER_PROGRAM_GENERATOR_H

#include <vector>
#include <set>
#include "Program.h"
#include "color/rgb.h"
#include "config/config.h"

/**
 * Responsible for generating raster programs from images
 */
class RasterProgramGenerator {
public:
    /**
     * Constructor
     */
    RasterProgramGenerator();
    
    /**
     * Initialize the generator with screen dimensions
     * 
     * @param width Screen width
     * @param height Screen height
     */
    void Initialize(int width, int height);
    
    /**
     * Create an empty raster program
     * 
     * @param pic Raster picture to initialize
     */
    void CreateEmptyRasterPicture(raster_picture* pic);
    
    /**
     * Create a raster program with limited colors
     * 
     * @param pic Raster picture to initialize
     * @param colorIndexes Set of color indexes to use
     */
    void CreateLowColorRasterPicture(raster_picture* pic, const std::set<unsigned char>& colorIndexes);
    
    /**
     * Create a "smart" raster program that analyzes the image for better initial state
     * 
     * @param pic Raster picture to initialize
     * @param initType Type of initialization (SMART or LESS)
     * @param picture Screen line data
     * @param colorIndexes Set of color indexes to use
     */
    void CreateSmartRasterPicture(raster_picture* pic, e_init_type initType, 
                                 const std::vector<screen_line>& picture,
                                 const std::set<unsigned char>& colorIndexes);
    
    /**
     * Create a random raster program
     * 
     * @param pic Raster picture to initialize
     * @param picture Screen line data
     */
    void CreateRandomRasterPicture(raster_picture* pic, const std::vector<screen_line>& picture);
    
    /**
     * Optimize a raster program by eliminating redundant instructions
     * 
     * @param pic Raster picture to optimize
     */
    void OptimizeRasterProgram(raster_picture* pic);
    
    /**
     * Create a test raster program (for debugging)
     * 
     * @param pic Raster picture to initialize
     * @param picture Screen line data
     */
    void TestRasterProgram(raster_picture* pic, std::vector<screen_line>& picture);
    
private:
    int m_width;
    int m_height;
};

#endif // RASTER_PROGRAM_GENERATOR_H