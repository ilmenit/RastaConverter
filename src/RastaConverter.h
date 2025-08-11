#ifndef RASTA_CONVERTER_H
#define RASTA_CONVERTER_H

#include "config.h"
#include "io/ImageProcessor.h"
#include "raster/RasterProgramGenerator.h"
#include "optimization/OptimizationController.h"
#include "io/OutputManager.h"
#include "FreeImage.h"
#include "OnOffMap.h"

#ifdef NO_GUI
#include "ui/RastaConsole.h"
#else
#include "ui/RastaSDL.h"
#endif

/**
 * Main class that coordinates the entire conversion process
 */
class RastaConverter {
public:
    /**
     * Constructor
     */
    RastaConverter();
    
    /**
     * Destructor
     */
    ~RastaConverter();
    
    /**
     * Set configuration parameters
     * 
     * @param config Configuration to use
     */
    void SetConfig(Configuration& config);
    
    /**
     * Initialize the converter
     * 
     * @return True if initialization succeeded
     */
    bool ProcessInit();
    
    /**
     * Run the main processing loop
     */
    void MainLoop();
    
    /**
     * Save the best solution found
     */
    void SaveBestSolution();
    
    /**
     * Resume a previously saved session
     * 
     * @return True if resume succeeded
     */
    bool Resume();
    
    /**
     * Load register initializations from file
     * 
     * @param filename File to load from
     */
    void LoadRegInits(const std::string& filename);
    
    /**
     * Load raster program from file
     * 
     * @param filename File to load from
     */
    void LoadRasterProgram(const std::string& filename);
    
    /**
     * Parse an instruction from a string
     * 
     * @param line String containing instruction
     * @param instr Instruction to populate
     * @return True if instruction was parsed successfully
     */
    bool GetInstructionFromString(const std::string& line, SRasterInstruction& instr);
    
    /**
     * Display a message in the UI
     * 
     * @param message Message to display
     */
    void Message(std::string message);
    
    /**
     * Display an error and exit
     * 
     * @param error Error message
     */
    void Error(std::string error);
    
private:
    /**
     * Load the Atari palette
     */
    void LoadAtariPalette();
    
    /**
     * Initialize the optimization
     */
    void Init();
    
    /**
     * Show the input bitmap in the UI
     */
    void ShowInputBitmap();
    
    /**
     * Show the destination bitmap in the UI
     */
    void ShowDestinationBitmap();
    
    /**
     * Show a single line of the destination bitmap
     * 
     * @param y Line to show
     */
    void ShowDestinationLine(int y);
    
    /**
     * Show the last created picture in the UI
     */
    void ShowLastCreatedPicture();
    
    /**
     * Show mutation statistics in the UI
     */
    void ShowMutationStats();
    
    /**
     * Load OnOff file for register usage map
     * 
     * @param filename File to load from
     */
    void LoadOnOffFile(const char* filename);
    
public:
    // Configuration
    Configuration cfg;
    
    // Components
    ImageProcessor m_imageProcessor;
    RasterProgramGenerator m_programGenerator;
    OptimizationController m_optimizer;
    OutputManager m_outputManager;
    
    // UI
#ifdef NO_GUI
    RastaConsole gui;
#else
    RastaSDL gui;
#endif
    
    // Optimization state
    bool init_finished;
    OnOffMap m_onOffMap;
    
    // Output bitmap for displaying results
    FIBITMAP* output_bitmap;
    // Dual GUI view mode: 0=blended, 1=A, 2=B
    int dual_view_mode = 0;
};

#endif // RASTA_CONVERTER_H