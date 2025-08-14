#include "RastaConverter.h"
#include <fstream>
#include <sstream>
#include <algorithm>

void RastaConverter::SaveBestSolution()
{
    if (!init_finished)
        return;

    // Show the final picture (output_bitmap contains blended when dual-mode)
    ShowLastCreatedPicture();

    const auto& ctx = m_optimizer.GetEvaluationContext();
    if (ctx.m_dual_mode) {
        m_outputManager.SaveBestSolutionDual(
            ctx,
            output_bitmap,
            cfg.command_line,
            cfg.input_file
        );
    } else {
        m_outputManager.SaveBestSolution(
            ctx,
            output_bitmap,
            cfg.command_line,
            cfg.input_file
        );
    }
}

bool RastaConverter::Resume()
{
    // Load register initialization state
    // Use configured output base if available; otherwise default
    const std::string base = cfg.output_file.empty() ? std::string("output.png") : cfg.output_file;
    LoadRegInits(base + ".rp.ini");
    
    // Load raster program
    LoadRasterProgram(base + ".rp");
    
    // Load LAHC/DLAS state (state file is shared format for both)
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


