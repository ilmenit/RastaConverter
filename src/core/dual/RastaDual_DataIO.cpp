// Dual-mode helpers for updating created/targets and saving PMG/screen data
#include "rasta.h"
#include <cstdio>
#include <fstream>
#include <iomanip>

extern const char* program_version;
unsigned char ConvertColorRegisterToRawData(e_target t);

void RastaConverter::UpdateCreatedFromResults(const std::vector<const line_cache_result*>& results,
	std::vector< std::vector<unsigned char> >& out_created)
{
	out_created.resize(m_height);
	for (int y=0;y<m_height;++y) {
		const line_cache_result* lr = results[y];
		if (lr && lr->color_row) {
			out_created[y].assign(lr->color_row, lr->color_row + m_width);
		} else {
			out_created[y].assign(m_width, 0);
		}
	}
}

void RastaConverter::UpdateTargetsFromResults(const std::vector<const line_cache_result*>& results,
	std::vector< std::vector<unsigned char> >& out_targets)
{
	out_targets.resize(m_height);
	for (int y=0;y<m_height;++y) {
		const line_cache_result* lr = results[y];
		if (lr && lr->target_row) {
			out_targets[y].assign(lr->target_row, lr->target_row + m_width);
		} else {
			out_targets[y].assign(m_width, (unsigned char)E_COLBAK);
		}
	}
}

bool RastaConverter::SaveScreenDataFromTargets(const char *filename, const std::vector< std::vector<unsigned char> >& targets)
{
    std::ofstream out(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return false;

    for (int y = 0; y < m_height; ++y)
    {
        for (int x = 0; x < m_width; x += 4)
        {
            unsigned char pix = 0;
            int a = ConvertColorRegisterToRawData((e_target)targets[y][x]);
            int b = ConvertColorRegisterToRawData((e_target)targets[y][x + 1]);
            int c = ConvertColorRegisterToRawData((e_target)targets[y][x + 2]);
            int d = ConvertColorRegisterToRawData((e_target)targets[y][x + 3]);
            pix |= static_cast<unsigned char>(a << 6);
            pix |= static_cast<unsigned char>(b << 4);
            pix |= static_cast<unsigned char>(c << 2);
            pix |= static_cast<unsigned char>(d);
            out.put(static_cast<char>(pix));
        }
    }

    return static_cast<bool>(out);
}

void RastaConverter::SavePMGWithSprites(std::string name, const sprites_memory_t& sprites)
{
    size_t sprite,y,bit; unsigned char b;
    std::ofstream out(name, std::ios::out | std::ios::trunc);
    if (!out) return;
    out << "; ---------------------------------- \n";
    out << "; RastaConverter by Ilmenit v." << program_version << '\n';
    out << "; ---------------------------------- \n";
    out << "missiles\n";
    out << "\t.ds $100\n";
    for(sprite=0;sprite<4;++sprite)
    {
        out << "player" << sprite << '\n';
        out << "\t.he 00 00 00 00 00 00 00 00";
        for (y=0;y<240;++y)
        {
            b=0; for (bit=0;bit<8;++bit) { b|=(sprites[y][sprite][bit])<<(7-bit); }
            out << ' ' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            out << std::nouppercase << std::dec;
            if (y%16==7) out << "\n\t.he";
        }
        out << " 00 00 00 00 00 00 00 00\n";
    }
    if (!out)
        Error(std::string("Error writing PMG handler to ") + name);
}


