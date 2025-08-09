#include "RastaConverter.h"
#include "color/Distance.h"
#include "TargetPicture.h"
#include "mt19937int.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <climits>
#include "optimization/EvaluationContext.h"

// External const array for mutation names
extern const char* mutation_names[E_MUTATION_MAX];

RastaConverter::RastaConverter()
    : init_finished(false)
    , output_bitmap(nullptr)
{
}

RastaConverter::~RastaConverter()
{
    if (output_bitmap) {
        FreeImage_Unload(output_bitmap);
    }
}

void RastaConverter::SetConfig(Configuration& config)
{
    cfg = config;
}

void RastaConverter::Message(std::string message)
{
    if (!cfg.preprocess_only) {
        time_t t;
        t = time(NULL);
        string current_time = ctime(&t);
        current_time = current_time.substr(0, current_time.length() - 1);
        gui.DisplayText(0, 440, current_time + string(": ") + message + string("                    "));
    }
}

void RastaConverter::Error(std::string error)
{
    gui.Error(error);
    exit(1);
}

void RastaConverter::LoadAtariPalette()
{
    Message("Loading palette");
    if (!::LoadAtariPalette(cfg.palette_file))
        Error("Error opening .act palette file");
}

bool RastaConverter::ProcessInit()
{
    // Initialize GUI
    if (!cfg.quiet) {
        gui.Init(cfg.command_line);
    }

    // Load Atari palette
    LoadAtariPalette();
    
    // Initialize image processor
    m_imageProcessor.Initialize(cfg);
    
    // Load input bitmap
    if (!m_imageProcessor.LoadInputBitmap())
        Error("Error loading Input Bitmap!");
    
    // Initialize local structure
    m_imageProcessor.InitLocalStructure();
    
    // Show input image
    if (!cfg.preprocess_only)
        ShowInputBitmap();
    
    // Set preprocessing distance function
    SetDistanceFunction(cfg.pre_dstf);
    
    // Prepare destination picture (apply dithering and transformations)
    m_imageProcessor.PrepareDestinationPicture();
    
    // Save the preprocessed image
    m_outputManager.Initialize(cfg.output_file, m_imageProcessor.GetWidth(), m_imageProcessor.GetHeight());
    m_outputManager.SavePicture(cfg.output_file + "-src.png", m_imageProcessor.GetInputBitmap());
    m_outputManager.SavePicture(cfg.output_file + "-dst.png", m_imageProcessor.GetDestinationBitmap());

    // Ensure Destination preview is drawn early (before entering MainLoop)
    if (!cfg.preprocess_only) {
        ShowDestinationBitmap();
    }
    
    // Exit if only preprocessing was requested
    if (cfg.preprocess_only)
        return false; // signal to caller to skip MainLoop after saving preprocessed images
    
    // Load OnOff map if specified
    if (!cfg.on_off_file.empty())
        LoadOnOffFile(cfg.on_off_file.c_str());
    
    // Set post-processing distance function
    SetDistanceFunction(cfg.dstf);
    
    // Generate picture error map
    m_imageProcessor.GeneratePictureErrorMap();
    
    // Find possible colors for each line
    m_imageProcessor.FindPossibleColors();
    
    // Initialize program generator
    m_programGenerator.Initialize(m_imageProcessor.GetWidth(), m_imageProcessor.GetHeight());
    
    // Initialize optimizer with region-based mutation from config
    bool useRegionalMutation = (cfg.mutation_strategy == E_MUTATION_REGIONAL);
    
    const std::vector<distance_t>* pictureAllErrors[128];
    for (int i = 0; i < 128; ++i)
        pictureAllErrors[i] = &m_imageProcessor.GetPictureAllErrors()[i];
    
    m_optimizer.Initialize(
        cfg.threads, 
        m_imageProcessor.GetWidth(), 
        m_imageProcessor.GetHeight(), 
        &m_imageProcessor.GetPicture(),
        pictureAllErrors,
        cfg.max_evals,
        cfg.save_period,
        cfg.cache_size,
        (cfg.continue_processing && cfg.have_resume_seed) ? cfg.resume_seed : cfg.initial_seed,
        &m_onOffMap,
        useRegionalMutation
    );
    
    // Allocate output bitmap
    output_bitmap = FreeImage_Allocate(
        m_imageProcessor.GetWidth(), 
        m_imageProcessor.GetHeight(), 
        24
    );
    
    return true;
}

void RastaConverter::Init()
{
    if (!cfg.continue_processing)
    {
        // Create initial raster picture based on configuration
        raster_picture m(m_imageProcessor.GetHeight());
        init_finished = false;

        const auto& colorIndexes = m_imageProcessor.GetColorIndexesOnDstPicture();
        
        if (colorIndexes.size() < 5)
            m_programGenerator.CreateLowColorRasterPicture(&m, colorIndexes);
        else if (cfg.init_type == E_INIT_RANDOM)
            m_programGenerator.CreateRandomRasterPicture(&m, m_imageProcessor.GetPicture());
        else if (cfg.init_type == E_INIT_EMPTY)
            m_programGenerator.CreateEmptyRasterPicture(&m);
        else // LESS or SMART
            m_programGenerator.CreateSmartRasterPicture(
                &m, 
                cfg.init_type, 
                m_imageProcessor.GetPicture(), 
                colorIndexes
            );
        
        // Set initial program for optimization
        m_optimizer.SetInitialProgram(m, m_imageProcessor.GetPossibleColorsForEachLine());
    }

    init_finished = true;
}

void RastaConverter::MainLoop()
{
    Message("Optimization started.");
    
    // Initialize optimization
    Init();
    
    // Start optimization
    m_optimizer.Run();
    
    // Track timing and performance
    bool pending_update = false;
    
    // Run until finished
    bool running = true;
    while (running && !m_optimizer.IsFinished())
    {
        static auto last_heartbeat = std::chrono::steady_clock::now();
        auto now_hb = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now_hb - last_heartbeat).count() >= 5) {
            auto& evalContext = m_optimizer.GetEvaluationContext();
            std::unique_lock<std::mutex> lock{ evalContext.m_mutex };
            std::cout << "[HB] UI loop heartbeat: finished=" << (evalContext.m_finished.load() ? 1 : 0)
                      << ", threads_active=" << evalContext.m_threads_active.load()
                      << ", evals=" << evalContext.m_evaluations << std::endl;
            last_heartbeat = now_hb;
        }
        // Update statistics
        m_optimizer.UpdateRate();
        
        // Check for auto-save
        if (m_optimizer.CheckAutoSave()) {
            SaveBestSolution();
            Message("Auto-saved.");
        }
        
        // Update display periodically
        if (pending_update)
        {
            pending_update = false;
            ShowLastCreatedPicture();
        }
        
        // Show mutation statistics
        ShowMutationStats();
        
        // Process UI commands
        auto ui_cmd = gui.NextFrame();
        #ifdef UI_DEBUG
        std::cout << "[UI] NextFrame returned " << (int)ui_cmd << std::endl;
        #endif
        switch (ui_cmd)
        {
        case GUI_command::SAVE:
            SaveBestSolution();
            Message("Saved.");
            break;
        case GUI_command::STOP:
            std::cout << "[RC] STOP command received from GUI" << std::endl;
            running = false;
            break;
        case GUI_command::REDRAW:
            ShowInputBitmap();
            ShowDestinationBitmap();
            ShowLastCreatedPicture();
            ShowMutationStats();
            break;
        default:
            break;
        }
        
        // Check if optimization state has changed
        auto& evalContext = m_optimizer.GetEvaluationContext();
        bool do_init_redraw = false;
        bool do_autosave = false;
        {
            std::unique_lock<std::mutex> lock{ evalContext.m_mutex };
            
            // Handle initialization completion (first frame data available)
            if (evalContext.m_update_initialized)
            {
                evalContext.m_update_initialized = false;
                do_init_redraw = true;
            }
            
            // Handle improvement updates
            if (evalContext.m_update_improvement)
            {
                evalContext.m_update_improvement = false;
                pending_update = true;
            }
            
            // Handle autosave updates
            if (evalContext.m_update_autosave)
            {
                evalContext.m_update_autosave = false;
                do_autosave = true;
            }
            
            // Wait for updates or timeout
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            evalContext.m_condvar_update.wait_until(lock, deadline);

            // Heartbeat: detect stagnation where no threads are active but not finished
            if (!evalContext.m_finished.load() && evalContext.m_threads_active.load() <= 0) {
                std::cout << "[RC] No active workers but not finished. Requesting DLAS re-spawn." << std::endl;
            }
        }

        // Perform UI updates and autosave outside of the lock to avoid deadlocks
        if (do_init_redraw)
        {
            ShowInputBitmap();
            ShowDestinationBitmap();
            ShowLastCreatedPicture();
            ShowMutationStats();
        }
        if (do_autosave)
        {
            SaveBestSolution();
            Message("Auto-saved.");
        }
    }
    
    // Stop optimization (controller will forward Stop to optimizer and log)
    m_optimizer.Stop();
    
    // Log exit state
    {
        auto& evalContext = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ evalContext.m_mutex };
        std::cout << "[RC] MainLoop exiting: running=false, ctx.finished=" << (evalContext.m_finished.load() ? 1 : 0)
                  << ", threads_active=" << evalContext.m_threads_active.load()
                  << ", evals=" << evalContext.m_evaluations << std::endl;
        if (evalContext.m_finished.load()) {
            std::cout << "[RC] Finish reason='" << evalContext.m_finish_reason
                      << "' at " << evalContext.m_finish_file << ":" << evalContext.m_finish_line
                      << ", evals_at=" << evalContext.m_finish_evals_at
                      << ", threads_active_at=" << evalContext.m_threads_active.load()
                      << ", best_result=" << evalContext.m_best_result
                      << ", cost_max=" << evalContext.m_cost_max
                      << ", N=" << evalContext.m_N
                      << ", current_cost=" << evalContext.m_current_cost
                      << ", previous_idx=" << evalContext.m_previous_results_index
                      << std::endl;
        }
    }

    // Save final solution
    SaveBestSolution();
}

void RastaConverter::LoadOnOffFile(const char* filename)
{
    memset(m_onOffMap.on_off, true, sizeof(m_onOffMap.on_off));

    // Open the file
    std::ifstream f(filename);
    if (f.fail())
        Error("Error loading OnOff file");

    std::string line;
    unsigned int y = 1;
    while (std::getline(f, line))
    {
        if (line.empty())
            continue;
        std::transform(line.begin(), line.end(), line.begin(), ::toupper);

        std::stringstream sl(line);
        std::string reg, value;
        e_target target = E_TARGET_MAX;
        unsigned int from, to;

        sl >> reg >> value >> from >> to;

        if (sl.rdstate() & std::ios::failbit) // failed to parse arguments?
        {
            std::string err = "Error parsing OnOff file in line ";
            err += std::to_string(y);
            err += "\n";
            err += line;
            Error(err.c_str());
        }
        if (!(value == "ON" || value == "OFF"))
        {
            std::string err = "OnOff file: Second parameter should be ON or OFF in line ";
            err += std::to_string(y);
            err += "\n";
            err += line;
            Error(err.c_str());
        }
        if (from > 239 || to > 239) // on_off table size
        {
            std::string err = "OnOff file: Range value greater than 239 line ";
            err += std::to_string(y);
            err += "\n";
            err += line;
            Error(err.c_str());
        }

        if ((int)from > m_imageProcessor.GetHeight() - 1 || (int)to > m_imageProcessor.GetHeight() - 1)
        {
            std::string err = "OnOff file: Range value greater than picture height in line ";
            err += std::to_string(y);
            err += "\n";
            err += line;
            err += "\n";
            err += "Set range from 0 to ";
            err += std::to_string(m_imageProcessor.GetHeight() - 1);
            Error(err.c_str());
        }
        
        // Find the target register
        for (size_t i = 0; i < E_TARGET_MAX; ++i)
        {
            if (reg == std::string(OutputManager::mem_regs_names[i]))
            {
                target = (e_target)i;
                break;
            }
        }
        
        if (target == E_TARGET_MAX)
        {
            std::string err = "OnOff file: Unknown register " + reg;
            err += " in line ";
            err += std::to_string(y);
            err += "\n";
            err += line;
            Error(err.c_str());
        }
        
        // Fill the on/off map
        for (size_t l = from; l <= to; ++l)
        {
            m_onOffMap.on_off[l][target] = (value == "ON");
        }
        
        ++y;
    }
}

void RastaConverter::ShowInputBitmap()
{
    unsigned int width = FreeImage_GetWidth(m_imageProcessor.GetInputBitmap());
    unsigned int height = FreeImage_GetHeight(m_imageProcessor.GetInputBitmap());
    gui.DisplayBitmap(0, 0, m_imageProcessor.GetInputBitmap());
    gui.DisplayText(0, height + 10, "Source");
    gui.DisplayText(width * 2, height + 10, "Current output");
    gui.DisplayText(width * 4, height + 10, "Destination");
}

void RastaConverter::ShowDestinationLine(int y)
{
    if (!cfg.preprocess_only)
    {
        unsigned int width = FreeImage_GetWidth(m_imageProcessor.GetDestinationBitmap());
        unsigned int where_x = FreeImage_GetWidth(m_imageProcessor.GetInputBitmap());

        gui.DisplayBitmapLine(where_x, y, y, m_imageProcessor.GetDestinationBitmap());
    }
}

void RastaConverter::ShowDestinationBitmap()
{
    gui.DisplayBitmap(FreeImage_GetWidth(m_imageProcessor.GetDestinationBitmap()) * 2, 
                     0, 
                     m_imageProcessor.GetDestinationBitmap());
}

void RastaConverter::ShowLastCreatedPicture()
{
    // Snapshot evaluation data under lock to avoid data races
    std::vector<std::vector<unsigned char>> created_picture_copy;
    unsigned snap_width = 0;
    unsigned snap_height = 0;
    {
        auto& ctx = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ ctx.m_mutex };
        snap_width = ctx.m_width;
        snap_height = ctx.m_height;
        created_picture_copy = ctx.m_created_picture; // copy
    }
    int x, y;

    // Check if the created picture exists and has the right dimensions
    if (created_picture_copy.empty() ||
        created_picture_copy.size() < snap_height) {
        // Just fill with black if not initialized yet
        RGBQUAD black = { 0, 0, 0, 0 };
        for (y = 0; y < (int)snap_height; ++y) {
            for (x = 0; x < (int)snap_width; ++x) {
                FreeImage_SetPixelColor(output_bitmap, x, y, &black);
            }
        }
    }
    else {
        // Draw the created picture
        for (y = 0; y < (int)snap_height; ++y) {
            // Skip any rows that aren't sized correctly
            if (created_picture_copy[y].size() < snap_width) {
                continue;
            }

            for (x = 0; x < (int)snap_width; ++x) {
                rgb atari_color = atari_palette[created_picture_copy[y][x]];
                RGBQUAD color;
                color.rgbRed = atari_color.r;
                color.rgbGreen = atari_color.g;
                color.rgbBlue = atari_color.b;
                FreeImage_SetPixelColor(output_bitmap, x, y, &color);
            }
        }
    }

    // Display the picture
    int w = FreeImage_GetWidth(output_bitmap);
    gui.DisplayBitmap(FreeImage_GetWidth(m_imageProcessor.GetInputBitmap()), 0, output_bitmap);
}

void RastaConverter::ShowMutationStats()
{
    // Snapshot under lock
    unsigned long long evaluations = 0;
    unsigned long long last_best_eval = 0;
    double best_result = 0.0;
    int stats[E_MUTATION_MAX] = {0};
    {
        auto& ctx = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ ctx.m_mutex };
        evaluations = ctx.m_evaluations;
        last_best_eval = ctx.m_last_best_evaluation;
        best_result = ctx.m_best_result;
        for (int i = 0; i < E_MUTATION_MAX; ++i) stats[i] = ctx.m_mutation_stats[i];
    }

    for (int i = 0; i < E_MUTATION_MAX; ++i)
    {
        gui.DisplayText(0, 250 + 20 * i, std::string(mutation_names[i]) +
                                        std::string("  ") +
                                        std::to_string(stats[i]));
    }

    gui.DisplayText(320, 250, std::string("Evaluations: ") + std::to_string(evaluations));
    gui.DisplayText(320, 270, std::string("LastBest: ") + std::to_string(last_best_eval) + std::string("                "));
    gui.DisplayText(320, 290, std::string("Rate: ") + std::to_string((unsigned long long)m_optimizer.GetRate()) + std::string("                "));
    gui.DisplayText(320, 310, std::string("Norm. Dist: ") + std::to_string(m_outputManager.NormalizeScore(best_result)) + std::string("                "));
}

void RastaConverter::SaveBestSolution()
{
    if (!init_finished)
        return;

    // Show the final picture
    ShowLastCreatedPicture();
    
    // Save all files
    m_outputManager.SaveBestSolution(
        m_optimizer.GetEvaluationContext(),
        output_bitmap,
        cfg.command_line,
        cfg.input_file
    );
}

bool RastaConverter::Resume()
{
    // Load register initialization state
    // Use configured output base if available; otherwise default
    const std::string base = cfg.output_file.empty() ? std::string("output.png") : cfg.output_file;
    LoadRegInits(base + ".rp.ini");
    
    // Load raster program
    LoadRasterProgram(base + ".rp");
    
    // Load LAHC state
    m_optimizer.LoadState(base + ".lahc");
    
    // Process command line for config
    cfg.ProcessCmdLine();
    
    // Mark as continuing
    cfg.continue_processing = true;
    
    return true;
}

void RastaConverter::LoadRegInits(const std::string& filename)
{
    Message("Loading Register Initializations");

    std::ifstream f(filename.c_str());
    if (f.fail())
        Error("Error loading register initializations");

    std::string line;
    SRasterInstruction instr;

    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;

    while (std::getline(f, line))
    {
        instr.loose.target = E_TARGET_MAX;
        if (GetInstructionFromString(line, instr))
        {
            switch (instr.loose.instruction)
            {
            case E_RASTER_LDA:
                a = instr.loose.value;
                break;
            case E_RASTER_LDX:
                x = instr.loose.value;
                break;
            case E_RASTER_LDY:
                y = instr.loose.value;
                break;
            case E_RASTER_STA:
                if (instr.loose.target != E_TARGET_MAX)
                    m_optimizer.GetEvaluationContext().m_best_pic.mem_regs_init[instr.loose.target] = a;
                break;
            case E_RASTER_STX:
                if (instr.loose.target != E_TARGET_MAX)
                    m_optimizer.GetEvaluationContext().m_best_pic.mem_regs_init[instr.loose.target] = x;
                break;
            case E_RASTER_STY:
                if (instr.loose.target != E_TARGET_MAX)
                    m_optimizer.GetEvaluationContext().m_best_pic.mem_regs_init[instr.loose.target] = y;
                break;
            default:
                break;
            }
        }
    }
}

void RastaConverter::LoadRasterProgram(const std::string& filename)
{
    Message("Loading Raster Program");

    std::ifstream f(filename.c_str());
    if (f.fail())
        Error("Error loading Raster Program");

    std::string line;

    SRasterInstruction instr;
    raster_line current_raster_line;
    current_raster_line.cycles = 0;
    size_t pos;
    bool line_started = false;

    auto& eval_context = m_optimizer.GetEvaluationContext();
    eval_context.m_best_pic.raster_lines.clear();

    while (std::getline(f, line))
    {
        // skip filler
        if (line.find("; filler") != std::string::npos)
            continue;

        // get info about the file
        pos = line.find("; Evaluations:");
        if (pos != std::string::npos)
            eval_context.m_evaluations = std::stoul(line.substr(pos + 15));

        pos = line.find("; InputName:");
        if (pos != std::string::npos)
            cfg.input_file = (line.substr(pos + 13));

        pos = line.find("; CmdLine:");
        if (pos != std::string::npos)
            cfg.command_line = (line.substr(pos + 11));

        // Optional Seed: stored if present to keep RNG continuity on /continue
        pos = line.find("; Seed:");
        if (pos != std::string::npos) {
            try {
                cfg.resume_seed = std::stoul(line.substr(pos + 7));
                cfg.have_resume_seed = true;
            } catch (...) {
                cfg.have_resume_seed = false;
            }
        }

        if (line.compare(0, 4, "line", 4) == 0)
        {
            line_started = true;
            continue;
        }

        if (!line_started)
            continue;

        // if next raster line
        if (line.find("cmp byt2") != std::string::npos && current_raster_line.cycles > 0)
        {
            current_raster_line.rehash();
            eval_context.m_best_pic.raster_lines.push_back(current_raster_line);
            current_raster_line.cycles = 0;
            current_raster_line.instructions.clear();
            line_started = false;
            continue;
        }

        // add instruction to raster program if proper instruction
        if (GetInstructionFromString(line, instr))
        {
            current_raster_line.cycles += GetInstructionCycles(instr);
            current_raster_line.instructions.push_back(instr);
        }
    }
}

bool RastaConverter::GetInstructionFromString(const std::string& line, SRasterInstruction& instr)
{
    static const char* load_names[3] =
    {
        "lda",
        "ldx",
        "ldy",
    };
    static const char* store_names[3] =
    {
        "sta",
        "stx",
        "sty",
    };

    size_t pos_comment, pos_instr, pos_value, pos_target;

    if (line.find(":") != std::string::npos)
        return false;

    pos_comment = line.find(";");
    if (pos_comment == std::string::npos)
        pos_comment = INT_MAX;

    pos_value = line.find("$");

    size_t i, j;

    instr.loose.instruction = E_RASTER_MAX;

    if (line.find("nop") != std::string::npos)
    {
        instr.loose.instruction = E_RASTER_NOP;
        instr.loose.value = 0;
        instr.loose.target = E_COLBAK;
        return true;
    }

    // check load instructions
    for (i = 0; i < 3; ++i)
    {
        pos_instr = line.find(load_names[i]);
        if (pos_instr != std::string::npos)
        {
            if (pos_instr < pos_comment)
            {
                instr.loose.instruction = (e_raster_instruction)(E_RASTER_LDA + i);
                pos_value = line.find("$");
                if (pos_value == std::string::npos)
                    gui.Error("Load instruction: No value for Load Register");
                ++pos_value;
                std::string val_string = line.substr(pos_value, 2);
                instr.loose.value = std::stoi(val_string, nullptr, 16);
                instr.loose.target = E_TARGET_MAX;
                return true;
            }
        }
    }
    
    // check store instructions
    for (i = 0; i < 3; ++i)
    {
        pos_instr = line.find(store_names[i]);
        if (pos_instr != std::string::npos)
        {
            if (pos_instr < pos_comment)
            {
                instr.loose.instruction = (e_raster_instruction)(E_RASTER_STA + i);
                // find target
                for (j = 0; j < E_TARGET_MAX; ++j)
                {
                    pos_target = line.find(OutputManager::mem_regs_names[j]);
                    if (pos_target != std::string::npos)
                    {
                        instr.loose.target = (e_target)(E_COLOR0 + j);
                        instr.loose.value = 0;
                        return true;
                    }
                }
                gui.Error("Load instruction: Unknown target for store");
            }
        }
    }
    return false;
}