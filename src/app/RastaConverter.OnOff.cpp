#include "RastaConverter.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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


