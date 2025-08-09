#include "OutputManager.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <FreeImage.h>

// Define register names array
const char* OutputManager::mem_regs_names[E_TARGET_MAX + 1] = {
    "COLOR0",
    "COLOR1",
    "COLOR2",
    "COLBAK",
    "COLPM0",
    "COLPM1",
    "COLPM2",
    "COLPM3",
    "HPOSP0",
    "HPOSP1",
    "HPOSP2",
    "HPOSP3",
    "HITCLR",
};

OutputManager::OutputManager()
    : m_width(0)
    , m_height(0)
{
}

void OutputManager::Initialize(const std::string& outputFile, int width, int height)
{
    m_outputFile = outputFile;
    m_width = width;
    m_height = height;
}

// Function to rescale FIBITMAP to double its width
FIBITMAP* RescaleFIBitmapDoubleWidth(FIBITMAP* originalFiBitmap) {
    int originalWidth = FreeImage_GetWidth(originalFiBitmap);
    int originalHeight = FreeImage_GetHeight(originalFiBitmap);

    // Calculate the new width as double the original width
    int newWidth = originalWidth * 2;

    // Use FreeImage_Rescale to create a new bitmap with the new dimensions
    FIBITMAP* rescaledFiBitmap = FreeImage_Rescale(originalFiBitmap, newWidth, originalHeight, FILTER_BOX);

    return rescaledFiBitmap;
}

bool OutputManager::SavePicture(const std::string& filename, FIBITMAP* to_save)
{
    // Create a stretched version to show scanlines at 2x width
    FIBITMAP* stretched = RescaleFIBitmapDoubleWidth(to_save);

    // Flip the image vertically for correct orientation
    FreeImage_FlipVertical(stretched);

    // Save the image as a PNG
    if (FreeImage_Save(FIF_PNG, stretched, filename.c_str()))
    {
        // If the image is saved successfully
        FreeImage_Unload(stretched);
        return true;
    }
    else
    {
        std::cerr << "Error saving picture: " << filename << std::endl;
        FreeImage_Unload(stretched);
        return false;
    }
}

void OutputManager::SaveRasterProgram(const std::string& filename, 
                                    const raster_picture* pic,
                                    const std::string& cmdLine,
                                    const std::string& inputFile,
                                    unsigned long long evaluations,
                                    double score)
{
    // First save the initialization values
    FILE* fp = fopen((filename + ".ini").c_str(), "wt+");
    if (!fp) {
        std::cerr << "Error saving raster program initialization" << std::endl;
        return;
    }

    // Write header
    fprintf(fp, "; ---------------------------------- \n");
    fprintf(fp, "; RastaConverter\n");
    fprintf(fp, "; ---------------------------------- \n");

    fprintf(fp, "\n; Initial values \n");

    // Write initial register values
    for (size_t y = 0; y < sizeof(pic->mem_regs_init); ++y)
    {
        fprintf(fp, "\tlda ");
        fprintf(fp, "#$%02X\n", pic->mem_regs_init[y]);
        fprintf(fp, "\tsta ");
        fprintf(fp, "%s\n", mem_regs_names[y]);
    }

    // Zero registers
    fprintf(fp, "\tlda #$0\n");
    fprintf(fp, "\ttax\n");
    fprintf(fp, "\ttay\n");

    // Set proper count of wsyncs
    fprintf(fp, "\n; Set proper count of wsyncs \n");
    fprintf(fp, "\n\t:2 sta wsync\n");

    // Set proper picture height
    fprintf(fp, "\n; Set proper picture height\n");
    fprintf(fp, "\n\nPIC_HEIGHT = %d\n", m_height);

    fclose(fp);

    // Now save the main raster program
    fp = fopen(filename.c_str(), "wt+");
    if (!fp) {
        std::cerr << "Error saving raster program" << std::endl;
        return;
    }

    // Write header
    fprintf(fp, "; ---------------------------------- \n");
    fprintf(fp, "; RastaConverter\n");
    fprintf(fp, "; InputName: %s\n", inputFile.c_str());
    fprintf(fp, "; CmdLine: %s\n", cmdLine.c_str());
    fprintf(fp, "; Evaluations: %llu\n", evaluations);
    fprintf(fp, "; Score: %g\n", NormalizeScore(score));
    // Persist seed if present in command line (best-effort parse)
    {
        const char* seed_key = "/seed=";
        auto pos = cmdLine.find(seed_key);
        if (pos != std::string::npos) {
            pos += strlen(seed_key);
            size_t end = cmdLine.find(' ', pos);
            std::string seed_str = cmdLine.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            fprintf(fp, "; Seed: %s\n", seed_str.c_str());
        }
    }
    fprintf(fp, "; ---------------------------------- \n");

    // Proper offset
    fprintf(fp, "; Proper offset \n");
    fprintf(fp, "\tnop\n");
    fprintf(fp, "\tnop\n");
    fprintf(fp, "\tnop\n");
    fprintf(fp, "\tnop\n");
    fprintf(fp, "\tcmp byt2;\n");

    // Write each raster line
    for (int y = 0; y < m_height; ++y)
    {
        fprintf(fp, "line%d\n", y);
        size_t prog_len = pic->raster_lines[y].instructions.size();
        
        // Write each instruction
        for (size_t i = 0; i < prog_len; ++i)
        {
            SRasterInstruction instr = pic->raster_lines[y].instructions[i];
            WriteInstructionToFile(fp, instr);
        }

        // Add filler NOPs to reach the cycle limit
        for (int cycle = pic->raster_lines[y].cycles; cycle < free_cycles; cycle += 2)
        {
            fprintf(fp, "\tnop ; filler\n");
        }
        
        fprintf(fp, "\tcmp byt2; on zero page so 3 cycles\n");
    }
    
    fprintf(fp, "; ---------------------------------- \n");
    fclose(fp);
}

void OutputManager::WriteInstructionToFile(FILE* fp, const SRasterInstruction& instr) const
{
    bool save_target = false;
    bool save_value = false;
    
    fprintf(fp, "\t");
    switch (instr.loose.instruction)
    {
    case E_RASTER_LDA:
        fprintf(fp, "lda ");
        save_value = true;
        break;
    case E_RASTER_LDX:
        fprintf(fp, "ldx ");
        save_value = true;
        break;
    case E_RASTER_LDY:
        fprintf(fp, "ldy ");
        save_value = true;
        break;
    case E_RASTER_NOP:
        fprintf(fp, "nop ");
        break;
    case E_RASTER_STA:
        fprintf(fp, "sta ");
        save_target = true;
        break;
    case E_RASTER_STX:
        fprintf(fp, "stx ");
        save_target = true;
        break;
    case E_RASTER_STY:
        fprintf(fp, "sty ");
        save_target = true;
        break;
    default:
        std::cerr << "Unknown instruction!" << std::endl;
        return;
    }
    
    if (save_value)
    {
        fprintf(fp, "#$%02X ; %d (spr=%d)", instr.loose.value, instr.loose.value, instr.loose.value - 48);
    }
    else if (save_target)
    {
        if (instr.loose.target > E_TARGET_MAX)
        {
            std::cerr << "Unknown target in instruction!" << std::endl;
            return;
        }
        fprintf(fp, "%s", mem_regs_names[instr.loose.target]);
    }
    
    fprintf(fp, "\n");
}

void OutputManager::SavePMG(const std::string& filename, const sprites_memory_t& spritesMemory)
{
    size_t sprite, y, bit;
    unsigned char b;

    FILE* fp = fopen(filename.c_str(), "wt+");
    if (!fp) {
        std::cerr << "Error saving PMG handler" << std::endl;
        return;
    }

    fprintf(fp, "; ---------------------------------- \n");
    fprintf(fp, "; RastaConverter\n");
    fprintf(fp, "; ---------------------------------- \n");

    fprintf(fp, "missiles\n");
    fprintf(fp, "\t.ds $100\n");

    for (sprite = 0; sprite < 4; ++sprite)
    {
        fprintf(fp, "player%d\n", (int)sprite);
        fprintf(fp, "\t.he 00 00 00 00 00 00 00 00");
        for (y = 0; y < 240; ++y)
        {
            b = 0;
            for (bit = 0; bit < 8; ++bit)
            {
                if (y > (size_t)m_height)
                    continue;

                b |= (spritesMemory[y][sprite][bit]) << (7 - bit);
            }
            fprintf(fp, " %02X", b);
            if (y % 16 == 7)
                fprintf(fp, "\n\t.he");
        }
        fprintf(fp, " 00 00 00 00 00 00 00 00\n");
    }
    fclose(fp);
}

unsigned char OutputManager::ConvertColorRegisterToRawData(e_target t) const
{
    if (t > E_COLBAK)
        t = E_COLBAK;
    switch (t)
    {
    case E_COLBAK:
        return 0;
    case E_COLOR0:
        return 1;
    case E_COLOR1:
        return 2;
    case E_COLOR2:
        return 3;
    default:
        return 0;
    }
}

bool OutputManager::SaveScreenData(const std::string& filename,
                                  const std::vector<color_index_line>& createdPicture,
                                  const std::vector<line_target>& createdPictureTargets)
{
    int x, y, a = 0, b = 0, c = 0, d = 0;
    FILE* fp = fopen(filename.c_str(), "wb+");
    if (!fp) {
        std::cerr << "Error saving MIC screen data" << std::endl;
        return false;
    }

    for (y = 0; y < m_height; ++y)
    {
        // encode 4 pixel colors in byte
        for (x = 0; x < m_width; x += 4)
        {
            unsigned char pix = 0;
            a = ConvertColorRegisterToRawData((e_target)createdPictureTargets[y][x]);
            b = ConvertColorRegisterToRawData((e_target)createdPictureTargets[y][x + 1]);
            c = ConvertColorRegisterToRawData((e_target)createdPictureTargets[y][x + 2]);
            d = ConvertColorRegisterToRawData((e_target)createdPictureTargets[y][x + 3]);
            pix |= a << 6;
            pix |= b << 4;
            pix |= c << 2;
            pix |= d;
            fwrite(&pix, 1, 1, fp);
        }
    }
    fclose(fp);
    return true;
}

void OutputManager::SaveStatistics(const std::string& filename, 
                                  const statistics_list& statistics)
{
    FILE* fp = fopen(filename.c_str(), "w");
    if (!fp) {
        std::cerr << "Error saving statistics" << std::endl;
        return;
    }

    fprintf(fp, "Iterations,Seconds,Score\n");
    for (const auto& pt : statistics)
    {
        fprintf(fp, "%u,%u,%.6f\n", pt.evaluations, pt.seconds, NormalizeScore(pt.distance));
    }

    fclose(fp);
}

void OutputManager::SaveLAHC(const std::string& filename,
                            const std::vector<double>& previousResults,
                            size_t previousResultsIndex,
                            double costMax,
                            int N,
                            double currentCost)
{
    FILE* fp = fopen(filename.c_str(), "wt+");
    if (!fp) {
        std::cerr << "Error saving LAHC state" << std::endl;
        return;
    }

    fprintf(fp, "%lu\n", (unsigned long)previousResults.size());
    fprintf(fp, "%lu\n", (unsigned long)previousResultsIndex);
    fprintf(fp, "%Lf\n", (long double)costMax);
    fprintf(fp, "%d\n", N);
    fprintf(fp, "%Lf\n", (long double)currentCost);

    for (size_t i = 0; i < previousResults.size(); ++i) {
        fprintf(fp, "%Lf\n", (long double)previousResults[i]);
    }
    fclose(fp);
}

void OutputManager::SaveBestSolution(const EvaluationContext& evalContext,
                                    FIBITMAP* outputBitmap,
                                    const std::string& cmdLine,
                                    const std::string& inputFile)
{
    // Create optimized version of the raster program
    raster_picture pic = evalContext.m_best_pic;
    
    // Save best solution in all formats
    SaveRasterProgram(m_outputFile + ".rp", &pic, cmdLine, inputFile, 
                     evalContext.m_evaluations, evalContext.m_best_result);
    
    // Output PMG data
    SavePMG(m_outputFile + ".pmg", evalContext.m_sprites_memory);
    
    // Output screen data
    SaveScreenData(m_outputFile + ".mic", 
                  evalContext.m_created_picture, 
                  evalContext.m_created_picture_targets);
    
    // Save the output image
    SavePicture(m_outputFile, outputBitmap);
    
    // Save statistics and optimization state
    SaveStatistics(m_outputFile + ".csv", evalContext.m_statistics);
    SaveLAHC(m_outputFile + ".lahc", 
            evalContext.m_previous_results,
            evalContext.m_previous_results_index,
            evalContext.m_cost_max,
            evalContext.m_N,
            evalContext.m_current_cost);
}

double OutputManager::NormalizeScore(double rawScore) const
{
    // Normalize the raw score to a 0-1 range based on image size
    return rawScore / (((double)m_width * (double)m_height) * (MAX_COLOR_DISTANCE / 10000));
}