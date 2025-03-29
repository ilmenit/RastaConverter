#ifndef OUTPUT_MANAGER_H
#define OUTPUT_MANAGER_H

#include <string>
#include "../optimization/EvaluationContext.h"
#include "../raster/Program.h"
#include "../color/rgb.h"
#include "FreeImage.h"

/**
 * Manages all file output operations
 */
class OutputManager {
public:
    /**
     * Constructor
     */
    OutputManager();
    
    /**
     * Initialize the output manager
     * 
     * @param outputFile Base name for output files
     * @param width Image width
     * @param height Image height
     */
    void Initialize(const std::string& outputFile, int width, int height);
    
    /**
     * Save a bitmap to file
     * 
     * @param filename Filename to save to
     * @param bitmap Bitmap to save
     * @return True if saving was successful
     */
    bool SavePicture(const std::string& filename, FIBITMAP* bitmap);
    
    /**
     * Save current raster program
     * 
     * @param filename Filename to save to
     * @param pic Raster program to save
     * @param cmdLine Command line used to generate the program
     * @param inputFile Input file used
     * @param evaluations Number of evaluations performed
     * @param score Final score/distance
     */
    void SaveRasterProgram(const std::string& filename, 
                          const raster_picture* pic,
                          const std::string& cmdLine,
                          const std::string& inputFile,
                          unsigned long long evaluations,
                          double score);
    
    /**
     * Save player-missile graphics data
     * 
     * @param filename Filename to save to
     * @param spritesMemory Sprite memory data
     */
    void SavePMG(const std::string& filename, const sprites_memory_t& spritesMemory);
    
    /**
     * Save screen data in MIC format
     * 
     * @param filename Filename to save to
     * @param createdPicture Created picture color data
     * @param createdPictureTargets Created picture target registers
     * @return True if saving was successful
     */
    bool SaveScreenData(const std::string& filename,
                      const std::vector<color_index_line>& createdPicture,
                      const std::vector<line_target>& createdPictureTargets);
    
    /**
     * Save optimization statistics
     * 
     * @param filename Filename to save to
     * @param statistics Statistics data
     */
    void SaveStatistics(const std::string& filename, 
                      const statistics_list& statistics);
    
    /**
     * Save LAHC (Late Acceptance Hill Climbing) state
     * 
     * @param filename Filename to save to
     * @param previousResults Previous results array
     * @param previousResultsIndex Current index in results array
     * @param costMax Maximum cost threshold
     * @param N Count of cost_max entries
     * @param currentCost Current accepted cost
     */
    void SaveLAHC(const std::string& filename,
                const std::vector<double>& previousResults,
                size_t previousResultsIndex,
                double costMax,
                int N,
                double currentCost);
    
    /**
     * Save the best solution (all files)
     * 
     * @param evalContext Evaluation context
     * @param outputBitmap Output bitmap
     * @param cmdLine Command line used
     * @param inputFile Input file used
     */
    void SaveBestSolution(const EvaluationContext& evalContext,
                         FIBITMAP* outputBitmap,
                         const std::string& cmdLine,
                         const std::string& inputFile);
    
    /**
     * Convert a raw score to a normalized score
     * 
     * @param rawScore Raw score/distance value
     * @return Normalized score
     */
    double NormalizeScore(double rawScore) const;
    
public:
    // External instruction/register name arrays
    static const char* mem_regs_names[E_TARGET_MAX + 1];
    
private:
    // Constants for RGB to register conversion
    unsigned char ConvertColorRegisterToRawData(e_target t) const;
    
    // Helper function to write instruction string to file
    void WriteInstructionToFile(FILE* fp, const SRasterInstruction& instr) const;
    
private:
    std::string m_outputFile;
    int m_width;
    int m_height;
};

#endif // OUTPUT_MANAGER_H