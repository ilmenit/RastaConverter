#include "RasterProgramGenerator.h"
#include "TargetPicture.h"
#include "color/rgb.h"
#include "mt19937int.h"
#include "utils/RandomUtils.h"
#include <algorithm>
#include <map>
#include <assert.h>
#include <cstring>
#include <FreeImage.h>

RasterProgramGenerator::RasterProgramGenerator()
    : m_width(0)
    , m_height(0)
{
}

void RasterProgramGenerator::Initialize(int width, int height)
{
    m_width = width;
    m_height = height;
}

void RasterProgramGenerator::CreateEmptyRasterPicture(raster_picture* r)
{
    // Reset memory registers
    memset(r->mem_regs_init, 0, sizeof(r->mem_regs_init));
    
    // Create a minimal instruction
    SRasterInstruction i;
    i.loose.instruction = E_RASTER_NOP;
    i.loose.target = E_COLBAK;
    i.loose.value = 0;
    
    // Initialize each line with a NOP instruction
    for (size_t y = 0; y < r->raster_lines.size(); ++y)
    {
        r->raster_lines[y].instructions.push_back(i);
        r->raster_lines[y].cycles += 2;
        r->raster_lines[y].rehash();
    }
}

void RasterProgramGenerator::CreateLowColorRasterPicture(raster_picture* r, const std::set<unsigned char>& colorIndexes)
{
    // Start with an empty picture
    CreateEmptyRasterPicture(r);
    
    // Assign colors to registers
    int i = 0;
    for (std::set<unsigned char>::iterator m = colorIndexes.begin(); 
         m != colorIndexes.end() && i < 4; // Only use first 4 color registers
         ++m, ++i)
    {
        r->mem_regs_init[E_COLOR0 + i] = (*m) * 2;
    }
}

void RasterProgramGenerator::CreateSmartRasterPicture(raster_picture* r, e_init_type initType, 
                                                   const std::vector<screen_line>& picture,
                                                   const std::set<unsigned char>& colorIndexes)
{
    SRasterInstruction i;
    int dest_colors;
    int dest_regs;
    int x, y;
    rgb color;

    // Reset memory registers
    memset(r->mem_regs_init, 0, sizeof(r->mem_regs_init));

    // Set number of color registers to use
    dest_regs = 8;

    // Set number of colors based on init type
    if (initType == E_INIT_LESS)
        dest_colors = dest_regs;
    else
        dest_colors = dest_regs + 4;

    // Create temporary bitmap for processing
    FIBITMAP* f_copy = FreeImage_Allocate(m_width, 1, 24);
    if (!f_copy) return;

    // Process each line
    for (y = 0; y < (int)r->raster_lines.size(); ++y)
    {
        // Copy current line to a bitmap for quantization
        RGBQUAD fpixel;
        for (x = 0; x < m_width; ++x)
        {
            const rgb& atari_color = picture[y][x];
            fpixel.rgbRed = atari_color.r;
            fpixel.rgbGreen = atari_color.g;
            fpixel.rgbBlue = atari_color.b;
            FreeImage_SetPixelColor(f_copy, x, 0, &fpixel);
        }
        
        // Convert to 24-bit and quantize colors
        FIBITMAP* f_copy24bits = FreeImage_ConvertTo24Bits(f_copy);
        FIBITMAP* f_quant = FreeImage_ColorQuantizeEx(f_copy24bits, FIQ_WUQUANT, dest_colors);
        FIBITMAP* f_copy24bits2;
        
        if (dest_colors > 4)
            f_copy24bits2 = FreeImage_ConvertTo24Bits(f_quant);
        else
            f_copy24bits2 = FreeImage_ConvertTo24Bits(f_copy);

        // Map colors by frequency
        std::map<int, int> color_map;
        std::map<int, int> color_position;
        std::multimap<int, int, std::greater<int>> sorted_colors;
        
        for (x = 0; x < m_width; ++x)
        {
            RGBQUAD fpixel;
            FreeImage_GetPixelColor(f_copy24bits2, x, 0, &fpixel);
            int c = fpixel.rgbRed + fpixel.rgbGreen * 0x100 + fpixel.rgbBlue * 0x10000;
            color_map[c]++;
            if (color_position.find(c) == color_position.end())
            {
                color_position[c] = x;
            }
        }

        // Sort by frequency
        for (auto iter = color_map.begin(); iter != color_map.end(); ++iter)
        {
            sorted_colors.insert(std::pair<int, int>(iter->second, iter->first));
        }

        // Create instructions for most frequent colors
        auto m = sorted_colors.begin();
        for (int k = 0; k < dest_regs && k < (int)sorted_colors.size(); ++k, ++m)
        {
            int c = m->second;
            color.r = c & 0xFF;
            color.g = (c >> 8) & 0xFF;
            color.b = (c >> 16) & 0xFF;

            // LDA/LDX/LDY instruction (cycling through registers)
            i.loose.instruction = (e_raster_instruction)(E_RASTER_LDA + k % 3);
            if (k > E_COLBAK && y % 2 == 1 && dest_colors > 4)
                i.loose.value = color_position[c] + sprite_screen_color_cycle_start; // sprite position
            else
                i.loose.value = FindAtariColorIndex(color) * 2;
            i.loose.target = E_COLOR0;
            r->raster_lines[y].instructions.push_back(i);
            r->raster_lines[y].cycles += 2;

            // STA/STX/STY instruction
            i.loose.instruction = (e_raster_instruction)(E_RASTER_STA + k % 3);
            i.loose.value = (random(128) * 2);

            if (k > E_COLBAK && y % 2 == 1 && dest_colors > 4)
                i.loose.target = (e_target)(k + 4); // sprite registers
            else
                i.loose.target = (e_target)k;
            r->raster_lines[y].instructions.push_back(i);
            r->raster_lines[y].cycles += 4;

            assert(r->raster_lines[y].cycles < free_cycles);
        }

        // Finalize the line
        r->raster_lines[y].rehash();

        // Clean up
        FreeImage_Unload(f_copy24bits);
        FreeImage_Unload(f_quant);
        FreeImage_Unload(f_copy24bits2);
    }
    
    // Clean up the temporary bitmap
    FreeImage_Unload(f_copy);
}

void RasterProgramGenerator::CreateRandomRasterPicture(raster_picture* r, 
                                                     const std::vector<screen_line>& picture)
{
    SRasterInstruction i;
    int x;
    
    // Reset memory registers
    memset(r->mem_regs_init, 0, sizeof(r->mem_regs_init));

    // Set random sprite positions and colors
    x = random(m_width);
    r->mem_regs_init[E_COLPM0] = FindAtariColorIndex(picture[0][x]) * 2;
    r->mem_regs_init[E_HPOSP0] = x + sprite_screen_color_cycle_start;

    x = random(m_width);
    r->mem_regs_init[E_COLPM1] = FindAtariColorIndex(picture[0][x]) * 2;
    r->mem_regs_init[E_HPOSP1] = x + sprite_screen_color_cycle_start;

    x = random(m_width);
    r->mem_regs_init[E_COLPM2] = FindAtariColorIndex(picture[0][x]) * 2;
    r->mem_regs_init[E_HPOSP2] = x + sprite_screen_color_cycle_start;

    x = random(m_width);
    r->mem_regs_init[E_COLPM3] = FindAtariColorIndex(picture[0][x]) * 2;
    r->mem_regs_init[E_HPOSP3] = x + sprite_screen_color_cycle_start;

    // Add random instructions to each line
    for (size_t y = 0; y < r->raster_lines.size(); ++y)
    {
        // LDA random color
        i.loose.instruction = E_RASTER_LDA;
        r->raster_lines[y].cycles += 2;
        x = random(m_width);
        i.loose.value = FindAtariColorIndex(picture[y][x]) * 2;
        i.loose.target = E_COLOR0;
        r->raster_lines[y].instructions.push_back(i);
        
        // STA COLOR0
        i.loose.instruction = E_RASTER_STA;
        r->raster_lines[y].cycles += 4;
        i.loose.value = (random(128) * 2);
        i.loose.target = E_COLOR0;
        r->raster_lines[y].instructions.push_back(i);

        // LDX random color
        i.loose.instruction = E_RASTER_LDX;
        r->raster_lines[y].cycles += 2;
        x = random(m_width);
        i.loose.value = FindAtariColorIndex(picture[y][x]) * 2;
        i.loose.target = E_COLOR1;
        r->raster_lines[y].instructions.push_back(i);
        
        // STX COLOR1
        i.loose.instruction = E_RASTER_STX;
        r->raster_lines[y].cycles += 4;
        i.loose.value = (random(128) * 2);
        i.loose.target = E_COLOR1;
        r->raster_lines[y].instructions.push_back(i);

        // LDY random color
        i.loose.instruction = E_RASTER_LDY;
        r->raster_lines[y].cycles += 2;
        x = random(m_width);
        i.loose.value = FindAtariColorIndex(picture[y][x]) * 2;
        i.loose.target = E_COLOR2;
        r->raster_lines[y].instructions.push_back(i);
        
        // STY COLOR2
        i.loose.instruction = E_RASTER_STY;
        r->raster_lines[y].cycles += 4;
        i.loose.value = (random(128) * 2);
        i.loose.target = E_COLOR2;
        r->raster_lines[y].instructions.push_back(i);

        // LDA random color
        i.loose.instruction = E_RASTER_LDA;
        r->raster_lines[y].cycles += 2;
        x = random(m_width);
        i.loose.value = FindAtariColorIndex(picture[y][x]) * 2;
        i.loose.target = E_COLBAK;
        r->raster_lines[y].instructions.push_back(i);
        
        // STA COLBAK
        i.loose.instruction = E_RASTER_STA;
        r->raster_lines[y].cycles += 4;
        i.loose.value = (random(128) * 2);
        i.loose.target = E_COLBAK;
        r->raster_lines[y].instructions.push_back(i);

        // Ensure we haven't exceeded cycle limit
        assert(r->raster_lines[y].cycles < free_cycles);
        
        // Finalize the line
        r->raster_lines[y].rehash();
    }
}

void RasterProgramGenerator::OptimizeRasterProgram(raster_picture* pic)
{
    struct previous_reg_usage {
        int i;  // Instruction index
        int y;  // Line index
    };
    
    // Initialize register usage tracking
    previous_reg_usage p_usage[3] = {
        { -1, -1 },  // A register
        { -1, -1 },  // X register
        { -1, -1 }   // Y register
    };

    // Scan through the program
    for (int y = 0; y < (int)pic->raster_lines.size(); ++y)
    {
        size_t size = pic->raster_lines[y].instructions.size();
        if (size == 0) {
            continue;
        }
        SRasterInstruction* __restrict rastinsns = &pic->raster_lines[y].instructions[0];
        
        for (size_t i = 0; i < size; ++i)
        {
            unsigned char ins = rastinsns[i].loose.instruction;
            
            // Check LDA/LDX/LDY instructions
            if (ins <= E_RASTER_LDY)
            {
                // If we have a previous load to this register that isn't used,
                // we can replace it with a NOP
                if (p_usage[ins].i != -1)
                {
                    pic->raster_lines[p_usage[ins].y].instructions[p_usage[ins].i].loose.instruction = E_RASTER_NOP;
                }
                
                // Update the register usage
                p_usage[ins].i = i;
                p_usage[ins].y = y;
            }
            // Check STA/STX/STY instructions
            else if (ins >= E_RASTER_STA)
            {
                // After a store, the register can be reused
                p_usage[ins - E_RASTER_STA].i = -1;
            }
        }
    }
}

void RasterProgramGenerator::TestRasterProgram(raster_picture* pic, std::vector<screen_line>& picture)
{
    int x, y;
    rgb white;
    rgb black;
    white.g = white.b = white.r = 255;
    black.g = black.b = black.r = 0;

    for (y = 0; y < m_height; ++y)
    {
        // Clear existing instructions
        pic->raster_lines[y].instructions.clear();
        pic->raster_lines[y].cycles = 6;
        
        // Add test instructions
        SRasterInstruction instr;
        instr.loose.instruction = E_RASTER_LDA;
        if (y % 2 == 0)
            instr.loose.value = 0xF;
        else
            instr.loose.value = 0x33;
        pic->raster_lines[y].instructions.push_back(instr);

        instr.loose.instruction = E_RASTER_STA;
        instr.loose.target = E_COLOR2;
        pic->raster_lines[y].instructions.push_back(instr);

        // Set black background with cycle markers in white
        for (x = 0; x < m_width; ++x)
            picture[y][x] = black;
            
        for (int i = 0; i < CYCLES_MAX; ++i)
        {
            x = screen_cycles[i].offset;
            if (x >= 0 && x < m_width)
                picture[y][x] = white;
        }
        
        // Finalize the line
        pic->raster_lines[y].rehash();
    }
}