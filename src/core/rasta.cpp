#include "version.h"

const char* program_version = RASTA_CONVERTER_VERSION;

#ifdef _MSC_VER
#pragma warning (disable: 4312)
#pragma warning (disable: 4996)
#endif

#include <climits>
#include <math.h>
#include <cmath>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <memory>
#include "FreeImage.h"

#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>

#include "CommandLineParser.h"
#include "string_conv.h"
#include <assert.h>
#include "config.h"
#include <float.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <ctype.h>
#include <iomanip>
#include <iterator>
#include <initializer_list>
#include <optional>
#include <limits>
#include <random>

#include "rasta.h"
#include "Interrupt.h"
#include "ColorCorrection.h"
#include "prng_xoroshiro.h"
#include "LinearAllocator.h"
#include "LineCache.h"
#include "Program.h"
#include "Evaluator.h"
#include "StructuredSolver.h"
#include "TargetPicture.h"
#include "TargetBuilder.h"
#include "debug_log.h"
#include "FreeImageIO.h"
#include "Utf8Path.h"

#ifndef _MSC_EXTENSIONS
#define __timeb64 timeb
#define _ftime ftime
#endif

unsigned char FindAtariColorIndex( const rgb& col );


// Cycle where WSYNC starts - 105?
#define WSYNC_START 104
// Normal screen CPU cycle 24-104 = 80 cycles = 160 color cycles

// global variables
int solutions=1;

// Defined below; used by the dithering path before its definition.
double random_plus_minus(double val);

bool quiet=false;

void RastaConverter::Error(std::string e)
{
	DBG_PRINT("[RASTA] Fatal error: %s", e.c_str());
	if (quiet)
		std::cerr << "Error: " << e << '\n';
	else
		gui.Error(e);
	exit(1);
}

int random(int range)
{
	if (range==0)
		return 0;
	return genrand_int32()%range;
}


void RastaConverter::Message(std::string message)
{
	if (quiet)
		return;

	time_t t;
	t = time(NULL);	
	string current_time = ctime(&t);
	current_time = current_time.substr(0, current_time.length() - 1);
	DBG_PRINT("[RASTA] %s", message.c_str());
	m_last_message = message;
	gui.DisplayText(0, 450, current_time + string(": ") + message + string("                    "));
}

using namespace std;

static rgb PIXEL2RGB(RGBQUAD &q)
{
	rgb x;
	x.b = q.rgbBlue;
	x.g = q.rgbGreen;
	x.r = q.rgbRed;
	x.a = q.rgbReserved;
	return x;
}

static std::string HashPicture(const std::vector<screen_line>& picture,
	int width, int height)
{
	unsigned long long hash = 1469598103934665603ULL;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const rgb& color = picture[y][x];
			for (unsigned char value : {color.r, color.g, color.b}) {
				hash ^= value;
				hash *= 1099511628211ULL;
			}
		}
	std::ostringstream stream;
	stream << std::hex << std::setfill('0') << std::setw(16) << hash;
	return stream.str();
}

static std::string SetRecipeOption(const std::string& recipe,
	const std::string& name, const std::string& value,
	std::initializer_list<std::string> aliases = {})
{
	std::vector<std::string> tokens;
	bool skipOptionValue = false;
	for (size_t i = 0; i < recipe.size();) {
		while (i < recipe.size() && std::isspace(
			static_cast<unsigned char>(recipe[i]))) ++i;
		if (i == recipe.size()) break;
		const size_t begin = i;
		bool quoted = false;
		for (; i < recipe.size(); ++i) {
			if (recipe[i] == '"') quoted = !quoted;
			else if (!quoted && std::isspace(
				static_cast<unsigned char>(recipe[i]))) break;
		}
		const std::string token = recipe.substr(begin, i - begin);
		if (skipOptionValue) {
			skipOptionValue = false;
			continue;
		}
		auto isOption = [&token](const std::string& option) {
			const std::string longPrefix = "--" + option + "=";
			const std::string dashPrefix = "-" + option + "=";
			const std::string shortPrefix = "/" + option + "=";
			return token.compare(0, longPrefix.size(), longPrefix) == 0
				|| token.compare(0, dashPrefix.size(), dashPrefix) == 0
				|| token.compare(0, shortPrefix.size(), shortPrefix) == 0;
		};
		auto isBareOption = [&token](const std::string& option) {
			return token == "--" + option || token == "-" + option
				|| token == "/" + option;
		};
		bool replace = isOption(name);
		bool replaceBare = isBareOption(name);
		for (const std::string& alias : aliases) {
			replace = replace || isOption(alias);
			replaceBare = replaceBare || isBareOption(alias);
		}
		if (replaceBare)
			skipOptionValue = true;
		if (!replace && !replaceBare)
			tokens.push_back(token);
	}
	std::ostringstream result;
	bool first = true;
	for (const std::string& token : tokens) {
		if (!first) result << ' ';
		result << token;
		first = false;
	}
	if (!first) result << ' ';
	result << "/" << name << "=" << value;
	return result.str();
}

static bool ReplaceTextInFile(const std::filesystem::path& path,
	const std::string& from, const std::string& to)
{
	if (from.empty() || from == to)
		return true;
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return false;
	std::string content((std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	input.close();
	size_t offset = 0;
	while ((offset = content.find(from, offset)) != std::string::npos) {
		content.replace(offset, from.size(), to);
		offset += to.size();
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output)
		return false;
	output.write(content.data(), static_cast<std::streamsize>(content.size()));
	return static_cast<bool>(output);
}

static RGBQUAD RGB2PIXEL(rgb& val)
{
	RGBQUAD fpixel;
	fpixel.rgbRed = val.r;
	fpixel.rgbGreen = val.g;
	fpixel.rgbBlue = val.b;
	return fpixel;
}


set < unsigned char > color_indexes_on_dst_picture;

OnOffMap on_off;

// Clears the process-global state that would otherwise leak from one
// conversion into the next when several are run in a single process.
//
// The rest of the globals are safe by construction and are listed here so the
// next person does not have to re-derive it:
//   atari_palette, distance_function - rewritten wholesale by LoadAtariPalette
//     and SetDistanceFunction at the start of every run.
//   on_off - memset to all-true by LoadOnOffFile, and only handed to the
//     evaluator when cfg.on_off_file is set, so stale ranges cannot apply.
//   solutions - reassigned by Configuration::Process from the command line.
// color_indexes_on_dst_picture is the exception: it accumulates, and it decides
// both the low-colour initializer and the low-colour warning.
void ResetProcessGlobalsForNewRun()
{
	color_indexes_on_dst_picture.clear();
}


const char *mem_regs_names[E_TARGET_MAX+1]=
{
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
	"COLOR3",
	"HITCLR",
};

// create_cycles_table now defined in src/core/Cycles.cpp

const char *mutation_names[E_MUTATION_MAX]=
{
	"PushBack2Prev ",
	"Copy2NextLine ",
	"SwapWithPrevL ",
	"Add Instr     ",
	"Remove Instr  ",
	"Swap Instr    ",
	"Change Target ",
	"Change Value  ",
	"Chg Val to Col",
	"Dual Complement",
	"Flip A4 Attr  ",
};

void resize_rgb_picture(vector < screen_line > *picture, size_t width, size_t height)
{
	size_t y;
	picture->resize(height);
	for (y=0;y<height;++y)
	{
		(*picture)[y].Resize(width);
	}
}

void RastaConverter::LoadAtariPalette()
{
	Message("Loading palette");
	if (!::LoadAtariPalette(cfg.palette_file))
		Error("Error opening .act palette file");
}

// RAII helper for FreeImage bitmaps
struct FreeImageBitmapDeleter {
    void operator()(FIBITMAP* bmp) const {
        if (bmp) {
            FreeImage_Unload(bmp);
        }
    }
};

using FreeImageBitmapPtr = std::unique_ptr<FIBITMAP, FreeImageBitmapDeleter>;

// Function to rescale FIBITMAP to double its width
static FIBITMAP* RescaleFIBitmapDoubleWidth(FIBITMAP* originalFiBitmap) {
    if (!originalFiBitmap) {
        DBG_PRINT("[RASTA] RescaleFIBitmapDoubleWidth called with null bitmap");
        return nullptr;
    }

    const int originalWidth = FreeImage_GetWidth(originalFiBitmap);
    const int originalHeight = FreeImage_GetHeight(originalFiBitmap);

    if (originalWidth <= 0 || originalHeight <= 0) {
        DBG_PRINT("[RASTA] Invalid bitmap dimensions for rescale: %dx%d", originalWidth, originalHeight);
        return nullptr;
    }

    // Calculate the new width as double the original width
    const int newWidth = originalWidth * 2;

    // Use FreeImage_Rescale to create a new bitmap with the new dimensions
    FIBITMAP* rescaledFiBitmap = FreeImage_Rescale(originalFiBitmap, newWidth, originalHeight, FILTER_BOX);
    if (!rescaledFiBitmap) {
        DBG_PRINT("[RASTA] FreeImage_Rescale failed for %dx%d -> %dx%d", originalWidth, originalHeight, newWidth, originalHeight);
    }

    return rescaledFiBitmap;
}

bool RastaConverter::SavePicture(const std::string& filename, FIBITMAP* to_save)
{
    if (!to_save) {
        Error(string("SavePicture called with null bitmap: ") + filename);
        return false;
    }

    FreeImageBitmapPtr stretched(RescaleFIBitmapDoubleWidth(to_save));
    if (!stretched) {
        Error(string("Failed to rescale bitmap for saving: ") + filename);
        return false;
    }

    if (!FreeImage_FlipVertical(stretched.get())) {
        Error(string("Error flipping picture vertically: ") + filename);
        return false;
    }

    if (!FreeImageSaveUtf8(FIF_PNG, stretched.get(), filename))
    {
        Error(string("Error saving picture.") + filename);
        return false;
    }

    return true;
}
void RastaConverter::SaveStatistics(const char *fn)
{
    std::ofstream out(Utf8Path(fn), std::ios::out | std::ios::trunc);
    if (!out)
    {
        DBG_PRINT("[RASTA] Unable to write statistics to %s", fn);
        return;
    }

    out << "Iterations,Seconds,Score\n";
    out << std::fixed << std::setprecision(6);
    for (const statistics_point& pt : m_eval_gstate.m_statistics)
    {
        out << static_cast<unsigned long long>(pt.evaluations) << ','
            << pt.seconds << ','
            << NormalizeScore(pt.distance) << '\n';
    }

    if (!out)
    {
        DBG_PRINT("[RASTA] Error while writing statistics to %s", fn);
    }
}

void RastaConverter::SaveOptimizerState(const char* fn, const raster_picture* picture) {
    std::ofstream out(Utf8Path(fn), std::ios::out | std::ios::trunc);
    if (!out)
    {
        DBG_PRINT("[RASTA] Unable to write optimizer state to %s", fn);
        return;
    }

    const char* opt;
    if (m_eval_gstate.m_optimizer == EvalGlobalState::OPT_LAHC) {
        opt = "lahc";
    } else if (m_eval_gstate.m_optimizer == EvalGlobalState::OPT_DLAS) {
        opt = "dlas";
    } else if (m_eval_gstate.m_optimizer == EvalGlobalState::OPT_LEGACY) {
        opt = "legacy";
    } else {
        opt = "lahc"; // fallback
    }
    out << opt << '\n';

    out << static_cast<unsigned long long>(m_eval_gstate.m_evaluations) << '\n';
    out << static_cast<unsigned long long>(m_eval_gstate.m_last_best_evaluation) << '\n';

	out << static_cast<unsigned long>(m_eval_gstate.m_previous_results.size()) << '\n';
	{
		unsigned long history_size = (unsigned long)m_eval_gstate.m_previous_results.size();
		unsigned long original_index = (unsigned long)m_eval_gstate.m_previous_results_index;
		unsigned long history_index = (history_size > 0) ? (original_index % history_size) : 0UL;
		out << history_index << '\n';
	}
    out << std::setprecision(21) << static_cast<long double>(m_eval_gstate.m_cost_max) << '\n';
    out << m_eval_gstate.m_N << '\n';
    out << std::setprecision(21) << static_cast<long double>(m_eval_gstate.m_current_cost) << '\n';

    out << std::setprecision(21);
    for (double value : m_eval_gstate.m_previous_results)
    {
        out << static_cast<long double>(value) << '\n';
    }
	if (picture && picture->graphics_mode == GraphicsMode::Antic4)
	{
		if (picture->antic4_attributes.empty()
			|| picture->antic4_attributes.size() > 30)
			Error("Cannot save malformed ANTIC 4 attribute state");
		out << "ANTIC4_STATE 1\n";
		out << "graphics_mode antic4\n";
		out << "attribute_rows " << picture->antic4_attributes.size() << '\n';
		out << std::hex << std::setfill('0');
		for (uint64_t row : picture->antic4_attributes)
			out << std::setw(11) << row << '\n';
		out << std::dec;
	}

    if (!out)
    {
        DBG_PRINT("[RASTA] Error while writing optimizer state to %s", fn);
    }
}

void RastaConverter::LoadOptimizerState(string name)
{
    std::ifstream in(Utf8Path(name));
    if (!in)
        return;

    auto lowercase = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };

    auto read_line = [&](std::string& line) -> bool {
        if (!std::getline(in, line))
        {
            DBG_PRINT("[RASTA] Unexpected end of optimizer state file: %s", name.c_str());
            return false;
        }
        return true;
    };

    std::string line;
    if (!read_line(line)) return;
    std::string opt = lowercase(line);

    if (opt == "lahc") {
        m_eval_gstate.m_optimizer = EvalGlobalState::OPT_LAHC;
    } else if (opt == "dlas") {
        m_eval_gstate.m_optimizer = EvalGlobalState::OPT_DLAS;
    } else if (opt == "legacy") {
        m_eval_gstate.m_optimizer = EvalGlobalState::OPT_LEGACY;
    } else {
        m_eval_gstate.m_optimizer = EvalGlobalState::OPT_LAHC; // fallback
    }

    auto read_numeric = [&](auto& out) -> bool {
        if (!read_line(line)) return false;
        std::istringstream iss(line);
        if (!(iss >> out))
        {
            DBG_PRINT("[RASTA] Failed to parse optimizer state value: %s", line.c_str());
            return false;
        }
        return true;
    };

    unsigned long long evals = 0ULL;
    unsigned long long lastbest = 0ULL;
    if (!read_numeric(evals)) return;
    if (!read_numeric(lastbest)) return;
    m_eval_gstate.m_evaluations = evals;
    m_eval_gstate.m_last_best_evaluation = lastbest;

    unsigned long no_elements = 0;
    unsigned long index = 0;
    long double cost_max = 0;
    int N = 0;
    long double current_cost = 0;

    if (!read_numeric(no_elements)) return;
    if (!read_numeric(index)) return;
    if (!read_numeric(cost_max)) return;
    if (!read_numeric(N)) return;
    if (!read_numeric(current_cost)) return;

    m_eval_gstate.m_previous_results.clear();
    m_eval_gstate.m_previous_results.reserve(no_elements);
    for (size_t i = 0; i < static_cast<size_t>(no_elements); ++i) {
        long double v = 0;
        if (!read_numeric(v)) return;
        m_eval_gstate.m_previous_results.push_back(static_cast<double>(v));
    }

    if (!m_eval_gstate.m_previous_results.empty()) {
        index %= static_cast<unsigned long>(m_eval_gstate.m_previous_results.size());
    }
    m_eval_gstate.m_previous_results_index = index;
    m_eval_gstate.m_cost_max = static_cast<double>(cost_max);
    m_eval_gstate.m_N = N;
    m_eval_gstate.m_current_cost = static_cast<double>(current_cost);

	if (!std::getline(in, line))
		return; // Legacy mode-E optimizer state.
	if (line != "ANTIC4_STATE 1")
		Error("Unknown optimizer-state extension: " + line);
	if (!read_line(line) || lowercase(line) != "graphics_mode antic4")
		Error("Invalid ANTIC 4 optimizer-state graphics mode");
	if (!read_line(line))
		Error("Invalid ANTIC 4 optimizer-state row count");
	std::istringstream rowCountParser(line);
	std::string rowCountName;
	int attributeRows = 0;
	if (!(rowCountParser >> rowCountName >> attributeRows)
		|| rowCountName != "attribute_rows"
		|| attributeRows < 1 || attributeRows > 30)
		Error("Invalid ANTIC 4 optimizer-state row count");
	raster_picture& picture = m_eval_gstate.m_best_pic;
	picture.graphics_mode = GraphicsMode::Antic4;
	picture.antic4_attributes.assign(attributeRows, 0);
	for (int rowIndex = 0; rowIndex < attributeRows; ++rowIndex)
	{
		if (!read_line(line) || (line.size() != 10 && line.size() != 11))
			Error("Invalid ANTIC 4 optimizer-state attribute row");
		unsigned long long mask = 0;
		std::istringstream parser(line);
		if (!(parser >> std::hex >> mask)
			|| (mask >> antic4_visible_characters) != 0)
			Error("Invalid ANTIC 4 optimizer-state attribute mask");
		char extra = 0;
		if (parser >> extra)
			Error("Invalid ANTIC 4 optimizer-state attribute mask");
		picture.antic4_attributes[rowIndex] = mask;
	}
}

bool RastaConverter::LoadInputBitmap()
{
	Message("Loading and initializing file");
	FREE_IMAGE_FORMAT fif = FreeImageFormatUtf8(cfg.input_file);
	if (fif == FIF_UNKNOWN)
		Error(std::string("Unrecognized input image format: ") + cfg.input_file);
	input_bitmap = FreeImageLoadUtf8(cfg.input_file);
	if (!input_bitmap)
		Error(string("Error loading input file: ") + cfg.input_file);

	unsigned int input_width=FreeImage_GetWidth(input_bitmap);
	unsigned int input_height=FreeImage_GetHeight(input_bitmap);
	cfg.width = cfg.graphics_mode == GraphicsMode::Antic4
		? antic4_visible_width : 160;
	if (cfg.height==-1) // set height automatic to keep screen proportions
	{
		double iw= (double) input_width;
		double ih= (double) input_height;
		const double outputWidth = static_cast<double>(cfg.width * 2);
		if (iw / ih > outputWidth / 240.0)
		{
			ih = input_height / (input_width / outputWidth);
			cfg.height=(int) ih;
		}
		else
			cfg.height=240;
	}
	if (cfg.graphics_mode == GraphicsMode::Antic4)
		cfg.height = NormalizeAntic4Height(cfg.height);
	
    {
        FIBITMAP* rescaled = FreeImage_Rescale(input_bitmap, cfg.width, cfg.height, cfg.rescale_filter);
        if (!rescaled) {
            FreeImage_Unload(input_bitmap);
            Error(string("Error rescaling input file: ") + cfg.input_file);
        }
        FreeImage_Unload(input_bitmap);
        input_bitmap = rescaled;
    }

    {
        FIBITMAP* converted = FreeImage_ConvertTo24Bits(input_bitmap);
        if (!converted) {
            FreeImage_Unload(input_bitmap);
            Error(string("Error converting input file to 24-bit: ") + cfg.input_file);
        }
        FreeImage_Unload(input_bitmap);
        input_bitmap = converted;
    }

	FreeImage_AdjustBrightness(input_bitmap,cfg.brightness);
	FreeImage_AdjustContrast(input_bitmap,cfg.contrast);
	FreeImage_AdjustGamma(input_bitmap,cfg.gamma);
	if (cfg.saturation != 0 || cfg.vibrance != 0) {
		const unsigned width = FreeImage_GetWidth(input_bitmap);
		const unsigned height = FreeImage_GetHeight(input_bitmap);
		for (unsigned y = 0; y < height; ++y) {
			BYTE* row = FreeImage_GetScanLine(input_bitmap, y);
			for (unsigned x = 0; x < width; ++x) {
				const unsigned offset = x * 3;
				const rasta::RGB8 adjusted = rasta::AdjustSaturationAndVibrance(
					{row[offset + FI_RGBA_RED], row[offset + FI_RGBA_GREEN],
					 row[offset + FI_RGBA_BLUE]},
					cfg.saturation, cfg.vibrance);
				row[offset + FI_RGBA_RED] = adjusted.r;
				row[offset + FI_RGBA_GREEN] = adjusted.g;
				row[offset + FI_RGBA_BLUE] = adjusted.b;
			}
		}
	}

	FreeImage_FlipVertical(input_bitmap);

	m_height=(int) cfg.height;
	m_width=(int) cfg.width;

	return true;
}

void RastaConverter::InitLocalStructure()
{
	unsigned x,y;

	//////////////////////////////////////////////////////////////////////////
	// Set our structure size

	unsigned width = FreeImage_GetWidth(input_bitmap);
	unsigned height = FreeImage_GetHeight(input_bitmap);
	resize_rgb_picture(&m_picture, width, height);

	// Copy data to input_bitmap and to our structure
	RGBQUAD fpixel;
	rgb atari_color;
	for (y=0;y<height;++y)
	{
		for (x=0;x<width;++x)
		{
			FreeImage_GetPixelColor(input_bitmap, x, y, &fpixel);
			atari_color=PIXEL2RGB(fpixel);
			m_picture[y][x]=atari_color;
			fpixel.rgbRed=atari_color.r;
			fpixel.rgbGreen=atari_color.g;
			fpixel.rgbBlue=atari_color.b;
			FreeImage_SetPixelColor(input_bitmap, x, y, &fpixel);
		}
	}

	// Show our picture
	if (!cfg.preprocess_only)
	{
		ShowInputBitmap();
	}
}

void RastaConverter::LoadDetailsMap()
{
	Message("Loading details map");
	string error;
	bool loaded = false;
	if (cfg.details_layer)
	{
		loaded = details_mask.LoadEditableLayer(cfg.details_file, m_width, m_height,
			&error);
		if (loaded && cfg.details_mode == "refined")
			loaded = details_mask.RebuildRefined(m_picture_original.data(),
				cfg.details_strength, cfg.details_floor, cfg.details_feather,
				cfg.details_refine_mix);
		else if (loaded && cfg.details_mode == "normalized")
			loaded = details_mask.RebuildNormalized(cfg.details_strength,
				cfg.details_floor, cfg.details_feather);
	}
	else if (cfg.details_mode == "refined")
		loaded = details_mask.LoadRefined(cfg.details_file, m_width, m_height,
			m_picture_original.data(), cfg.details_strength, cfg.details_floor,
			cfg.details_feather, cfg.details_refine_mix, &error);
	else if (cfg.details_mode == "normalized")
		loaded = details_mask.LoadNormalized(cfg.details_file, m_width, m_height,
			cfg.details_strength, cfg.details_floor, cfg.details_feather, &error);
	else
		loaded = details_mask.LoadLegacy(cfg.details_file, m_width, m_height, &error);
	if (!loaded)
		Error(error);
	Message("Details source hash: " + details_mask.SourceHash()
		+ "; effective hash: " + details_mask.EffectiveHash());
	// Hand the effective map to the GUI so the run can show what it weights.
	if (!details_mask.Empty())
	{
		GuiDetailsMask published;
		published.values = details_mask.Values().data();
		published.editable_values = details_mask.EditableValues().data();
		published.width = static_cast<int>(details_mask.Width());
		published.height = static_cast<int>(details_mask.Height());
		gui.PublishDetailsMask(published);
	}
	if (cfg.continue_processing && cfg.details_score
		&& !m_saved_details_effective_hash.empty()
		&& m_saved_details_effective_hash != details_mask.EffectiveHash())
	{
		cfg.resume_objective_changed = true;
		m_needs_history_reconfigure = true;
		Message("Details map changed; rebuilding acceptance history.");
	}
	if (cfg.details_mode != "legacy")
	{
		const string preview = cfg.output_file + "-details-effective.png";
		if (!details_mask.SaveEffectivePreview(preview, &error)) Error(error);
	}
};

void RastaConverter::GeneratePictureErrorMap()
{
	if (!cfg.details_file.empty())
		LoadDetailsMap();

	const int w = (int)FreeImage_GetWidth(input_bitmap);
	const int h = (int)FreeImage_GetHeight(input_bitmap);
	if (details_mask.Empty())
	{
		// A run always owns a neutral mask. In legacy mode zero has multiplier
		// 1.0, so this is bit-identical to the historical no-mask path.
		if (cfg.details_mode != "legacy") {
			cfg.details_mode = "legacy";
			Message("No details file loaded; live mask painting starts in "
				"additive legacy mode.");
		}
		details_mask.InitializeNeutral(w, h);
		GuiDetailsMask published;
		published.values = details_mask.Values().data();
		published.editable_values = details_mask.EditableValues().data();
		published.width = w;
		published.height = h;
		gui.PublishDetailsMask(published);
	}
	m_target_hash = HashPicture(m_picture, m_width, m_height);
	if (cfg.continue_processing && !m_saved_target_hash.empty()
		&& m_saved_target_hash != m_target_hash) {
		cfg.resume_objective_changed = true;
		m_needs_history_reconfigure = true;
		Message("Destination target changed; rebuilding acceptance history.");
	}
	const std::vector<screen_line>& scoring_picture =
		(cfg.visual_objective == E_OBJECTIVE_LEGACY_TARGET)
			? m_picture : m_picture_original;

	for(int i=0; i<128; ++i)
	{
		m_picture_all_errors[i].resize(w * h);

		const rgb ref = atari_palette[i];

		distance_t *dst = &m_picture_all_errors[i][0];
		for (int y=0; y<h; ++y)
		{
			const screen_line& srcrow = scoring_picture[y];

			if (!details_mask.Empty() && cfg.details_score)
			{
				for (int x=0; x<w; ++x)
				{
					const distance_t base = distance_function(srcrow[x], ref);
					*dst++ = details_mask.IsNormalized()
						? ApplyEffectiveDetailsWeight(base, details_mask.WeightAt(x, y))
						: ApplyLegacyDetailsWeight(base, details_mask.At(x, y),
							cfg.details_strength);
				}
			}
			else
			{
				for (int x=0; x<w; ++x)
				{
					*dst++ = distance_function(srcrow[x], ref);
				}
			}
		}
	}
}

bool RastaConverter::SnapshotBeforeMaskEdit()
{
	if (m_mask_edited_since_save)
		return true;
	// Workers are quiesced by the caller. Save the live best first: copying the
	// previous autosave would lose all improvements made between that save and
	// this stroke, defeating the promise of a true pre-edit restore point.
	SaveBestSolution();
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path output = Utf8Path(cfg.output_file);
	const fs::path parent = output.has_parent_path() ? output.parent_path() : fs::path(".");
	auto snapshotName = [](unsigned number) {
		std::ostringstream name;
		name << "snap-" << std::setw(3) << std::setfill('0') << number;
		return name.str();
	};
	unsigned nextSnapshot = m_snapshot_count;
	do {
		++nextSnapshot;
	} while (fs::exists(parent / snapshotName(nextSnapshot), ec));
	const fs::path snapshot = parent / snapshotName(nextSnapshot);
	if (!fs::create_directories(snapshot, ec) && ec) {
		Message("Could not create pre-paint snapshot: " + ec.message());
		return false;
	}
	bool snapshotComplete = true;
	std::string snapshotError;
	const std::string prefix = Utf8String(output.filename());
	for (const fs::directory_entry& entry : fs::directory_iterator(parent, ec)) {
		if (ec) {
			snapshotComplete = false;
			snapshotError = ec.message();
			break;
		}
		if (!entry.is_regular_file())
			continue;
		const std::string name = Utf8String(entry.path().filename());
		if (name.compare(0, prefix.size(), prefix) != 0)
			continue;
		fs::copy_file(entry.path(), snapshot / entry.path().filename(),
			fs::copy_options::overwrite_existing, ec);
		if (ec) {
			snapshotComplete = false;
			snapshotError = ec.message();
			break;
		}
	}
	std::string error;
	if (snapshotComplete && !details_mask.SaveEditableLayer(
		Utf8String(snapshot / (prefix + "-details.png")), &error)) {
		snapshotComplete = false;
		snapshotError = error.empty()
			? "could not save the details layer" : error;
	}

	// A snapshot must not reach back into the mutable parent run. The saved
	// recipe contains the output prefix, so rewriting that prefix relocates
	// /output, /details and /target together when those artifacts exist.
	ec.clear();
	const fs::path snapshotOutput =
		fs::absolute(snapshot / prefix, ec).lexically_normal();
	const std::string snapshotPrefix = ec
		? Utf8String(snapshot / prefix) : Utf8String(snapshotOutput);
	for (const char* extension : {".opt", ".rp"}) {
		const fs::path recipeFile = snapshot / (prefix + extension);
		if (!snapshotComplete)
			break;
		if (!fs::exists(recipeFile, ec) || ec) {
			snapshotComplete = false;
			snapshotError = "missing saved recipe " + Utf8String(recipeFile);
			break;
		}
		if (!ReplaceTextInFile(recipeFile, cfg.output_file, snapshotPrefix)) {
			snapshotComplete = false;
			snapshotError =
				"could not make the saved recipe self-contained";
			break;
		}
		ec.clear();
	}
	if (!snapshotComplete) {
		std::error_code cleanupError;
		fs::remove_all(snapshot, cleanupError);
		Message("Could not create complete pre-paint snapshot: "
			+ snapshotError);
		return false;
	}
	m_snapshot_count = nextSnapshot;
	return true;
}

void RastaConverter::SaveEditedMaskArtifact()
{
	if (!m_mask_edited)
		return;
	const std::string artifact = cfg.output_file + "-details.png";
	std::string error;
	if (!details_mask.SaveEditableLayer(artifact, &error)) {
		Message(error.empty() ? "Could not save edited details mask." : error);
		return;
	}
	if (cfg.details_file != artifact) {
		cfg.details_file = artifact;
		cfg.details_score = true;
		cfg.details_layer = true;
		std::ostringstream strength;
		strength << std::setprecision(17) << cfg.details_strength;
		cfg.command_line = SetRecipeOption(cfg.command_line, "details",
			"\"" + artifact + "\"");
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_mode",
			cfg.details_mode);
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_val",
			strength.str());
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_floor",
			std::to_string(cfg.details_floor));
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_feather",
			std::to_string(cfg.details_feather));
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_refine_mix",
			std::to_string(cfg.details_refine_mix));
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_score", "on");
		cfg.command_line = SetRecipeOption(cfg.command_line, "details_layer", "on");
	}
}

void RastaConverter::PauseWorkers(std::unique_lock<std::mutex>& lock)
{
	// Workers acknowledge between complete evaluations, never while a cache row
	// is borrowed. Exited or not-yet-started threads cannot stall this: the
	// count they are compared against moves with them.
	m_eval_gstate.m_pause_requested.store(true, std::memory_order_release);
	m_eval_gstate.m_condvar_update.notify_all();
	m_eval_gstate.m_condvar_update.wait(lock, [this] {
		return m_eval_gstate.m_threads_paused >= m_eval_gstate.m_threads_active;
	});
}

void RastaConverter::ResumeWorkers(std::unique_lock<std::mutex>& lock)
{
	m_eval_gstate.m_pause_requested.store(false, std::memory_order_release);
	if (lock.owns_lock())
		lock.unlock();
	m_eval_gstate.m_condvar_update.notify_all();
}

void RastaConverter::BeginEditorSession(bool destination)
{
	if (m_editor_paused || cfg.dual_mode)
		return;
	if (destination && cfg.visual_objective != E_OBJECTIVE_LEGACY_TARGET)
		return;
	std::unique_lock<std::mutex> lock{m_eval_gstate.m_mutex};
	PauseWorkers(lock);
	m_editor_paused = true;
	m_editor_destination = destination;
	lock.unlock();
	// The editor paints on the layers it was handed. The destination one is
	// only published when the target picture is redrawn, which may have been
	// many minutes ago, so it is refreshed here rather than left to chance.
	if (destination_bitmap)
		ShowDestinationBitmap();
	Message(destination
		? "Editor open on the destination; optimizer paused."
		: "Editor open on the details mask; optimizer paused.");
	PublishLiveStats(/*preprocessing*/ false, /*finished*/ false);
}

void RastaConverter::DiscardEditorSession()
{
	if (!m_editor_paused)
		return;
	std::unique_lock<std::mutex> lock{m_eval_gstate.m_mutex};
	m_editor_paused = false;
	ResumeWorkers(lock);
	Message("Edits discarded; workers resumed.");
	PublishLiveStats(/*preprocessing*/ false, /*finished*/ false);
}

// The shared tail of every retarget: the cached line errors were accumulated
// from the table that just changed, and the acceptance history holds scores on
// the old scale. Both have to go, and the re-scored best becomes the new
// baseline. Same code path resume uses when it detects a changed objective.
void RastaConverter::RetargetLocked(bool /*full_rebuild*/)
{
	const std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> prior =
		std::atomic_load_explicit(&m_eval_gstate.m_best_snapshot,
			std::memory_order_acquire);
	if (prior)
		m_eval_gstate.m_best_pic = prior->picture;
	for (Evaluator& evaluator : m_evaluators)
		evaluator.ClearAllCaches();
	if (m_reporting_evaluator)
		m_reporting_evaluator->ClearAllCaches();
	m_eval_gstate.m_best_pic.uncache_insns();

	cfg.resume_objective_changed = true;
	m_needs_history_reconfigure = true;
	reconfigureAcceptanceHistory();

	const double baseline =
		m_eval_gstate.m_best_result.load(std::memory_order_relaxed);
	std::shared_ptr<EvalGlobalState::PublishedBestSnapshot> snapshot =
		std::make_shared<EvalGlobalState::PublishedBestSnapshot>();
	snapshot->picture = m_eval_gstate.m_best_pic;
	snapshot->picture.uncache_insns();
	snapshot->cost = baseline;
	snapshot->version =
		m_eval_gstate.m_best_state_version.load(std::memory_order_relaxed) + 1;
	std::atomic_store_explicit(&m_eval_gstate.m_best_snapshot,
		std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot>(snapshot),
		std::memory_order_release);
	m_eval_gstate.m_best_state_version.store(snapshot->version,
		std::memory_order_release);
	m_eval_gstate.m_objective_generation.fetch_add(1, std::memory_order_release);
}

bool RastaConverter::ApplyMaskEditLocked(const GuiEditorApply& request)
{
	if (cfg.dual_mode)
		return false;

	// Parameters and pixels commit together: one rebuild pays for both, and a
	// painted value means nothing without the strength that scales it.
	bool parametersChanged = false;
	const std::string priorMode = cfg.details_mode;
	const double priorStrength = cfg.details_strength;
	const double priorFloor = cfg.details_floor;
	const unsigned priorFeather = cfg.details_feather;
	const bool priorScore = cfg.details_score;
	if (request.has_mask_parameters) {
		const std::string mode = (request.details_mode == "normalized"
			|| request.details_mode == "refined") ? request.details_mode
			: std::string("legacy");
		parametersChanged = mode != cfg.details_mode
			|| request.details_strength != cfg.details_strength
			|| request.details_floor != cfg.details_floor
			|| request.details_feather != cfg.details_feather
			|| request.details_score != cfg.details_score;
		cfg.details_mode = mode;
		cfg.details_strength = request.details_strength;
		cfg.details_floor = request.details_floor;
		cfg.details_feather = request.details_feather;
		cfg.details_score = request.details_score;
	}

	bool pixelsChanged = false;
	for (const GuiMaskPixelChange& pixel : request.pixels) {
		if (pixel.x < details_mask.Width() && pixel.y < details_mask.Height()
			&& details_mask.EditableAt(pixel.x, pixel.y) != pixel.after) {
			pixelsChanged = true;
			break;
		}
	}
	if (!pixelsChanged && !parametersChanged)
		return false;

	if (!SnapshotBeforeMaskEdit()) {
		cfg.details_mode = priorMode;
		cfg.details_strength = priorStrength;
		cfg.details_floor = priorFloor;
		cfg.details_feather = priorFeather;
		cfg.details_score = priorScore;
		return false;
	}

	// Painting priority with scoring off would be a no-op, which is never what
	// the user means; it goes on with the first committed stroke.
	const bool scoringWasOff = !cfg.details_score;
	if (pixelsChanged)
		cfg.details_score = true;

	std::vector<unsigned char> priorValues;
	priorValues.reserve(request.pixels.size());
	for (const GuiMaskPixelChange& pixel : request.pixels) {
		const bool inBounds = pixel.x < details_mask.Width()
			&& pixel.y < details_mask.Height();
		priorValues.push_back(inBounds
			? details_mask.EditableAt(pixel.x, pixel.y) : 0);
		if (inBounds)
			details_mask.SetEditableValue(pixel.x, pixel.y, pixel.after);
	}

	auto rebuild = [this]() {
		if (cfg.details_mode == "refined")
			return details_mask.RebuildRefined(m_picture_original.data(),
				cfg.details_strength, cfg.details_floor, cfg.details_feather,
				cfg.details_refine_mix);
		if (cfg.details_mode == "normalized")
			return details_mask.RebuildNormalized(cfg.details_strength,
				cfg.details_floor, cfg.details_feather);
		return details_mask.RebuildLegacy();
	};
	if (!rebuild()) {
		for (size_t i = 0; i < request.pixels.size(); ++i)
			details_mask.SetEditableValue(
				request.pixels[i].x, request.pixels[i].y, priorValues[i]);
		cfg.details_mode = priorMode;
		cfg.details_strength = priorStrength;
		cfg.details_floor = priorFloor;
		cfg.details_feather = priorFeather;
		cfg.details_score = priorScore;
		rebuild();
		Message("Could not rebuild the edited details mask.");
		return false;
	}

	const std::vector<screen_line>& scoring_picture =
		(cfg.visual_objective == E_OBJECTIVE_LEGACY_TARGET)
			? m_picture : m_picture_original;
	auto patchPixel = [this, &scoring_picture](unsigned x, unsigned y) {
		const size_t offset = static_cast<size_t>(y) * m_width + x;
		for (int palette = 0; palette < 128; ++palette) {
			const distance_t base = distance_function(
				scoring_picture[y][x], atari_palette[palette]);
			m_picture_all_errors[palette][offset] =
				cfg.details_score
				? (details_mask.IsNormalized()
					? ApplyEffectiveDetailsWeight(base, details_mask.WeightAt(x, y))
					: ApplyLegacyDetailsWeight(base, details_mask.At(x, y),
						cfg.details_strength))
				: base;
		}
	};
	// A stroke in legacy mode touches only the pixels it covered. Anything that
	// rescales the whole map - a parameter change, a mode that renormalizes
	// against the image mean, or scoring coming on - touches all of them.
	const bool fullRebuild = parametersChanged || scoringWasOff
		|| details_mask.IsNormalized();
	if (fullRebuild) {
		for (unsigned y = 0; y < static_cast<unsigned>(m_height); ++y)
			for (unsigned x = 0; x < static_cast<unsigned>(m_width); ++x)
				patchPixel(x, y);
	} else {
		for (const GuiMaskPixelChange& pixel : request.pixels)
			if (pixel.x < static_cast<unsigned>(m_width)
				&& pixel.y < static_cast<unsigned>(m_height))
				patchPixel(pixel.x, pixel.y);
	}

	if (cfg.details_allocate)
		details_line_priorities = details_mask.LinePriorities(cfg.details_strength);

	RetargetLocked(fullRebuild);

	m_mask_edited = true;
	m_mask_edited_since_save = true;
	GuiDetailsMask published;
	published.values = details_mask.Values().data();
	published.editable_values = details_mask.EditableValues().data();
	published.width = m_width;
	published.height = m_height;
	gui.PublishDetailsMask(published);
	return true;
}

bool RastaConverter::ApplyDestinationEditLocked(const GuiEditorApply& request)
{
	if (request.pixels.empty())
		return false;
	if (!SnapshotBeforeMaskEdit())
		return false;
	for (const GuiMaskPixelChange& change : request.pixels) {
		if (change.x >= static_cast<unsigned>(m_width)
			|| change.y >= static_cast<unsigned>(m_height))
			continue;
		const unsigned char index = change.after % 128;
		m_picture[change.y][change.x] = atari_palette[index];
		RGBQUAD color = RGB2PIXEL(atari_palette[index]);
		FreeImage_SetPixelColor(destination_bitmap, change.x, change.y, &color);
	}
	// The target itself moved, so every plane of the error table is stale.
	for (int palette = 0; palette < 128; ++palette)
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				const distance_t base = distance_function(
					m_picture[y][x], atari_palette[palette]);
				m_picture_all_errors[palette][static_cast<size_t>(y) * m_width + x] =
					(!details_mask.Empty() && cfg.details_score)
					? (details_mask.IsNormalized()
						? ApplyEffectiveDetailsWeight(base,
							details_mask.WeightAt(x, y))
						: ApplyLegacyDetailsWeight(base, details_mask.At(x, y),
							cfg.details_strength))
					: base;
			}
	RetargetLocked(/*full_rebuild*/ true);
	RenderCreatedPicture(m_eval_gstate.m_best_pic);
	m_destination_edited = true;
	m_target_hash = HashPicture(m_picture, m_width, m_height);
	m_mask_edited_since_save = true;
	return true;
}

void RastaConverter::ApplyEditorSession(const GuiEditorApply& request)
{
	if (!m_editor_paused)
		return;
	const auto started = std::chrono::steady_clock::now();
	bool changed = false;
	double baseline = 0.0;
	{
		std::unique_lock<std::mutex> lock{m_eval_gstate.m_mutex};
		changed = request.destination
			? ApplyDestinationEditLocked(request)
			: ApplyMaskEditLocked(request);
		baseline = m_eval_gstate.m_best_result.load(std::memory_order_relaxed);
		m_editor_paused = false;
		m_last_retarget_ms = static_cast<int>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count());
		// Branching after the edit so the new run folder receives the edited
		// state, not the state it was branched from.
		if (changed && request.branch)
			BranchCurrentRun();
		ResumeWorkers(lock);
	}
	if (changed && request.destination) {
		ShowDestinationBitmap();
		ShowLastCreatedPicture();
	}
	if (changed) {
		Message((request.destination ? std::string("Destination applied in ")
				: std::string("Mask applied in "))
			+ std::to_string(m_last_retarget_ms) + " ms; history reset to "
			+ std::to_string(NormalizeScore(baseline)) + ".");
	} else {
		Message("Nothing to apply; workers resumed.");
	}
	PublishLiveStats(/*preprocessing*/ false, /*finished*/ false);
}

void RastaConverter::BranchCurrentRun()
{
	if (cfg.dual_mode) {
		Message("Branching from the live editor is currently single-frame only.");
		return;
	}
	SaveBestSolution();
	std::string nextOutput;
	std::string error;
	if (!gui.CreateBranchOutput(cfg.input_file, nextOutput, error)) {
		Message(error.empty() ? "Could not create branch run." : error);
		return;
	}
	const std::string previousOutput = cfg.output_file;
	cfg.output_file = nextOutput;
	cfg.command_line = SetRecipeOption(cfg.command_line, "output",
		"\"" + cfg.output_file + "\"", {"o"});
	m_snapshot_count = 0;
	m_mask_edited_since_save = true; // force the mask artifact into the branch
	if (input_bitmap)
		SavePicture(cfg.output_file + "-src.png", input_bitmap);
	if (destination_bitmap)
		SavePicture(cfg.output_file + "-dst.png", destination_bitmap);
	SaveBestSolution();
	Message("Branched from " + previousOutput + " to " + cfg.output_file + ".");
}

void RastaConverter::SaveEditedTargetArtifact()
{
	if (!m_destination_edited || !destination_bitmap) return;
	const std::string currentHash = HashPicture(m_picture, m_width, m_height);
	if (std::getenv("RASTA_TEST_DESTINATION_EDIT"))
		std::fprintf(stderr, "destination-test: saving hash %s\n",
			currentHash.c_str());
	m_target_hash = currentHash;
	const std::string artifact = cfg.output_file + "-target.png";
	FIBITMAP* bitmap = FreeImage_Allocate(m_width, m_height, 24);
	if (!bitmap) return;
	for (int y = 0; y < m_height; ++y)
		for (int x = 0; x < m_width; ++x) {
			RGBQUAD color = RGB2PIXEL(m_picture[y][x]);
			FreeImage_SetPixelColor(bitmap, x, m_height - 1 - y, &color);
		}
	const bool saved = FreeImageSaveUtf8(FIF_PNG, bitmap, artifact);
	FreeImage_Unload(bitmap);
	if (!saved) {
		Message("Could not save edited destination artifact.");
		return;
	}
	cfg.target_file = artifact;
	cfg.command_line = SetRecipeOption(cfg.command_line, "target",
		"\"" + artifact + "\"");
}

bool RastaConverter::OtherDithering()
{
	const int w = FreeImage_GetWidth(input_bitmap);
	const int h = FreeImage_GetHeight(input_bitmap);

	rasta::DitherParams params;
	params.type = cfg.dither;
	params.strength = cfg.dither_strength;
	params.randomness = cfg.dither_randomness;

	// The quantized result lands in a scratch buffer; the row callback mirrors
	// it into destination_bitmap and keeps the window responsive, exactly as
	// the previous inline loop did.
	std::vector<screen_line> quantized(h);
	for (int y = 0; y < h; ++y)
		quantized[y].Resize(w);

	bool cancelled_by_user = false;
	auto on_row = [&](int y) -> bool {
		for (int x = 0; x < w; ++x)
		{
			RGBQUAD color = RGB2PIXEL(quantized[y][x]);
			FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
		}
		ShowDestinationLine(y);
		// Keep UI responsive: pump events and present per line
		switch (gui.NextFrame())
		{
		case GUI_command::REDRAW:
			ShowInputBitmap();
			ShowDestinationBitmap();
			gui.Present();
			break;
		case GUI_command::SAVE:
		case GUI_command::CONTINUE:
		case GUI_command::SHOW_A:
		case GUI_command::SHOW_B:
		case GUI_command::SHOW_MIX:
			break;
		case GUI_command::STOP:
			cancelled_by_user = true;
			return false; // Exit dithering when user requests to quit
		}
		gui.Present();
		return true;
	};

	const bool cancelled = rasta::BuildQuantizedTarget(m_picture, w, h, params,
		quantized, color_indexes_on_dst_picture, on_row,
		[](double value) { return random_plus_minus(value); });

	return cancelled && cancelled_by_user;
}

void RastaConverter::ShowInputBitmap()
{
	unsigned int width = FreeImage_GetWidth(input_bitmap);
	unsigned int height = FreeImage_GetHeight(input_bitmap);
	gui.DisplayBitmap(0, 0, input_bitmap);
	gui.PublishImage(GuiImageSlot::Source, input_bitmap);
	if (cfg.dual_mode)
	{
		// In dual mode, input_bitmap shows original source (unchanged)
		gui.DisplayText(0, height + 10, "Source (original)");
		if (destination_bitmap)
		{
			ShowDestinationBitmap();
			// Destination shows high-color version (original or dithered)
			if (cfg.dual_dither != E_DUAL_DITHER_NONE)
			{
				gui.DisplayText(width * 4, height + 10, "Destination (dithered)");
			}
			else
			{
				gui.DisplayText(width * 4, height + 10, "Destination (high-color)");
			}
		}
	}
	else
	{
		gui.DisplayText(0, height + 10, "Source");
		if (destination_bitmap)
		{
			ShowDestinationBitmap();
			gui.DisplayText(width * 4, height + 10, "Destination");
		}
	}
}

void RastaConverter::ShowDestinationLine(int y)
{
	if (!cfg.preprocess_only)
	{
		unsigned int width = FreeImage_GetWidth(destination_bitmap);
		unsigned int where_x = FreeImage_GetWidth(input_bitmap) * 2;

		gui.DisplayBitmapLine(where_x, y, y, destination_bitmap);
	}
}

void RastaConverter::ShowDestinationBitmap()
{
	gui.DisplayBitmap(FreeImage_GetWidth(destination_bitmap)*2, 0, destination_bitmap);
	gui.PublishImage(GuiImageSlot::Target, destination_bitmap);
	m_destination_indices.resize(static_cast<size_t>(m_width) * m_height);
	for (int y = 0; y < m_height; ++y)
		for (int x = 0; x < m_width; ++x)
			m_destination_indices[static_cast<size_t>(y) * m_width + x] =
				FindAtariColorIndex(m_picture[y][x]);
	GuiDestinationLayer layer;
	layer.palette_indices = m_destination_indices.data();
	layer.width = m_width;
	layer.height = m_height;
	gui.PublishDestinationLayer(layer);
}



bool RastaConverter::PrepareDestinationPicture()
{
	Message("Preparing Destination Picture");
	PublishLiveStats(/*preprocessing*/ true, /*finished*/ false);

	int width = FreeImage_GetWidth(input_bitmap);
	int height = FreeImage_GetHeight(input_bitmap);
	int bpp = FreeImage_GetBPP(input_bitmap); // Bits per pixel

	// Allocate a new bitmap with the same dimensions and bpp
	destination_bitmap = FreeImage_Allocate(width, height, bpp);

	RGBQUAD black = { 0, 0, 0, 255 }; // Assuming 32-bit image with alpha channel

	// Fill the new bitmap with black color
	FreeImage_FillBackground(destination_bitmap, &black, 0);


	// Draw new picture on the screen. A persisted destination edit bypasses
	// preprocessing and is snapped back to the active hardware palette.
	if (!cfg.target_file.empty())
	{
		FIBITMAP* loaded = FreeImageLoadUtf8(cfg.target_file);
		// Rescaling an already exact-sized, palette-quantized PNG is not a
		// no-op in every FreeImage build: FILTER_BOX can round channels by one,
		// which then remaps pixels to a different Atari entry. Preserve exact
		// artifacts byte-for-byte; only filter genuinely different dimensions.
		FIBITMAP* resized = loaded
			? (FreeImage_GetWidth(loaded) == static_cast<unsigned>(width)
				&& FreeImage_GetHeight(loaded) == static_cast<unsigned>(height)
				? FreeImage_Clone(loaded)
				: FreeImage_Rescale(loaded, width, height, FILTER_BOX))
			: nullptr;
		if (loaded) FreeImage_Unload(loaded);
		FIBITMAP* rgb = resized ? FreeImage_ConvertTo24Bits(resized) : nullptr;
		if (resized) FreeImage_Unload(resized);
		if (!rgb)
			Error("Unable to load target override: " + cfg.target_file);
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				{
					RGBQUAD color{};
					FreeImage_GetPixelColor(rgb, x, height - 1 - y, &color);
					const auto loadedColor = PIXEL2RGB(color);
					unsigned char index = 128;
					for (unsigned char candidate = 0; candidate < 128; ++candidate)
						if (loadedColor.r == atari_palette[candidate].r
							&& loadedColor.g == atari_palette[candidate].g
							&& loadedColor.b == atari_palette[candidate].b) {
							index = candidate;
							break;
						}
					if (index == 128)
						index = FindAtariColorIndex(loadedColor);
					color_indexes_on_dst_picture.insert(index);
					color = RGB2PIXEL(atari_palette[index]);
				FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
			}
		FreeImage_Unload(rgb);
	}
	else if (cfg.dual_mode)
	{
		// In dual mode, destination is always high-color (original source or dithered source)
		// Quantization only happens during bootstrap phase, not in destination image
		for (int y=0;y<m_height;++y)
		{
			for (int x=0;x<m_width;++x)
			{
				// Copy high-color source to destination (no quantization)
				rgb out_pixel = m_picture_original[y][x];
				RGBQUAD color = RGB2PIXEL(out_pixel);
				FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
			}
			ShowDestinationLine(y);
		}
	}
	else if (cfg.dither!=E_DITHER_NONE)
	{
		// Single-frame mode with dithering
		bool cancelled = false;
		if (cfg.dither==E_DITHER_KNOLL)
			cancelled = KnollDithering();
		else
		{
			cancelled = OtherDithering();
		}
		if (cancelled)
			return true; // User cancelled, exit early
	}
	else
	{
		// Single-frame mode without dithering: quantize to Atari colors
		for (int y=0;y<m_height;++y)
		{
			for (int x=0;x<m_width;++x)
			{
				rgb out_pixel=m_picture[y][x];
				unsigned char color_index=FindAtariColorIndex(out_pixel);
				color_indexes_on_dst_picture.insert(color_index);	
				out_pixel = atari_palette[color_index];
				RGBQUAD color = RGB2PIXEL(out_pixel);
				FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
			}
			ShowDestinationLine(y);
		}
	}

	if (!cfg.preprocess_only)
	{
		ShowDestinationBitmap();
	}

	int w = FreeImage_GetWidth(input_bitmap);
	int h = FreeImage_GetHeight(input_bitmap);


	for (int y=0;y<h;++y)
	{
		for (int x=0;x<w;++x)
		{
			RGBQUAD color;
			FreeImage_GetPixelColor(destination_bitmap, x, y, &color);
			rgb out_pixel=PIXEL2RGB(color);
			m_picture[y][x]=out_pixel; // copy it always - it is used by the color distance cache m_picture_all_errors
		}
	}
	if (!cfg.preprocess_only)
		ShowDestinationBitmap();
	return false; // Completed successfully
}

void RastaConverter::LoadOnOffFile(const char *filename)
{
	memset(on_off.on_off,true,sizeof(on_off.on_off));

	// 1. Resize the on_off table and full it with true
	fstream f;
	f.open( filename, ios::in);
	if ( f.fail())
		Error("Error loading OnOff file");

	string line;
	unsigned int y=1;
	while( getline( f, line)) 
	{
		if (line.empty())
			continue;
		std::transform(line.begin(), line.end(), line.begin(), ::toupper);

		stringstream sl(line);
		string reg, value;
		e_target target=E_TARGET_MAX;
		unsigned int from, to;

		sl >> reg >> value >> from >> to;

		if(sl.rdstate() == ios::failbit) // failed to parse arguments?
		{
			string err="Error parsing OnOff file in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}
		if (!(value=="ON" || value=="OFF"))
		{
			string err="OnOff file: Second parameter should be ON or OFF in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}
		if (from>239 || to>239) // on_off table size
		{
			string err="OnOff file: Range value greater than 239 line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}

		if ((int)from > m_height-1 || (int)to > m_height-1)
		{
			string err="OnOff file: Range value greater than picture height in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			err+="\n";
			err+="Set range from 0 to ";
			err+=Value2String<unsigned int>(y-1);
			Error(err.c_str());
		}
		for (size_t i=0;i<E_TARGET_MAX;++i)
		{
			if (reg==string(mem_regs_names[i]))
			{
				target=(e_target) i;
				break;
			}
		}
		if (target==E_TARGET_MAX)
		{
			string err="OnOff file: Unknown register " + reg;
			err+=" in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}
		// fill 
		for (size_t l=from;l<=to;++l)
		{
			on_off.on_off[l][target] = (value=="ON");
		}
		++y;
	}
}

bool RastaConverter::ProcessInit()
{
	DBG_PRINT("[RASTA] ProcessInit start (dual=%d quiet=%d)", (int)cfg.dual_mode, (int)quiet);
#ifdef NO_GUI
	if (!gui.Init(cfg.command_line))
		return false;
#elif defined(RASTA_ENABLE_LIVE_UI)
	// Always the dashboard, whatever brought us here. /livegui decides whether
	// the setup screen appears first, not how a run is displayed: a conversion
	// started from the command line has the same three pictures to show and the
	// same questions to answer about them, and the legacy three-blit display
	// answered fewer of them. It survives only in builds without the live UI.
	if (!gui.Init(cfg.command_line, true))
		return false;
#else
	if (!gui.Init(cfg.command_line, false))
		return false;
#endif

	DBG_PRINT("[RASTA] LoadAtariPalette");
	LoadAtariPalette();
	// Reuse starts a fresh process and therefore has no edit metadata loaded
	// from an .opt header. The internal artifact options carry that identity.
	m_mask_edited = m_mask_edited || cfg.details_layer;
	m_destination_edited = m_destination_edited || !cfg.target_file.empty();
	DBG_PRINT("[RASTA] LoadInputBitmap");
	if (!LoadInputBitmap())
		Error("Error loading Input Bitmap!");

#ifndef NO_GUI
	if (input_bitmap) {
		if (!gui.SetIcon(input_bitmap)) {
			DBG_PRINT("[RASTA] Failed to set window icon from input bitmap");
		}
	}
#endif

	DBG_PRINT("[RASTA] InitLocalStructure");
	InitLocalStructure();
    m_picture_original = m_picture; // keep full-color source for dual optimization
	
	// set preprocess distance function
	DBG_PRINT("[RASTA] SetDistanceFunction(pre)");
	SetDistanceFunction(cfg.pre_dstf);

	// Prepare destination picture for BOTH modes to keep bootstrap behavior identical to single-frame
	if (PrepareDestinationPicture())
		return false; // User cancelled during dithering
	
	// Apply dual dithering for dual mode (if enabled) - overwrites destination with dithered version
	// Source (m_picture_original and input_bitmap) remains unchanged
	if (cfg.dual_mode && cfg.dual_dither != E_DUAL_DITHER_NONE)
	{
		ApplyDualInputDithering();
		// Refresh display to show original source and dithered destination
		if (!cfg.preprocess_only)
		{
			ShowInputBitmap();
			gui.Present();
		}
	}
	
	if (!cfg.preprocess_only)
		SavePicture(cfg.output_file+"-src.png",input_bitmap);
	// Preserve original behavior of saving -dst only in single-frame mode
	if (!cfg.dual_mode)
		SavePicture(cfg.output_file+"-dst.png",destination_bitmap);

	if (cfg.preprocess_only)
	{
		// Preprocessing has produced everything it was asked for. This used to
		// call exit(1), which both reported a successful operation as a failure
		// and killed the process outright - so with the live UI it would end the
		// session instead of returning to the setup screen. ProcessInit's
		// contract already covers this: false means "nothing to optimize".
		Message("Preprocessing finished");
		return false;
	}

	if (!cfg.on_off_file.empty())
		LoadOnOffFile(cfg.on_off_file.c_str());

	// set postprocess distance function
	DBG_PRINT("[RASTA] SetDistanceFunction(post)");
	SetDistanceFunction(cfg.dstf);

	DBG_PRINT("[RASTA] GeneratePictureErrorMap");
	GeneratePictureErrorMap();
	if (cfg.details_allocate)
	{
		if (details_mask.Empty()) Error("/details_allocate requires /details=FILE");
		details_line_priorities = details_mask.LinePriorities(cfg.details_strength);
	}
	else
		details_line_priorities.clear();

	m_eval_gstate.m_max_evals = cfg.max_evals;
	m_eval_gstate.m_save_period = cfg.save_period;

	for(int i=0; i<128; ++i)
		m_picture_all_errors_array[i] = m_picture_all_errors[i].data();

	DBG_PRINT("[RASTA] Create %d evaluator(s)", cfg.threads);
	m_evaluators.resize(cfg.threads);

	unsigned long long randseed = cfg.initial_seed;

	for(size_t i=0; i<m_evaluators.size(); ++i)
	{
		// seed=0 would lock up the LFSR
		if (!randseed)
			++randseed;

		m_evaluators[i].Init(m_width, m_height, m_picture_all_errors_array,
			m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off,
			&m_eval_gstate, solutions, randseed, cfg.cache_size, 0,
			m_picture_original.data(),
			cfg.details_allocate ? &details_line_priorities : nullptr,
			cfg.details_global_period);

		randseed += 187927 * i;
	}

	m_eval_gstate.m_thread_count = cfg.threads;
	// Propagate optimizer selection (default LAHC)
	if (cfg.optimizer == Configuration::E_OPT_LAHC) {
		m_eval_gstate.m_optimizer = EvalGlobalState::OPT_LAHC;
	} else if (cfg.optimizer == Configuration::E_OPT_DLAS) {
		m_eval_gstate.m_optimizer = EvalGlobalState::OPT_DLAS;
	} else if (cfg.optimizer == Configuration::E_OPT_LEGACY) {
		m_eval_gstate.m_optimizer = EvalGlobalState::OPT_LEGACY;
	} else {
		m_eval_gstate.m_optimizer = EvalGlobalState::OPT_LAHC; // fallback
	}
	// Configure aggressive search trigger
	m_eval_gstate.m_unstuck_after = cfg.unstuck_after;
	m_eval_gstate.m_unstuck_drift_norm = cfg.unstuck_drift_norm;

	// When initializing evaluators, pass thread ID:
	for (size_t i = 0; i < m_evaluators.size(); ++i)
	{
		// seed=0 would lock up the LFSR
		if (!randseed)
			++randseed;

		m_evaluators[i].Init(m_width, m_height, m_picture_all_errors_array,
			m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off,
			&m_eval_gstate, solutions, randseed, cfg.cache_size, i,
			m_picture_original.data(),
			cfg.details_allocate ? &details_line_priorities : nullptr,
			cfg.details_global_period);

		randseed += 187927 * i;
	}

	// Reporting is deliberately not one of the worker evaluators. Save can run
	// while worker 0 is evaluating and clearing its line/instruction allocators;
	// sharing that evaluator would leave EvaluateUnweightedSource() holding
	// borrowed row pointers into concurrently reclaimed storage.
	m_reporting_evaluator = std::make_unique<Evaluator>();
	m_reporting_evaluator->Init(m_width, m_height, m_picture_all_errors_array,
		m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off,
		&m_reporting_eval_gstate, 1, 1, cfg.cache_size,
		static_cast<int>(m_evaluators.size()), m_picture_original.data(),
		nullptr, cfg.details_global_period);

	if (cfg.continue_processing && m_needs_history_reconfigure && !cfg.dual_mode) {
		reconfigureAcceptanceHistory();
	}

	return true;
}


unsigned char ConvertColorRegisterToRawData(e_target t)
{
	if (t>E_COLBAK)
		t=E_COLBAK;
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
		;
	}
	assert(0); // this should never happen
	return -1;
}

bool RastaConverter::SaveScreenData(const char *filename)
{
    int x,y,a=0,b=0,c=0,d=0;
    std::ofstream out(Utf8Path(filename), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
        Error("Error saving MIC screen data");

    Message("Saving screen data");
    for(y=0;y<m_height;++y)
    {
        // encode 4 pixel colors in byte

        for (x=0;x<m_width;x+=4)
        {
            unsigned char pix=0;
            a=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x]);
            b=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x+1]);
            c=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x+2]);
            d=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x+3]);
            pix |= a<<6;
            pix |= b<<4;
            pix |= c<<2;
            pix |= d;
            out.put(static_cast<char>(pix));
            if (!out)
            {
                Error("Error writing MIC screen data");
            }
        }
    }
    out.flush();
    if (!out)
    {
        Error("Error finalizing MIC screen data");
    }
    return true;
}

bool RastaConverter::SaveAntic4Data(const std::string& screenFilename,
	const std::string& fontFilename, const raster_picture& picture)
{
	if (m_width != antic4_visible_width || m_height < 8 || m_height > 240
		|| m_height % 8 != 0
		|| m_eval_gstate.m_created_picture_targets.size()
			!= static_cast<size_t>(m_height))
		Error("ANTIC 4 export requires a complete 168-pixel-wide target map "
			"with a whole number of 8-scanline character rows");

	const int characterRows = m_height / 8;
	const int charsetCount = (characterRows + 2) / 3;
	std::vector<unsigned char> screen(
		characterRows * antic4_dma_characters);
	std::vector<unsigned char> fonts(charsetCount * 1024);
	for (int characterRow = 0; characterRow < characterRows; ++characterRow)
	{
		for (int cell = 0; cell < antic4_visible_characters; ++cell)
		{
			const bool alternate = picture.antic4_attribute(characterRow, cell);
			const int glyph =
				(characterRow % 3) * antic4_visible_characters + cell;
			screen[characterRow * antic4_dma_characters
				+ antic4_screen_left_hidden_characters + cell] =
				Antic4ScreenCode(characterRow, cell, alternate);
			for (int glyphRow = 0; glyphRow < 8; ++glyphRow)
			{
				unsigned char value = 0;
				const int y = characterRow * 8 + glyphRow;
				for (int pixel = 0; pixel < 4; ++pixel)
				{
					const int x = cell * 4 + pixel;
					e_target target = static_cast<e_target>(
						m_eval_gstate.m_created_picture_targets[y][x]);
					unsigned char encoded = 0;
					if (!EncodeAntic4PlayfieldTarget(target, alternate, encoded))
						Error("ANTIC 4 target map contains an illegal cell target");
					value |= static_cast<unsigned char>(encoded << (6 - pixel * 2));
				}
				const int charset = characterRow / 3;
				fonts[charset * 1024 + glyph * 8 + glyphRow] = value;
			}
		}
	}

	std::ofstream screenOut(screenFilename,
		std::ios::out | std::ios::binary | std::ios::trunc);
	std::ofstream fontOut(fontFilename,
		std::ios::out | std::ios::binary | std::ios::trunc);
	if (!screenOut || !fontOut)
		Error("Unable to create ANTIC 4 screen/font output");
	screenOut.write(reinterpret_cast<const char*>(screen.data()), screen.size());
	fontOut.write(reinterpret_cast<const char*>(fonts.data()), fonts.size());
	if (!screenOut || !fontOut)
		Error("Error writing ANTIC 4 screen/font output");
	return true;
}


void RastaConverter::SetConfig(Configuration &a_c)
{
	cfg=a_c;
}

double ColorCompare(int r1,int g1,int b1, int r2,int g2,int b2)
{
	double luma1 = (r1*299 + g1*587 + b1*114) / (255.0*1000);
	double luma2 = (r2*299 + g2*587 + b2*114) / (255.0*1000);
	double lumadiff = luma1-luma2;
	double diffR = (r1-r2)/255.0, diffG = (g1-g2)/255.0, diffB = (b1-b2)/255.0;
	return (diffR*diffR*0.299 + diffG*diffG*0.587 + diffB*diffB*0.114)*0.75
		+ lumadiff*lumadiff;
}

struct MixingPlan
{
    unsigned colors[64];
};

double random_plus_minus(double val)
{
	double result;
	int val2=100.0*val;
	result = random(val2);
	if (random(2))
		result*=-1;
	return result/100.0;
}


MixingPlan RastaConverter::DeviseBestMixingPlan(rgb color)
{
	rasta::DitherParams params;
	params.type = cfg.dither;
	params.strength = cfg.dither_strength;
	params.randomness = cfg.dither_randomness;
	const rasta::MixingPlan shared = rasta::DeviseBestMixingPlan(color, params,
		[](double value) { return random_plus_minus(value); });
	MixingPlan result = { {0} };
	std::copy(shared.colors, shared.colors + 64, result.colors);
	return result;
}

void RastaConverter::ParallelFor(int from, int to, void *(*start_routine)(void*))
{
	void *status;
	vector<std::thread> threads;
	vector<parallel_for_arg_t> threads_arg;
	/* Initialize and set thread detached attribute */

	threads.reserve(cfg.threads);
	threads_arg.resize(cfg.threads);

	int step=abs(to-from)/cfg.threads;
	for (int t=0;t<cfg.threads;++t)
	{
		threads_arg[t].this_ptr=this;
		threads_arg[t].from=from;
		if (t==cfg.threads-1) // last one
			threads_arg[t].to=to;
		else
			threads_arg[t].to=from+step;
		threads.emplace_back( std::bind( start_routine, ( void* )&threads_arg[t] ) );
		from+=step;
	}
	for (int t=0;t<cfg.threads;++t)
	{
		threads[t].join();
	}
	return;
}

// This is still quite ugly, KnollDitheringParallel should be passed in *arg so this helper could be generic
void *RastaConverter::KnollDitheringParallelHelper(void *arg)
{
	parallel_for_arg_t *param=(parallel_for_arg_t *)arg;
	((RastaConverter *)param->this_ptr)->KnollDitheringParallel(param->from,param->to);
	return NULL;
}

void RastaConverter::KnollDitheringParallel(int from, int to)
{
	std::vector<unsigned char> local_line;
	local_line.resize((size_t)m_width);
	std::set<unsigned char> local_indices;
	for(int y=from; y<to; ++y)
	{
		// Check if we should stop early
		if (m_knoll_should_stop.load(std::memory_order_acquire))
			break;
		
		local_indices.clear();
		for(unsigned x=0; x<(unsigned)m_width; ++x)
		{
			rgb r_color = m_picture[y][x];
			unsigned map_value = rasta::KnollThresholdMap()[(x & 7) + ((y & 7) << 3)];
			MixingPlan plan = DeviseBestMixingPlan(r_color);
			unsigned char color_index=plan.colors[ map_value ];
			local_line[x] = color_index;
			local_indices.insert(color_index);
		}
		for(unsigned x=0; x<(unsigned)m_width; ++x)
		{
			rgb out_pixel = atari_palette[ local_line[x] ];
			m_picture[y][x] = out_pixel;
		}
		{
			std::lock_guard<std::mutex> lock(m_color_set_mutex);
			for (auto v : local_indices) color_indexes_on_dst_picture.insert(v);
		}
		// Mark this line ready so the main thread can draw it
		if (m_knoll_line_ready)
			m_knoll_line_ready[(size_t)y].store(1, std::memory_order_release);
	}
}

bool RastaConverter::KnollDithering()
{
	Message("Knoll Dithering             ");
	// Initialize progress flags for multi-threaded readiness
	m_knoll_should_stop.store(false, std::memory_order_relaxed);
	m_knoll_line_ready.reset(new std::atomic<unsigned char>[(size_t)m_height]);
	m_knoll_line_drawn.assign((size_t)m_height, 0);
	for (int i=0;i<m_height;++i) m_knoll_line_ready[(size_t)i].store(0, std::memory_order_relaxed);
	// Show initial empty destination area so window isn't blank
	ShowDestinationBitmap();
	gui.Present();
	// Launch workers (non-blocking) with robust partitioning
	std::vector<std::thread> threads;
	std::vector<parallel_for_arg_t> threads_arg;
	int total_threads = std::max(1, cfg.threads);
	threads.reserve(total_threads);
	threads_arg.resize(total_threads);
	int from=0;
	int to=m_height;
	int step = (to - from + total_threads - 1) / total_threads; // ceil division
	for (int t=0;t<total_threads;++t)
	{
		threads_arg[t].this_ptr=this;
		threads_arg[t].from=from;
		int tto = from + step;
		if (t==total_threads-1 || tto>to) tto = to;
		threads_arg[t].to=tto;
		threads.emplace_back( std::bind( KnollDitheringParallelHelper, ( void* )&threads_arg[t] ) );
		from = tto;
	}
	// Progressive commit loop: draw lines as they become ready
	int next_to_draw = 0;
	int presented_until = -1;
	bool should_stop = false;
	while (next_to_draw < m_height && !should_stop)
	{
		// Draw any contiguous ready lines starting from next_to_draw
		while (next_to_draw < m_height && m_knoll_line_ready[(size_t)next_to_draw].load(std::memory_order_acquire))
		{
			if (!m_knoll_line_drawn[(size_t)next_to_draw])
			{
				for (int x=0; x<m_width; ++x)
				{
					rgb out_pixel = m_picture[next_to_draw][x];
					RGBQUAD color=RGB2PIXEL(out_pixel);
					FreeImage_SetPixelColor(destination_bitmap, x, next_to_draw, &color);
				}
				ShowDestinationLine(next_to_draw);
				m_knoll_line_drawn[(size_t)next_to_draw] = 1;
			}
			++next_to_draw;
		}
		// Also draw any out-of-order ready lines to avoid waiting on the first line
		int drawn_this_iter = 0;
		for (int y=0; y<m_height && drawn_this_iter<8; ++y)
		{
			if (!m_knoll_line_drawn[(size_t)y] && m_knoll_line_ready[(size_t)y].load(std::memory_order_acquire))
			{
				for (int x=0; x<m_width; ++x)
				{
					rgb out_pixel = m_picture[y][x];
					RGBQUAD color=RGB2PIXEL(out_pixel);
					FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
				}
				ShowDestinationLine(y);
				m_knoll_line_drawn[(size_t)y] = 1;
				++drawn_this_iter;
			}
		}
		// Pump events and present periodically to keep UI responsive
		switch (gui.NextFrame())
		{
		case GUI_command::REDRAW:
			ShowInputBitmap();
			ShowDestinationBitmap();
			gui.Present();
			break;
		case GUI_command::SAVE:
		case GUI_command::CONTINUE:
		case GUI_command::SHOW_A:
		case GUI_command::SHOW_B:
		case GUI_command::SHOW_MIX:
			break;
		case GUI_command::STOP:
			should_stop = true; // Exit dithering when user requests to quit
			m_knoll_should_stop.store(true, std::memory_order_release); // Signal worker threads to stop
			break;
		}
		if (presented_until != next_to_draw)
		{
			gui.Present();
			presented_until = next_to_draw;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	// Join workers before leaving
	for (int t=0;t<total_threads;++t)
	{
		threads[t].join();
	}
	return should_stop; // Return true if cancelled, false if completed
}

// Generate Bayer matrix recursively (supports 2x2, 4x4, 8x8)
// n must be a power of 2
static std::vector<std::vector<double>> GenerateBayerMatrix(int n)
{
	// Validate n is power of 2
	if (n <= 0 || (n & (n - 1)) != 0)
	{
		// Fallback to 4x4 if invalid
		n = 4;
	}
	
	std::vector<std::vector<double>> m(n, std::vector<double>(n, 0.0));
	
	// Start with 1x1 matrix [0]
	std::vector<std::vector<int>> temp(1, std::vector<int>(1, 0));
	
	// Recursively build up to size n
	for (int size = 1; size < n; size *= 2)
	{
		int newSize = size * 2;
		std::vector<std::vector<int>> newM(newSize, std::vector<int>(newSize, 0));
		
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				int v = temp[y][x] * 4;
				newM[y][x] = v;
				newM[y][x + size] = v + 2;
				newM[y + size][x] = v + 3;
				newM[y + size][x + size] = v + 1;
			}
		}
		temp = newM;
	}
	
	// Normalize to [0.0, 1.0] range
	double denom = n * n;
	for (int y = 0; y < n; ++y)
	{
		for (int x = 0; x < n; ++x)
		{
			m[y][x] = (temp[y][x] + 0.5) / denom;
		}
	}
	
	return m;
}

void RastaConverter::ApplyDualInputDithering()
{
	if (!cfg.dual_mode || cfg.dual_dither == E_DUAL_DITHER_NONE)
		return;
	
	if (m_picture_original.empty() || m_height <= 0 || m_width <= 0)
		return;
	
	const double strength = cfg.dual_dither_val;
	const double randomness = cfg.dual_dither_rand;
	
	// Per-channel offsets to decorrelate patterns (R:[0,0], G:[1,2], B:[2,1])
	const int offR[2] = {0, 0};
	const int offG[2] = {1, 2};
	const int offB[2] = {2, 1};
	
	// Precompute Bayer matrix for KNOLL dithering (4x4)
	std::vector<std::vector<double>> bayer_matrix;
	if (cfg.dual_dither == E_DUAL_DITHER_KNOLL)
	{
		bayer_matrix = GenerateBayerMatrix(4);
	}
	
	// Deterministic RNG for random component (seeded by pixel position)
	// Use hash function to scramble position and avoid visible patterns
	auto get_random = [&](int x, int y, int channel_offset) -> double {
		// Hash the position to break correlation between adjacent pixels
		unsigned long hash = (unsigned long)(y * m_width + x) + cfg.initial_seed + channel_offset;
		// Mix bits using a simple hash function (similar to MurmurHash)
		hash ^= hash >> 16;
		hash *= 0x85ebca6bUL;
		hash ^= hash >> 13;
		hash *= 0xc2b2ae35UL;
		hash ^= hash >> 16;
		// Ensure non-zero seed for LCG
		if (hash == 0) hash = 1;
		// Use LCG with hashed seed for better distribution
		hash = (hash * 1103515245UL + 12345UL) & 0x7fffffffUL;
		// Additional LCG iteration for better quality
		hash = (hash * 1103515245UL + 12345UL) & 0x7fffffffUL;
		return (double)hash / 2147483648.0; // normalize to [0.0, 1.0)
	};
	
	// Helper to clamp RGB value
	auto clamp = [](double v) -> unsigned char {
		if (v < 0.0) return 0;
		if (v > 255.0) return 255;
		return (unsigned char)(v + 0.5);
	};
	
	// Process each pixel: read from original source, apply dithering, write to destination only
	for (int y = 0; y < m_height; ++y)
	{
		for (int x = 0; x < m_width; ++x)
		{
			// Read from original source (do not modify)
			const rgb& src_pixel = m_picture_original[y][x];
			double r = (double)src_pixel.r;
			double g = (double)src_pixel.g;
			double b = (double)src_pixel.b;
			
			double biasR = 0.0, biasG = 0.0, biasB = 0.0;
			
			// Compute pattern-based bias for each channel
			if (cfg.dual_dither == E_DUAL_DITHER_KNOLL)
			{
				// Bayer matrix dithering
				int bxR = (x + offR[0]) % 4;
				int byR = (y + offR[1]) % 4;
				int bxG = (x + offG[0]) % 4;
				int byG = (y + offG[1]) % 4;
				int bxB = (x + offB[0]) % 4;
				int byB = (y + offB[1]) % 4;
				
				double tR = bayer_matrix[byR][bxR];
				double tG = bayer_matrix[byG][bxG];
				double tB = bayer_matrix[byB][bxB];
				
				biasR = ((tR * 2.0) - 1.0) * strength * 127.5;
				biasG = ((tG * 2.0) - 1.0) * strength * 127.5;
				biasB = ((tB * 2.0) - 1.0) * strength * 127.5;
			}
			else if (cfg.dual_dither == E_DUAL_DITHER_RANDOM)
			{
				// Pure random noise (will be blended with randomness control)
				biasR = (get_random(x, y, 0) * 2.0 - 1.0) * strength * 127.5;
				biasG = (get_random(x, y, 1) * 2.0 - 1.0) * strength * 127.5;
				biasB = (get_random(x, y, 2) * 2.0 - 1.0) * strength * 127.5;
			}
			else if (cfg.dual_dither == E_DUAL_DITHER_CHESS)
			{
				// Chessboard pattern
				int patternR = ((x + offR[0] + y + offR[1]) % 2 == 0) ? 1 : -1;
				int patternG = ((x + offG[0] + y + offG[1]) % 2 == 0) ? 1 : -1;
				int patternB = ((x + offB[0] + y + offB[1]) % 2 == 0) ? 1 : -1;
				
				biasR = patternR * strength * 127.5;
				biasG = patternG * strength * 127.5;
				biasB = patternB * strength * 127.5;
			}
			else if (cfg.dual_dither == E_DUAL_DITHER_LINE)
			{
				// Line pattern (alternating lines)
				int patternR = ((y + offR[1]) % 2 == 0) ? 1 : -1;
				int patternG = ((y + offG[1]) % 2 == 0) ? 1 : -1;
				int patternB = ((y + offB[1]) % 2 == 0) ? 1 : -1;
				
				biasR = patternR * strength * 127.5;
				biasG = patternG * strength * 127.5;
				biasB = patternB * strength * 127.5;
			}
			else if (cfg.dual_dither == E_DUAL_DITHER_LINE2)
			{
				// Line2 pattern (similar to Line)
				int patternR = ((y + offR[1]) % 2 == 0) ? 1 : -1;
				int patternG = ((y + offG[1]) % 2 == 0) ? 1 : -1;
				int patternB = ((y + offB[1]) % 2 == 0) ? 1 : -1;
				
				biasR = patternR * strength * 127.5;
				biasG = patternG * strength * 127.5;
				biasB = patternB * strength * 127.5;
			}
			
			// Apply randomness blending if enabled
			if (randomness > 0.0)
			{
				double randomR = (get_random(x, y, 0) * 2.0 - 1.0) * strength * 127.5;
				double randomG = (get_random(x, y, 1) * 2.0 - 1.0) * strength * 127.5;
				double randomB = (get_random(x, y, 2) * 2.0 - 1.0) * strength * 127.5;
				
				biasR = (1.0 - randomness) * biasR + randomness * randomR;
				biasG = (1.0 - randomness) * biasG + randomness * randomG;
				biasB = (1.0 - randomness) * biasB + randomness * randomB;
			}
			
			// Apply bias and clamp to create dithered pixel
			rgb dithered_pixel;
			dithered_pixel.r = clamp(r + biasR);
			dithered_pixel.g = clamp(g + biasG);
			dithered_pixel.b = clamp(b + biasB);
			
			// Write to destination_bitmap and m_picture (destination only, source unchanged)
			RGBQUAD color = RGB2PIXEL(dithered_pixel);
			if (destination_bitmap)
			{
				FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
			}
			m_picture[y][x] = dithered_pixel;
		}
	}
}

void RastaConverter::CreateLowColorRasterPicture(raster_picture *r)
{
	CreateEmptyRasterPicture(r);
	set < unsigned char >::iterator m,_m;
	int i=0;
	for (m=color_indexes_on_dst_picture.begin(),_m=color_indexes_on_dst_picture.end();m!=_m;++m,++i)
	{
		r->mem_regs_init[E_COLOR0+i]=(*m)*2;
	}
}


void RastaConverter::CreateEmptyRasterPicture(raster_picture *r)
{
	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));
	SRasterInstruction i;
	i.loose.instruction=E_RASTER_NOP;
	i.loose.target=E_COLBAK;
	i.loose.value=0;
	FreeImage_GetWidth(input_bitmap);
	// in line 0 we set init registers
	for (size_t y=0;y<r->raster_lines.size();++y)
	{
		r->raster_lines[y].instructions.push_back(i);
		r->raster_lines[y].cycles+=2;
		r->raster_lines[y].rehash();
	}
}

void RastaConverter::CreateSmartRasterPicture(raster_picture *r)
{
	SRasterInstruction i;
	int dest_colors;
	int dest_regs;
	int x,y;
	rgb color;

	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));

	dest_regs=8;

	if (cfg.init_type==E_INIT_LESS)
		dest_colors=dest_regs;
	else
		dest_colors=dest_regs+4;


	int width = FreeImage_GetWidth(input_bitmap);
	// in line 0 we set init registers

//	FreeImage_FlipVertical(input_bitmap);
	FIBITMAP *f_copy = FreeImage_Copy(input_bitmap,0,1,width,0);
//	FreeImage_FlipVertical(input_bitmap);

	for (y=0;y<(int)r->raster_lines.size();++y)
	{
		RGBQUAD fpixel;
		rgb atari_color;
		for (x=0;x<m_width;++x)
		{
			atari_color=m_picture[y][x];
			fpixel.rgbRed=atari_color.r;
			fpixel.rgbGreen=atari_color.g;
			fpixel.rgbBlue=atari_color.b;
			FreeImage_SetPixelColor(f_copy, x, 0, &fpixel);
		}
		FIBITMAP *f_copy24bits2;
		// create new picture from line y 
		FIBITMAP *f_copy24bits = FreeImage_ConvertTo24Bits(f_copy);	
		// quantize it 
		FIBITMAP *f_quant = FreeImage_ColorQuantizeEx(f_copy24bits,FIQ_WUQUANT,dest_colors);
		if (dest_colors>4)
			f_copy24bits2 = FreeImage_ConvertTo24Bits(f_quant);
		else
			f_copy24bits2 = FreeImage_ConvertTo24Bits(f_copy);


		map <int,int > color_map;
		map <int,int >::iterator j,_j;
		multimap <int,int, greater <int> > sorted_colors;
		multimap <int,int, greater <int> >::iterator m;
		map <int,int> color_position;
		for (x=0;x<width;++x)
		{
			RGBQUAD fpixel;
			FreeImage_GetPixelColor(f_copy24bits2, x,0, &fpixel);
			int c = fpixel.rgbRed + fpixel.rgbGreen * 0x100 + fpixel.rgbBlue * 0x10000;
			color_map[c]++;
			if (color_position.find(c)==color_position.end())
			{
				color_position[c]=x;
			}
		}

		// copy colors to sorted
		for (j=color_map.begin(),_j=color_map.end();j!=_j;++j)
		{
			sorted_colors.insert(pair<int,int>(j->second,j->first));
		}


		// convert colors to series of LDA/STA in order of appearance. Ignore for now(?) regs in prev line

		m=sorted_colors.begin();
		for (int k=0;k<dest_regs && k<(int)sorted_colors.size();++k,++m)
		{
			int c=m->second;
			color.r=c & 0xFF;
			color.g=(c>>8) & 0xFF;
			color.b=(c>>16) & 0xFF;

			// lda
			i.loose.instruction=(e_raster_instruction) (E_RASTER_LDA+k%3); // k%3 to cycle through A,X,Y regs
			if (k>E_COLBAK && y%2==1 && dest_colors>4)
				i.loose.value=(e_target) color_position[k]
					+ SpriteScreenColorCycleStart(r->graphics_mode); // sprite position
			else
				i.loose.value=FindAtariColorIndex(color)*2;
			i.loose.target=E_COLOR0;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=2;

			// sta 
			i.loose.instruction=(e_raster_instruction) (E_RASTER_STA+k%3); // k%3 to cycle through A,X,Y regs
			i.loose.value=(random(128)*2);

			if (k>E_COLBAK && y%2==1 && dest_colors>4)
				i.loose.target=(e_target) (k+4); // position
			else
				i.loose.target=(e_target) k;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=4;	

			assert(r->raster_lines[y].cycles <= raster_program_cycle_limit);
		}

		r->raster_lines[y].rehash();

		FreeImage_Unload(f_copy24bits);
		FreeImage_Unload(f_quant);
		FreeImage_Unload(f_copy24bits2);
	}
	FreeImage_Unload(f_copy);
}

void RastaConverter::CreateRandomRasterPicture(raster_picture *r)
{
	SRasterInstruction i;
	int x;
	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));

	x=random(m_width); 
	r->mem_regs_init[E_COLPM0]=FindAtariColorIndex(m_picture[0][x])*2;
	const int spriteStart = SpriteScreenColorCycleStart(r->graphics_mode);
	r->mem_regs_init[E_HPOSP0]=x+spriteStart;

	x=random(m_width); 
	r->mem_regs_init[E_COLPM1]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP1]=x+spriteStart;

	x=random(m_width); 
	r->mem_regs_init[E_COLPM2]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP2]=x+spriteStart;

	x=random(m_width); 
	r->mem_regs_init[E_COLPM3]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP3]=x+spriteStart;

	for (size_t y=0;y<r->raster_lines.size();++y)
	{
		// lda random
		i.loose.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLOR0;
		r->raster_lines[y].instructions.push_back(i);
		// sta 
		i.loose.instruction=E_RASTER_STA;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLOR0;
		r->raster_lines[y].instructions.push_back(i);

		// ldx random
		i.loose.instruction=E_RASTER_LDX;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLOR1;
		r->raster_lines[y].instructions.push_back(i);
		// stx 
		i.loose.instruction=E_RASTER_STX;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLOR1;
		r->raster_lines[y].instructions.push_back(i);

		// ldy random
		i.loose.instruction=E_RASTER_LDY;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLOR2;
		r->raster_lines[y].instructions.push_back(i);
		// sty 
		i.loose.instruction=E_RASTER_STY;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLOR2;
		r->raster_lines[y].instructions.push_back(i);

		// lda random
		i.loose.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLBAK;
		r->raster_lines[y].instructions.push_back(i);
		// sty 
		i.loose.instruction=E_RASTER_STA;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLBAK;
		r->raster_lines[y].instructions.push_back(i);

		assert(r->raster_lines[y].cycles <= raster_program_cycle_limit);
	}
}

void RastaConverter::OptimizeRasterProgram(raster_picture *pic)
{
	struct previous_reg_usage {
		int i;
		int y;
	};
	/*
		E_RASTER_LDA,
		E_RASTER_LDX,
		E_RASTER_LDY,
	*/

	previous_reg_usage p_usage[3]=  // a,x,y;
	{ 
		{ -1, -1 },
		{ -1, -1 },
		{ -1, -1 }
	};

	for (int y=0;y<m_height;++y)
	{
		size_t size=pic->raster_lines[y].instructions.size();
		SRasterInstruction *__restrict rastinsns = &pic->raster_lines[y].instructions[0];
		for (size_t i=0;i<size;++i)
		{
			unsigned char ins=rastinsns[i].loose.instruction;
			if (ins<=E_RASTER_LDY)
			{
				if (p_usage[ins].i != -1)
				{
					// nop previous usage of this register
					pic->raster_lines[ p_usage[ins].y ].instructions[ p_usage[ins].i ].loose.instruction=E_RASTER_NOP;
				}
				p_usage[ins].i=i;
				p_usage[ins].y=y;
			}
			else if (ins>=E_RASTER_STA)
			{
				p_usage[ins-E_RASTER_STA].i=-1;
			}

		}	
	}

	// Dead-load elimination changes the executable instruction stream. Cached
	// line keys/results still describe the pre-optimized stream unless every
	// line is rehashed and recached before export.
	for (raster_line& line : pic->raster_lines)
	{
		line.rehash();
		line.cache_key = nullptr;
	}
}

void RastaConverter::FindPossibleColors()
{
	m_eval_gstate.m_possible_colors_for_each_line.resize(m_height);
	set < unsigned char > set_of_colors;

	// For each screen line set the possible colors
	vector < unsigned char > vector_of_colors;
	for (int l=m_height-1;l>=0;--l)
	{
		for (int x=0;x<m_width;++x)
			set_of_colors.insert(FindAtariColorIndex(m_picture[l][x])*2);				

		// copy set to vector
		vector_of_colors.resize(set_of_colors.size());
		copy(set_of_colors.begin(), set_of_colors.end(), vector_of_colors.data());
		m_eval_gstate.m_possible_colors_for_each_line[l]=vector_of_colors;
	}
}

void RastaConverter::Init()
{
	if (!cfg.continue_processing)
	{
		raster_picture m(m_height);
		m.graphics_mode = cfg.graphics_mode;
		init_finished=false;

		if (cfg.graphics_mode == GraphicsMode::Antic4)
		{
			CreateEmptyRasterPicture(&m);
			m.antic4_attributes.assign(m_height / 8, 0);
			const e_target initialTargets[5] = {
				E_COLOR0, E_COLOR1, E_COLOR2, E_COLBAK, E_COLOR3
			};
			size_t targetIndex = 0;
			for (unsigned char color : color_indexes_on_dst_picture)
			{
				if (targetIndex == std::size(initialTargets))
					break;
				m.mem_regs_init[initialTargets[targetIndex++]] =
					static_cast<unsigned char>(color * 2);
			}
		}
		else if (color_indexes_on_dst_picture.size()<5)
			CreateLowColorRasterPicture(&m);
		else if (cfg.init_type==E_INIT_RANDOM)
			CreateRandomRasterPicture(&m);
		else if (cfg.init_type==E_INIT_EMPTY)
			CreateEmptyRasterPicture(&m);
		else // LESS or SMART
			CreateSmartRasterPicture(&m);

		m_eval_gstate.m_best_pic = m;
	}

	init_finished=true;
}

void RastaConverter::ApplyInternalStructuredInitializer()
{
	const char* profile = std::getenv("RASTA_STRUCTURED_INITIALIZER");
	ApplyInternalStructuredPass(profile, "initializer", false);
}

void RastaConverter::ApplyInternalStructuredFinalizer()
{
	const char* profile = std::getenv("RASTA_STRUCTURED_FINALIZER");
	ApplyInternalStructuredPass(profile, "finalizer", true);
}

void RastaConverter::ApplyInternalStructuredPass(
	const char* profile, const char* label, bool publishResult)
{
	if (profile == nullptr || profile[0] == '\0')
		return;
	const std::string profileName(profile);
	if (profileName != "ntsc")
	{
		Message(std::string("Structured ") + label
			+ " disabled for profile: " + profile);
		return;
	}
	if (cfg.dual_mode || m_evaluators.empty()
		|| cfg.graphics_mode == GraphicsMode::Antic4
		|| m_eval_gstate.m_best_pic.raster_lines.size()
			!= static_cast<std::size_t>(m_height))
		return;
	if (publishResult)
	{
		const auto current = std::atomic_load_explicit(
			&m_eval_gstate.m_best_snapshot, std::memory_order_acquire);
		if (current)
		{
			m_eval_gstate.m_best_pic = current->picture;
			m_eval_gstate.m_best_pic.uncache_insns();
		}
	}

	StructuredBeamOptions options;
	options.width = 16;
	options.diversity_per_state = 1;
	options.repair_cost_per_pixel = 0.0;
	std::size_t feasible = 0;
	std::size_t accepted = 0;
	distance_accum_t totalImprovement = 0;
	distance_accum_t totalSourceOklabImprovement = 0;
	Evaluator& evaluator = m_evaluators.front();
	for (std::size_t line = 0; line < static_cast<std::size_t>(m_height); ++line)
	{
		const Evaluator::StructuredWindowComparison comparison =
			evaluator.ApplyStructuredSourceWindowIfBetter(
				m_eval_gstate.m_best_pic, line, 1, 1, options, publishResult);
		if (!comparison.feasible)
			continue;
		++feasible;
		if (comparison.accepted)
		{
			++accepted;
			totalImprovement += comparison.baseline_score
				- comparison.structured_score;
			totalSourceOklabImprovement += comparison.baseline_source_oklab
				- comparison.structured_source_oklab;
		}
	}
	if (publishResult)
	{
		std::vector<const line_cache_result*> results(m_height, nullptr);
		const distance_accum_t finalScore = evaluator.EvaluateSingle(
			&m_eval_gstate.m_best_pic, results.data());
		m_eval_gstate.m_best_result.store(finalScore, std::memory_order_relaxed);
		m_eval_gstate.m_created_picture.resize(m_height);
		m_eval_gstate.m_created_picture_targets.resize(m_height);
		for (int y = 0; y < m_height; ++y)
		{
			const line_cache_result& result = *results[y];
			m_eval_gstate.m_created_picture[y].assign(
				result.color_row, result.color_row + m_width);
			m_eval_gstate.m_created_picture_targets[y].resize(m_width);
			result.copy_target_row(
				m_eval_gstate.m_created_picture_targets[y].data(), m_width);
		}
		memcpy(&m_eval_gstate.m_sprites_memory, &evaluator.GetSpritesMemory(),
			sizeof m_eval_gstate.m_sprites_memory);
		auto snapshot = std::make_shared<EvalGlobalState::PublishedBestSnapshot>();
		snapshot->picture = m_eval_gstate.m_best_pic;
		snapshot->picture.uncache_insns();
		snapshot->cost = finalScore;
		const auto previous = std::atomic_load_explicit(
			&m_eval_gstate.m_best_snapshot, std::memory_order_acquire);
		snapshot->version = previous ? previous->version + 1 : 1;
		m_eval_gstate.m_best_state_version.store(
			snapshot->version, std::memory_order_release);
		std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> published = snapshot;
		std::atomic_store_explicit(&m_eval_gstate.m_best_snapshot,
			std::move(published), std::memory_order_release);
	}
	Message(std::string("Structured ") + profileName + ' ' + label
		+ ": feasible=" + Value2String(feasible)
		+ " accepted=" + Value2String(accepted)
		+ " raw_improvement=" + Value2String(totalImprovement)
		+ " source_oklab_improvement="
		+ Value2String(totalSourceOklabImprovement));
}

void RastaConverter::TestRasterProgram(raster_picture *pic)
{
	int x,y;
	rgb white;
	rgb black;
	white.g=white.b=white.r=255;
	black.g=black.b=black.r=0;

	for (y=0;y<m_height;++y)
	{
		pic->raster_lines[y].cycles=6;
		pic->raster_lines[y].instructions.resize(2);
		pic->raster_lines[y].instructions[0].loose.instruction=E_RASTER_LDA;
		if (y%2==0)
			pic->raster_lines[y].instructions[0].loose.value=0xF;
		else
			pic->raster_lines[y].instructions[0].loose.value=0x33;

		pic->raster_lines[y].instructions[1].loose.instruction=E_RASTER_STA;
		pic->raster_lines[y].instructions[1].loose.target=E_COLOR2;


		for (x=0;x<m_width;++x)
			m_picture[y][x]=black;
		for (int i=0;i<CYCLES_MAX;++i)
		{
			x=screen_cycles[i].offset;
			if (x>=0 && x<m_width)
				m_picture[y][x]=white;
		}
	}
}

std::string RastaConverter::BuildConfigRecap() const
{
	// The restart-only recap of design §9.4: what produced the picture in
	// front of you, in one readable line.
	static const char* const kDistance[] = {"euclid", "yuv", "ciede", "cie94", "oklab", "rasta"};
	static const char* const kDither[] = {"none", "floyd", "rfloyd", "line", "line2",
		"chess", "simple", "2d", "jarvis", "knoll"};
	static const char* const kOptimizer[] = {"dlas", "lahc", "legacy"};
	static const char* const kInit[] = {"random", "smart", "empty", "less"};
	static const char* const kObjective[] = {"target", "source"};

	std::ostringstream out;
	out << "palette " << cfg.palette_file
		<< "  |  distance " << kDistance[cfg.dstf]
		<< "  |  target " << kDistance[cfg.pre_dstf]
		<< "  |  dither " << kDither[cfg.dither]
		<< "\n" << "objective " << kObjective[cfg.visual_objective]
		<< "  |  optimizer " << kOptimizer[cfg.optimizer]
		<< "  |  history " << solutions
		<< "  |  init " << kInit[cfg.init_type]
		<< "  |  graphics "
		<< (cfg.graphics_mode == GraphicsMode::Antic4 ? "ANTIC 4" : "ANTIC E");
	if (cfg.dual_mode)
		out << "\n" << "dual frame on, blending " << cfg.dual_blending;
	if (!cfg.details_file.empty())
		out << "\n" << "details mask " << cfg.details_file << " (" << cfg.details_mode << ")";
	return out.str();
}

void RastaConverter::PublishLiveStats(bool preprocessing, bool finished)
{
	if (!gui.LiveUiActive())
		return;

	LiveStats stats;
	stats.evaluations = m_eval_gstate.m_evaluations.load(std::memory_order_relaxed);
	stats.last_best_evaluation =
		m_eval_gstate.m_last_best_evaluation.load(std::memory_order_relaxed);
	// The parser's "no limit" default is a huge sentinel; report it as no limit
	// so the dashboard shows "runs until stopped" rather than a fake progress bar.
	stats.max_evals = cfg.max_evals >= 1000000000000000000ULL ? 0 : cfg.max_evals;
	stats.rate = m_rate;
	// Before the first evaluation the best result is unset; normalizing it
	// produces a meaningless number, so leave it at zero and let the dashboard
	// say it has nothing yet.
	stats.normalized_distance =
		m_eval_gstate.m_evaluations.load(std::memory_order_relaxed) > 0
			? NormalizeScore(m_eval_gstate.m_best_result) : 0.0;
	stats.normalized_drift = m_eval_gstate.m_current_norm_drift;
	stats.unstuck_after = cfg.unstuck_after;
	stats.elapsed_seconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - m_run_started).count();

	for (int i = 0; i < E_MUTATION_MAX; ++i) {
		if (!cfg.dual_mode && i == E_MUTATION_COMPLEMENT_VALUE_DUAL)
			continue;
		if (cfg.graphics_mode != GraphicsMode::Antic4
			&& i == E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE)
			continue;
		LiveStats::MutationStat stat;
		stat.name = mutation_names[i];
		stat.count = m_eval_gstate.m_mutation_stats[i];
		stats.mutations.push_back(stat);
	}

	stats.accepted = m_eval_gstate.m_single_accepted.load(std::memory_order_relaxed);
	stats.global_improvements =
		m_eval_gstate.m_single_global_improvements.load(std::memory_order_relaxed);
	stats.migrations = m_eval_gstate.m_single_migrations.load(std::memory_order_relaxed);
	stats.cache_hits = m_eval_gstate.m_cache_hits.load(std::memory_order_relaxed);
	stats.cache_lookups = m_eval_gstate.m_cache_lookups.load(std::memory_order_relaxed);

	stats.dual_mode = cfg.dual_mode;
	if (cfg.dual_mode) {
		const EvalGlobalState::DualPhase phase =
			m_eval_gstate.m_dual_phase.load(std::memory_order_relaxed);
		switch (phase) {
		case EvalGlobalState::DUAL_PHASE_BOOTSTRAP_A:
			stats.dual_phase = "Bootstrap A";
			stats.dual_block_steps = cfg.first_dual_steps;
			stats.dual_block_progress = std::min(stats.evaluations, cfg.first_dual_steps);
			break;
		case EvalGlobalState::DUAL_PHASE_BOOTSTRAP_B:
			stats.dual_phase =
				m_eval_gstate.m_dual_bootstrap_b_copied.load(std::memory_order_relaxed)
					? "Bootstrap B (copied from A)" : "Bootstrap B (generated)";
			stats.dual_block_steps = cfg.first_dual_steps;
			break;
		case EvalGlobalState::DUAL_PHASE_ALTERNATING:
			stats.dual_phase = "Alternating";
			stats.dual_block_steps = cfg.altering_dual_steps;
			if (cfg.altering_dual_steps > 0)
				stats.dual_block_progress = stats.evaluations % cfg.altering_dual_steps;
			break;
		default:
			stats.dual_phase = "-";
			break;
		}
		stats.dual_focus_b = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);
		stats.dual_display = (m_dual_display == DualDisplayMode::A) ? 'A'
			: (m_dual_display == DualDisplayMode::B) ? 'B' : 'M';
	}

	stats.input_file = cfg.input_file;
	stats.output_file = cfg.output_file;
	stats.command_line = cfg.command_line;
	stats.config_recap = BuildConfigRecap();
	stats.threads = cfg.threads;
	stats.cache_mb = cfg.cache_size / (1024 * 1024);
	stats.preprocessing = preprocessing;
	stats.finished = finished;
	stats.editor_available = !cfg.dual_mode && !preprocessing && !finished;
	stats.destination_edit_available = !cfg.dual_mode
		&& cfg.visual_objective == E_OBJECTIVE_LEGACY_TARGET
		&& !preprocessing && !finished;
	stats.editor_paused = m_editor_paused;
	stats.details_floor = cfg.details_floor;
	stats.details_feather = cfg.details_feather;
	stats.mask_edited = m_mask_edited;
	stats.details_mode = cfg.details_mode;
	stats.details_strength = cfg.details_strength;
	stats.details_score = cfg.details_score;
	stats.objective_revision =
		m_eval_gstate.m_objective_generation.load(std::memory_order_relaxed);
	stats.last_retarget_ms = m_last_retarget_ms;
	stats.message = m_last_message;
	if (m_ever_saved) {
		stats.last_save_seconds_ago = std::chrono::duration<double>(
			std::chrono::steady_clock::now() - m_last_save_time).count();
	}

	gui.PublishStats(stats);
}

void RastaConverter::ShowMutationStats()
{
	// Image captions may be as low as y=250 for a 240-line source. Keep the
	// status block below that caption row while leaving the final mutation line
	// clear of the persistent message row at y=450.
	constexpr int status_top = 270;
	constexpr int status_line_height = 20;
	constexpr int mutation_line_height = 18;
	int row = 0;
	for (int i=0;i<E_MUTATION_MAX;++i)
	{
		// Show dual-only mutation stat only in dual mode
		if (!cfg.dual_mode && i == E_MUTATION_COMPLEMENT_VALUE_DUAL) continue;
		if (cfg.graphics_mode != GraphicsMode::Antic4
			&& i == E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE) continue;
		gui.DisplayText(0, status_top + mutation_line_height * row,
			string(mutation_names[i]) + string("  ")
			+ format_with_commas(m_eval_gstate.m_mutation_stats[i]));
		++row;
	}

	gui.DisplayText(320, status_top + status_line_height,
		string("Evaluations: ") + format_with_commas(
			m_eval_gstate.m_evaluations.load(std::memory_order_relaxed)));
	gui.DisplayText(320, status_top + 2 * status_line_height,
		string("LastBest: ") + format_with_commas(
			m_eval_gstate.m_last_best_evaluation.load(std::memory_order_relaxed))
		+ string("                "));
	gui.DisplayText(320, status_top + 3 * status_line_height,
		string("Rate: ") + format_with_commas((unsigned long long)m_rate)
		+ string("                "));
	{
		double norm = NormalizeScore(m_eval_gstate.m_best_result);
		std::string line = std::string("Norm. Dist: ") + format_with_commas(norm);
		// Show current normalized drift if active
		if (m_eval_gstate.m_current_norm_drift > 0.0 && m_eval_gstate.m_unstuck_after > 0 && m_eval_gstate.m_evaluations > m_eval_gstate.m_last_best_evaluation) {
			unsigned long long plateau = m_eval_gstate.m_evaluations - m_eval_gstate.m_last_best_evaluation;
			if (plateau >= m_eval_gstate.m_unstuck_after) {
				line += std::string(" (+") + format_with_commas(m_eval_gstate.m_current_norm_drift) + std::string(")");
			}
		}
		line += std::string("                ");
		gui.DisplayText(320, status_top + 4 * status_line_height, line);
	}

	// Additional dual-mode status lines
	if (cfg.dual_mode)
	{
		const bool focusB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);
		const char *showing = (m_dual_display == DualDisplayMode::A) ? "A" : (m_dual_display == DualDisplayMode::B) ? "B" : "M";
		EvalGlobalState::DualPhase phase = m_eval_gstate.m_dual_phase.load(std::memory_order_relaxed);
		std::string phaseText;
		switch (phase) {
			case EvalGlobalState::DUAL_PHASE_BOOTSTRAP_A: phaseText = "Phase: Bootstrap A"; break;
			case EvalGlobalState::DUAL_PHASE_BOOTSTRAP_B: {
				bool copied = m_eval_gstate.m_dual_bootstrap_b_copied.load(std::memory_order_relaxed);
				phaseText = copied ? "Phase: Bootstrap B (copy)" : "Phase: Bootstrap B (generate)"; break;
			}
			case EvalGlobalState::DUAL_PHASE_ALTERNATING: phaseText = std::string("Phase: Alternating, optimizing ") + (focusB ? "B" : "A"); break;
			default: phaseText = "Phase: -"; break;
		}
		gui.DisplayText(320, status_top + 5 * status_line_height, phaseText);
		gui.DisplayText(320, status_top + 6 * status_line_height,
			std::string("Showing: ") + showing);
		gui.DisplayText(320, status_top + 7 * status_line_height,
			"Press [A] [B] [M]ix");
	}
}

void RastaConverter::SaveBestSolution()
{
	if (!init_finished)
		return;

	// Note that we are assuming that we have exclusive access to global state.

	if (!cfg.dual_mode) {
		SaveEditedMaskArtifact();
		SaveEditedTargetArtifact();
		const std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> snapshot =
			std::atomic_load_explicit(&m_eval_gstate.m_best_snapshot, std::memory_order_acquire);
		raster_picture pic = snapshot ? snapshot->picture : m_eval_gstate.m_best_pic;

		SaveRasterProgram(string(cfg.output_file+".rp"), &pic);
		OptimizeRasterProgram(&pic);
		// Export every derived artifact from the exact optimized program that
		// will be assembled into the XEX, never from a worker's pre-optimization
		// cache rows.
		RenderCreatedPicture(pic);
		ShowLastCreatedPicture();
		SaveRasterProgram(string(cfg.output_file+".opt"), &pic);
		SavePMG(string(cfg.output_file+".pmg"));
		if (pic.graphics_mode == GraphicsMode::Antic4)
			SaveAntic4Data(cfg.output_file + ".a4.scr",
				cfg.output_file + ".a4.fnt", pic);
		else
			SaveScreenData(string(cfg.output_file+".mic").c_str());
		SavePicture     (cfg.output_file,output_bitmap);
		SaveStatistics((cfg.output_file+".csv").c_str());
		SaveOptimizerState((cfg.output_file+".optstate").c_str(), &pic);
		m_mask_edited_since_save = false;
		m_ever_saved = true;
		m_last_save_time = std::chrono::steady_clock::now();
		return;
	}

	// Dual-mode saving: save A, B, and blended
	ShowLastCreatedPictureDual();

	// Build directory prefix from cfg.output_file so dual outputs go to the same folder
	std::string __out_dir_prefix;
	{
		std::string __of = cfg.output_file;
		size_t __pos = __of.find_last_of("/\\");
		__out_dir_prefix = (__pos == std::string::npos) ? std::string() : __of.substr(0, __pos + 1);
	}

	// A
	{
		raster_picture picA = m_eval_gstate.m_best_pic;
		SaveRasterProgram(__out_dir_prefix + string("out_dual_A.rp"), &picA);
		OptimizeRasterProgram(&picA);
		SaveRasterProgram(__out_dir_prefix + string("out_dual_A.opt"), &picA);
		SavePMGWithSprites(__out_dir_prefix + string("out_dual_A.pmg"), m_eval_gstate.m_sprites_memory);
		SaveScreenDataFromTargets((__out_dir_prefix + string("out_dual_A.mic")).c_str(), m_eval_gstate.m_created_picture_targets);
		if (output_bitmap_A) SavePicture(__out_dir_prefix + string("out_dual_A.png"), output_bitmap_A);
	}
	// B
	{
		raster_picture picB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
		SaveRasterProgram(__out_dir_prefix + string("out_dual_B.rp"), &picB);
		OptimizeRasterProgram(&picB);
		SaveRasterProgram(__out_dir_prefix + string("out_dual_B.opt"), &picB);
		SavePMGWithSprites(__out_dir_prefix + string("out_dual_B.pmg"), m_sprites_memory_B);
		if (m_created_picture_targets_B.empty()) m_created_picture_targets_B = m_eval_gstate.m_created_picture_targets; // fallback
		SaveScreenDataFromTargets  ((__out_dir_prefix + string("out_dual_B.mic")).c_str(), m_created_picture_targets_B);
		if (output_bitmap_B) SavePicture(__out_dir_prefix + string("out_dual_B.png"), output_bitmap_B);
	}
	// Blended
	if (output_bitmap_blended) SavePicture(__out_dir_prefix + string("out_dual_blended.png"), output_bitmap_blended);

	// Stats
	SaveStatistics((cfg.output_file+".csv").c_str());
	SaveOptimizerState((cfg.output_file+".optstate").c_str());
}

RastaConverter::RastaConverter()
	: init_finished(false)
	, m_needs_history_reconfigure(false)
{
}

void RastaConverter::MainLoop()
{
	Message("Optimization started.");

	output_bitmap = FreeImage_Allocate(cfg.width, cfg.height, 24);
	if (cfg.dual_mode) {
		output_bitmap_A = FreeImage_Allocate(cfg.width, cfg.height, 24);
		output_bitmap_B = FreeImage_Allocate(cfg.width, cfg.height, 24);
		output_bitmap_blended = FreeImage_Allocate(cfg.width, cfg.height, 24);
	}

	DBG_PRINT("[RASTA] MainLoop start (dual=%d)", (int)cfg.dual_mode);

	FindPossibleColors();

	Init();
	ApplyInternalStructuredInitializer();

	// Mark optimization start time for statistics (seconds since start)
	m_eval_gstate.m_time_start = time(NULL);
	m_previous_save_time = std::chrono::steady_clock::now();

	bool clean_first_evaluation = cfg.continue_processing;
	auto last_rate_check_tp = std::chrono::steady_clock::now();
	auto last_ui_frame_tp = last_rate_check_tp;

	bool pending_update = false;

	// spin up only one evaluator -- we need its result before the rest can go, unless in dual mode
	// Do not hold the lock across UI work; acquire on demand
	std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex, std::defer_lock };
	lock.lock();
	if (!cfg.dual_mode)
		m_evaluators[0].Start();

	unsigned long long last_eval = 0;
	bool eval_inited = false;
	bool remaining_workers_started = m_evaluators.size() <= 1;
	auto startRemainingWorkers = [&]()
	{
		if (remaining_workers_started || !eval_inited)
			return;

		// Start delayed workers outside the global lock.
		lock.unlock();
		for (size_t i = 1; i < m_evaluators.size(); ++i)
			m_evaluators[i].Start();
		lock.lock();
		remaining_workers_started = true;
	};

	if (cfg.dual_mode) {
		lock.unlock();
		DBG_PRINT("[RASTA] Enter MainLoopDual");
		MainLoopDual();
		return;
	}

	bool running = true;
	auto handleGuiCommand = [&](GUI_command command)
	{
		switch (command)
		{
		case GUI_command::SAVE:
			if (m_editor_paused) {
				Message("Apply or discard the open edit before saving.");
				break;
			}
			SaveBestSolution();
			Message("Saved.");
			break;
		case GUI_command::STOP:
			if (m_editor_paused)
				DiscardEditorSession();
			running = false;
			break;
		case GUI_command::CONTINUE:
			break;
		case GUI_command::REDRAW:
			ShowInputBitmap();
			if (destination_bitmap) ShowDestinationBitmap();
			ShowLastCreatedPicture();
			ShowMutationStats();
			PublishLiveStats(/*preprocessing*/ false, /*finished*/ false);
			gui.Present();
			break;
		case GUI_command::EDITOR_BEGIN:
			BeginEditorSession(gui.EditorWantsDestination());
			break;
		case GUI_command::EDITOR_APPLY:
			{
				GuiEditorApply request;
				if (gui.TakeEditorApply(request))
					ApplyEditorSession(request);
				else
					DiscardEditorSession();
			}
			break;
		case GUI_command::EDITOR_DISCARD:
			DiscardEditorSession();
			break;
		case GUI_command::SHOW_A:
		case GUI_command::SHOW_B:
		case GUI_command::SHOW_MIX:
			break;
		}
	};
	while (running)
	{
		// Ctrl+C, a kill, or the terminal closing. Treated exactly as the Stop
		// button: leave the loop, let MainLoop return, and let the caller save.
		// An editor session has the workers parked, so it has to be unwound
		// first or the shutdown would wait for threads that are not running.
		if (interrupts::StopRequested()) {
			if (lock.owns_lock()) lock.unlock();
			// An editor session has the workers parked; unwinding it takes the
			// lock itself, so this happens with the lock released.
			if (m_editor_paused)
				DiscardEditorSession();
			Message("Interrupted - saving.");
			// Re-taken before leaving: everything after this loop - raising
			// m_finished, and waiting for the workers to drain - runs under the
			// lock, and std::condition_variable::wait on a mutex this thread
			// does not hold waits forever.
			if (!lock.owns_lock()) lock.lock();
			running = false;
			break;
		}

		// Release global lock during UI/rendering to avoid blocking workers
		if (lock.owns_lock()) lock.unlock();

		if (eval_inited && !cfg.quiet)
		{
			auto next_rate_check_tp = std::chrono::steady_clock::now();

			double secs = std::chrono::duration<double>(next_rate_check_tp - last_rate_check_tp).count();
			const bool statsDue = secs > 0.25;
			if (statsDue)
			{
				m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) / secs;

				last_rate_check_tp = next_rate_check_tp;
				last_eval = m_eval_gstate.m_evaluations;

				if (pending_update)
				{
					pending_update = false;
					ShowLastCreatedPicture();
				}

				ShowMutationStats();
				PublishLiveStats(/*preprocessing*/ false, /*finished*/ false);
			}
			const bool liveFrameDue = gui.LiveUiActive()
				&& next_rate_check_tp - last_ui_frame_tp
					>= std::chrono::milliseconds(16);
			if (statsDue || liveFrameDue) {
				last_ui_frame_tp = next_rate_check_tp;
				handleGuiCommand(gui.NextFrame());
			}
		}

		// Reacquire lock before waiting on condition/flags
		if (!lock.owns_lock()) lock.lock();
		startRemainingWorkers();

		// Nothing left to wait for. Every wake-up of this loop comes from a
		// worker, so once they have all gone and the search is not paused
		// mid-edit, waiting is waiting forever: two runs were found still
		// sitting here after five and a half hours, ticking over at 5% CPU with
		// no worker threads left and nothing written but the preprocessed
		// images. Whatever ends the workers early, the answer here is to stop.
		if (eval_inited && !m_editor_paused && m_eval_gstate.m_threads_active == 0
			&& !m_eval_gstate.m_finished) {
			Message("All workers have stopped - finishing.");
			break;
		}

		auto now = std::chrono::steady_clock::now();
		auto deadline = now + (gui.LiveUiActive()
			? std::chrono::milliseconds(16) : std::chrono::milliseconds(250));

		if ( std::cv_status::timeout == m_eval_gstate.m_condvar_update.wait_until( lock, deadline ) )
			continue;

		if (m_eval_gstate.m_update_initialized)
		{
			m_eval_gstate.m_update_initialized = false;
			eval_inited = true;
			pending_update = true;
		}
		startRemainingWorkers();

		if (m_eval_gstate.m_update_improvement)
		{
			m_eval_gstate.m_update_improvement = false;

			pending_update = true;
		}

		if (cfg.save_period == -1 && !m_editor_paused) // auto
		{
			using namespace std::literals::chrono_literals;
			if ( now - m_previous_save_time > 30s )
			{
				m_previous_save_time = now;
				SaveBestSolution();
			}
		}
		else if (!m_editor_paused && m_eval_gstate.m_update_autosave)
		{
			m_eval_gstate.m_update_autosave = false;
			SaveBestSolution();
		}

		if (m_eval_gstate.m_finished)
		{
			if (m_eval_gstate.m_best_result == 0)
			{
				Message("FINISHED: distance=0");
			}
			break;
		}
	}

	m_eval_gstate.m_finished = true;
	m_eval_gstate.m_condvar_update.notify_all();

	while(m_eval_gstate.m_threads_active > 0)
	{
		m_eval_gstate.m_condvar_update.wait( lock );
	}
}

void RastaConverter::RenderCreatedPicture(raster_picture& picture)
{
	if (!m_reporting_evaluator)
		return;
	std::vector<const line_cache_result*> results(m_height, nullptr);
	Evaluator& evaluator = *m_reporting_evaluator;
	// Published pictures can still carry instruction identities owned by a
	// worker evaluator. Re-intern them in the reporting evaluator before
	// rendering so saving never reads worker cache storage.
	evaluator.RecachePicture(&picture, true);
	evaluator.EvaluateSingle(&picture, results.data());
	m_eval_gstate.m_created_picture.resize(m_height);
	m_eval_gstate.m_created_picture_targets.resize(m_height);
	for (int y = 0; y < m_height; ++y) {
		if (results[y] == nullptr)
			continue;
		const line_cache_result& result = *results[y];
		m_eval_gstate.m_created_picture[y].assign(
			result.color_row, result.color_row + m_width);
		m_eval_gstate.m_created_picture_targets[y].resize(m_width);
		result.copy_target_row(
			m_eval_gstate.m_created_picture_targets[y].data(), m_width);
	}
	memcpy(&m_eval_gstate.m_sprites_memory, &evaluator.GetSpritesMemory(),
		sizeof m_eval_gstate.m_sprites_memory);
}

void RastaConverter::ShowLastCreatedPicture()
{
	int x,y;
	// Draw new picture on the screen
	for (y=0;y<m_height;++y)
	{
		for (x=0;x<m_width;++x)
		{
			rgb atari_color=atari_palette[m_eval_gstate.m_created_picture[y][x]];
			RGBQUAD color=RGB2PIXEL(atari_color);
			FreeImage_SetPixelColor(output_bitmap, x, y, &color);
		}
	}

	int w = FreeImage_GetWidth(output_bitmap);
	gui.DisplayBitmap(w, 0, output_bitmap);
	gui.PublishImage(GuiImageSlot::Output, output_bitmap);
}

void RastaConverter::SavePMG(string name)
{
    size_t sprite,y,bit;
    unsigned char b;
    Message("Saving sprites (PMG)");

    std::ofstream out(Utf8Path(name), std::ios::out | std::ios::trunc);
    if (!out)
        Error("Error saving PMG handler");

    out << "; ---------------------------------- \n";
    out << "; RastaConverter by Ilmenit v." << program_version << '\n';
    out << "; ---------------------------------- \n";

    out << "missiles\n";

    out << "\t.ds $100\n";


    for(sprite=0;sprite<4;++sprite)
    {
        out << "player" << static_cast<int>(sprite) << '\n';
        out << "\t.he 00 00 00 00 00 00 00 00";
        for (y=0;y<240;++y)
        {
            b=0;
            for (bit=0;bit<8;++bit)
            {
                if (y > (size_t)m_height)
                    m_eval_gstate.m_sprites_memory[y][sprite][bit]=0;

                b|=(m_eval_gstate.m_sprites_memory[y][sprite][bit])<<(7-bit);
            }
            out << ' ' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            out << std::nouppercase << std::dec;
            if (y%16==7)
                out << "\n\t.he";
        }
        out << " 00 00 00 00 00 00 00 00\n";
    }
    if (!out)
        Error("Error finalizing PMG handler");
}

bool RastaConverter::GetInstructionFromString(const string& line, SRasterInstruction &instr)
{
	static const char *load_names[3]=
	{
		"lda",
		"ldx",
		"ldy",
	};
	static const char *store_names[3]=
	{
		"sta",
		"stx",
		"sty",
	};

	size_t pos_comment, pos_instr, pos_value, pos_target;

	if (line.find(":")!=string::npos)
		return false;

	pos_comment=line.find(";");
	if (pos_comment==string::npos)
		pos_comment=INT_MAX;

	pos_value=line.find("$");

	size_t i,j;

	instr.loose.instruction=E_RASTER_MAX;

	if (line.find("nop") != string::npos)
	{
		instr.loose.instruction = E_RASTER_NOP;
		instr.loose.value = 0;
		instr.loose.target = E_COLBAK;
		return true;
	}

	// check load instructions
	for (i=0;i<3;++i)
	{
		pos_instr=line.find(load_names[i]);
		if (pos_instr!=string::npos)
		{
			if (pos_instr<pos_comment)
			{
				instr.loose.instruction= (e_raster_instruction) (E_RASTER_LDA+i);
				pos_value=line.find("$");
				if (pos_value==string::npos)
					gui.Error("Load instruction: No value for Load Register");
				++pos_value;
				string val_string=line.substr(pos_value,2);
				instr.loose.value=String2HexValue<int>(val_string);
				instr.loose.target = E_TARGET_MAX;
				return true;
			}
		}
	}
	// check store instructions
	for (i=0;i<3;++i)
	{
		pos_instr=line.find(store_names[i]);
		if (pos_instr!=string::npos)
		{
			if (pos_instr<pos_comment)
			{
				instr.loose.instruction=(e_raster_instruction) (E_RASTER_STA+i);
				// find target
				for (j=0;j<=E_TARGET_MAX;++j)
				{
					pos_target=line.find(mem_regs_names[j]);
					if (pos_target!=string::npos)
					{
						instr.loose.target=(e_target) (E_COLOR0+j);
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

void RastaConverter::LoadRegInits(string name)
{
	Message("Loading Reg Inits");

	fstream f;
	f.open( name.c_str(), ios::in);
	if ( f.fail())
		Error("Error loading reg inits");

	string line;
	SRasterInstruction instr;

	uint8_t a = 0;
	uint8_t x = 0;
	uint8_t y = 0;

	while( getline( f, line)) 
	{
		instr.loose.target=E_TARGET_MAX;
		if (GetInstructionFromString(line,instr))
		{
			switch(instr.loose.instruction)
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
						m_eval_gstate.m_best_pic.mem_regs_init[instr.loose.target] = a;
					break;
				case E_RASTER_STX:
					if (instr.loose.target != E_TARGET_MAX)
						m_eval_gstate.m_best_pic.mem_regs_init[instr.loose.target] = x;
					break;
				case E_RASTER_STY:
					if (instr.loose.target != E_TARGET_MAX)
						m_eval_gstate.m_best_pic.mem_regs_init[instr.loose.target] = y;
					break;
			}
		}
	}

}

void RastaConverter::LoadRasterProgram(string name)
{
	Message("Loading Raster Program");

	fstream f;
	f.open( name.c_str(), ios::in);
	if ( f.fail())
		Error("Error loading Raster Program");

	string line;

	SRasterInstruction instr;
	raster_line current_raster_line;
	current_raster_line.cycles=0;
	size_t pos;
	bool line_started = false;
	bool fixed_antic4_block = false;
	std::vector<std::string> fixed_antic4_lines;
	std::vector<int> fixed_antic4_rows;
	auto normalizedAsm = [](std::string value) {
		const size_t comment = value.find(';');
		if (comment != std::string::npos)
			value.resize(comment);
		const size_t first = value.find_first_not_of(" \t\r\n");
		const size_t last = value.find_last_not_of(" \t\r\n");
		if (first == std::string::npos)
			return std::string();
		value = value.substr(first, last - first + 1);
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	};
	
	while( getline( f, line)) 
	{
		if (line.find("ANTIC4_FIXED_CHBASE_BEGIN") != string::npos)
		{
			if (fixed_antic4_block || !line_started
				|| m_eval_gstate.m_best_pic.graphics_mode != GraphicsMode::Antic4)
				Error("Malformed ANTIC4 fixed CHBASE block");
			fixed_antic4_block = true;
			fixed_antic4_lines.clear();
			continue;
		}
		if (line.find("ANTIC4_FIXED_CHBASE_END") != string::npos)
		{
			if (!fixed_antic4_block)
				Error("Unexpected ANTIC4 fixed CHBASE block end");
			const int y = static_cast<int>(
				m_eval_gstate.m_best_pic.raster_lines.size());
			const std::string expectedLoad = "lda #>charset_"
				+ std::to_string(y / 24 + 1);
			if (y < 0 || y % 24 != 23
				|| fixed_antic4_lines.size() != 3
				|| fixed_antic4_lines[0] != "bit byt2"
				|| fixed_antic4_lines[1] != expectedLoad
				|| fixed_antic4_lines[2] != "sta chbase")
				Error("Invalid ANTIC4 fixed CHBASE instructions");
			fixed_antic4_rows.push_back(y);
			fixed_antic4_block = false;
			current_raster_line.rehash();
			m_eval_gstate.m_best_pic.raster_lines.push_back(current_raster_line);
			current_raster_line.cycles = 0;
			current_raster_line.instructions.clear();
			line_started = false;
			continue;
		}
		if (fixed_antic4_block)
		{
			const std::string instruction = normalizedAsm(line);
			if (!instruction.empty())
				fixed_antic4_lines.push_back(instruction);
			continue;
		}
		// skip filler
		if (line.find("; filler")!=string::npos)
			continue;

		// get info about the file
		pos=line.find("; Evaluations:");
		if (pos!=string::npos)
			m_eval_gstate.m_evaluations=String2Value<unsigned long long>(line.substr(pos+15));

		pos=line.find("; InputName:");
		if (pos!=string::npos)
			cfg.input_file=(line.substr(pos+13));

		pos=line.find("; CmdLine:");
		if (pos!=string::npos)
			cfg.command_line=(line.substr(pos+11));

		pos=line.find("; Details Effective Hash:");
		if (pos!=string::npos)
			m_saved_details_effective_hash=line.substr(pos+26);

		pos=line.find("; Mask Edited:");
		if (pos!=string::npos)
			m_mask_edited=line.substr(pos+15).find("yes") != string::npos;

		pos=line.find("; Destination Edited:");
		if (pos!=string::npos)
			m_destination_edited=line.substr(pos+21).find("yes") != string::npos;

		pos=line.find("; Target Hash:");
		if (pos!=string::npos)
			m_saved_target_hash=line.substr(pos+14);

		pos=line.find("; Snapshots:");
		if (pos!=string::npos)
			m_snapshot_count=String2Value<unsigned>(line.substr(pos+12));

		if (line.find("; Graphics Mode: ANTIC 4") != string::npos)
		{
			m_eval_gstate.m_best_pic.graphics_mode = GraphicsMode::Antic4;
			// The required tagged optstate block supplies the attributes.
			m_eval_gstate.m_best_pic.antic4_attributes.clear();
		}

		if (line.compare(0, 4, "line", 4) == 0)
		{
			line_started = true;
			continue;
		}

		if (!line_started)
			continue;

		if (line.find("ANTIC4_FIXED_LINE_END") != string::npos)
		{
			current_raster_line.rehash();
			m_eval_gstate.m_best_pic.raster_lines.push_back(current_raster_line);
			current_raster_line.cycles = 0;
			current_raster_line.instructions.clear();
			line_started = false;
			continue;
		}

		// if next raster line
		if (line.find("cmp byt2")!=string::npos && current_raster_line.cycles>0)
		{
			current_raster_line.rehash();
			m_eval_gstate.m_best_pic.raster_lines.push_back(current_raster_line);
			current_raster_line.cycles=0;
			current_raster_line.instructions.clear();
			line_started = false;
			continue;
		}

		// add instruction to raster program if proper instruction
		if (GetInstructionFromString(line,instr))
		{
			current_raster_line.cycles+=GetInstructionCycles(instr);
			current_raster_line.instructions.push_back(instr);
		}
	}
	if (fixed_antic4_block)
		Error("Unterminated ANTIC4 fixed CHBASE block");
	if (m_eval_gstate.m_best_pic.graphics_mode == GraphicsMode::Antic4)
	{
		const int pictureHeight = static_cast<int>(
			m_eval_gstate.m_best_pic.raster_lines.size());
		size_t fixedIndex = 0;
		for (int y = 0; y < pictureHeight; ++y)
		{
			if (!IsAntic4ChbaseTransitionLine(y, pictureHeight))
				continue;
			if (fixedIndex >= fixed_antic4_rows.size()
				|| fixed_antic4_rows[fixedIndex] != y)
				Error("Missing or misplaced ANTIC4 fixed CHBASE block");
			++fixedIndex;
		}
		if (fixedIndex != fixed_antic4_rows.size())
			Error("Unexpected ANTIC4 fixed CHBASE block");
	}
}

bool RastaConverter::LoadRasterProgramInto(raster_picture& dst, const std::string& rp_path, const std::string& ini_path)
{
	// The legacy parsers write into m_best_pic and LoadRasterProgram appends
	// scanlines. Clear that scratch destination before every frame so loading B
	// cannot retain A's instructions or register state.
	m_eval_gstate.m_best_pic = raster_picture();
	m_eval_gstate.m_best_pic.raster_lines.clear();
	LoadRegInits(ini_path);
	LoadRasterProgram(rp_path);
	dst = m_eval_gstate.m_best_pic;
	return !dst.raster_lines.empty();
}

bool RastaConverter::Resume()
{
	// Derive base from cfg.output_file and directory
	std::string base = cfg.output_file.empty() ? std::string("output.png") : cfg.output_file;
	// Split dir and filename
	std::string dir;
	{
		size_t pos = base.find_last_of("/\\");
		if (pos != std::string::npos) { dir = base.substr(0, pos + 1); }
	}
	// Expected single-frame paths
	std::string sf_rp = base + ".rp";
	std::string sf_ini = base + ".rp.ini";
	std::string sf_opt = base + ".optstate";
	// Expected dual-frame paths
	std::string df_a_rp = dir + "out_dual_A.rp";
	std::string df_a_ini = dir + "out_dual_A.rp.ini";
	std::string df_b_rp = dir + "out_dual_B.rp";
	std::string df_b_ini = dir + "out_dual_B.rp.ini";

	// Prefer dual if both A and B exist; fallback to single-frame
	auto file_exists = [](const std::string& path) -> bool {
		FILE* f = FopenUtf8(path, "rb"); if (f) { fclose(f); return true; } return false;
	};

	bool has_dual = file_exists(df_a_rp) && file_exists(df_b_rp);
	bool has_single = file_exists(sf_rp);

	if (has_dual)
	{
		// Enable dual mode if not already
		cfg.dual_mode = true;
		// Load into locals then assign to avoid clobbering A with B during parse
		raster_picture picA;
		raster_picture picB;
		if (!LoadRasterProgramInto(picA, df_a_rp, df_a_ini))
			Error(std::string("Error loading dual resume A: ") + df_a_rp);
		if (!LoadRasterProgramInto(picB, df_b_rp, df_b_ini))
			Error(std::string("Error loading dual resume B: ") + df_b_rp);
		if (picA.raster_lines.size() != picB.raster_lines.size())
			Error("Error loading dual resume: A/B raster line counts differ");
		m_eval_gstate.m_best_pic = picA;
		m_best_pic_B = picB;
	}
	else if (has_single)
	{
		// Single-frame resume
		LoadRegInits(sf_ini);
		LoadRasterProgram(sf_rp);
	}
	else
	{
		Error("/continue: no saved program found for resume (looked for single and dual outputs)");
	}
	m_ever_saved = true;
	m_last_save_time = std::chrono::steady_clock::now();
	m_mask_edited_since_save = false;

	// Load optimizer state if present
	if (file_exists(sf_opt)) {
		LoadOptimizerState(sf_opt);
	}

	// Re-parse saved command line to restore other options, but keep current CLI /output if set
	std::string cli_out = cfg.output_file;
	bool keep_cli_out = !cli_out.empty();
	// Which interface we are running is a property of this launch, not of the
	// saved settings: re-parsing decides live_gui by looking for /livegui in the
	// stored tokens, so resuming from the live UI would drop to the old display.
	const bool live_gui = cfg.live_gui;
	cfg.ProcessCmdLine(cfg.resume_override_tokens);
	cfg.live_gui = live_gui;
	m_needs_history_reconfigure = cfg.resume_optimizer_changed || cfg.resume_solutions_changed || cfg.resume_distance_changed || cfg.resume_predistance_changed || cfg.resume_dither_changed || cfg.resume_objective_changed;
	if (keep_cli_out) cfg.output_file = cli_out;
	const raster_picture& resumed = m_eval_gstate.m_best_pic;
	if ((cfg.graphics_mode == GraphicsMode::Antic4)
		!= (resumed.graphics_mode == GraphicsMode::Antic4))
		Error("Resume graphics mode does not match the saved raster program");
	if (ValidateRasterPicture(resumed) != E_RASTER_VALID)
		Error("Saved raster program or ANTIC 4 attribute state is invalid");
	return true;
}

bool RastaConverter::RunStructuredFixtureScreen(
	const std::string& csv_path, const std::string& profile_label)
{
	if (cfg.dual_mode || m_evaluators.empty()
		|| m_eval_gstate.m_best_pic.raster_lines.size()
			!= static_cast<std::size_t>(m_height))
		return false;
	std::ofstream csv(Utf8Path(csv_path), std::ios::out | std::ios::trunc);
	if (!csv)
		return false;
	csv << "profile,first_line,line_count,beam_width,diversity_per_state,"
		"alternate_count,feasible,baseline_score,structured_score,delta,"
		"replay_feasible,replay_score,source_vs_replay_delta\n";

	const std::size_t height = static_cast<std::size_t>(m_height);
	const std::size_t lineCounts[] = {1, 2, 4};
	const std::size_t widths[] = {16, 64, 256};
	const std::size_t diversities[] = {1, 2};
	const std::size_t alternateCounts[] = {1, 3, 7};

	Evaluator& evaluator = m_evaluators.front();
	raster_picture baseline = m_eval_gstate.m_best_pic;
	evaluator.RecachePicture(&baseline);
	std::vector<std::pair<std::size_t, std::size_t>> fixtures;
	StructuredBeamOptions discoveryOptions;
	discoveryOptions.width = 256;
	discoveryOptions.diversity_per_state = 2;
	discoveryOptions.repair_cost_per_pixel = 0.0;
	discoveryOptions.target_lifetime_spans = profile_label == "pal";
	for (std::size_t lineCount : lineCounts)
	{
		std::vector<std::size_t> feasibleStarts;
		for (std::size_t firstLine = 0; firstLine + lineCount <= height;
			++firstLine)
		{
			StructuredWindowResult replay;
			if (ExtractStructuredReplayWindow(baseline, firstLine, lineCount,
				discoveryOptions, replay))
				feasibleStarts.push_back(firstLine);
		}
		for (std::size_t sample = 1; sample <= 4 && !feasibleStarts.empty(); ++sample)
		{
			const std::size_t index = std::min(feasibleStarts.size() - 1,
				feasibleStarts.size() * sample / 5);
			const auto fixture = std::make_pair(feasibleStarts[index], lineCount);
			if (std::find(fixtures.begin(), fixtures.end(), fixture) == fixtures.end())
				fixtures.push_back(fixture);
		}
	}

	for (const auto& fixture : fixtures)
	{
		const std::size_t firstLine = fixture.first;
		const std::size_t lineCount = fixture.second;
		for (std::size_t width : widths)
		{
			for (std::size_t diversity : diversities)
			{
				for (std::size_t alternateCount : alternateCounts)
				{
					StructuredBeamOptions options;
					options.width = width;
					options.diversity_per_state = diversity;
					options.repair_cost_per_pixel = 0.0;
					options.target_lifetime_spans = profile_label == "pal";
					const Evaluator::StructuredWindowComparison comparison =
						evaluator.CompareStructuredSourceWindow(baseline,
							firstLine, lineCount, alternateCount, options);
					StructuredWindowResult replay;
					Evaluator::StructuredWindowComparison replayComparison;
					if (ExtractStructuredReplayWindow(baseline, firstLine,
						lineCount, options, replay))
					{
						replayComparison = evaluator.CompareStructuredWindow(
							baseline, firstLine, replay.lines);
					}
					csv << profile_label << ',' << firstLine << ',' << lineCount
						<< ',' << width << ',' << diversity << ','
						<< alternateCount << ',' << (comparison.feasible ? 1 : 0)
						<< ',' << comparison.baseline_score << ','
						<< comparison.structured_score << ','
						<< (comparison.feasible
							? static_cast<double>(comparison.structured_score)
								- static_cast<double>(comparison.baseline_score)
							: 0.0) << ',' << (replayComparison.feasible ? 1 : 0)
						<< ',' << replayComparison.structured_score << ','
						<< (comparison.feasible && replayComparison.feasible
							? static_cast<double>(comparison.structured_score)
								- static_cast<double>(replayComparison.structured_score)
							: 0.0) << '\n';
				}
			}
		}
	}
	return static_cast<bool>(csv);
}

bool RastaConverter::RunPhase7RetainedWindowScreen(
	const std::string& csvPath, const std::string& profileLabel)
{
	if (!cfg.dual_mode || m_evaluators.empty()
		|| m_eval_gstate.m_best_pic.raster_lines.size() != static_cast<size_t>(m_height)
		|| m_best_pic_B.raster_lines.size() != static_cast<size_t>(m_height))
		return false;
	std::ofstream csv(Utf8Path(csvPath), std::ios::out | std::ios::trunc);
	if (!csv) return false;
	csv << "profile,first_line,line_count,slots,choices,joint_feasible,"
		"alternating_feasible,joint_legal_a,joint_legal_b,alternating_legal_a,"
		"alternating_legal_b,baseline_visual,baseline_flicker,baseline_total,"
		"joint_visual,joint_flicker,joint_total,alternating_visual,"
		"alternating_flicker,alternating_total,joint_minus_alternating\n";

	Evaluator& evaluator = m_evaluators.front();
	raster_picture baselineA = m_eval_gstate.m_best_pic;
	raster_picture baselineB = m_best_pic_B;
	StructuredBeamOptions options;
	options.width = 64;
	options.diversity_per_state = 2;
	options.repair_cost_per_pixel = 0.0;
	const size_t lineCounts[] = {1, 2, 4};
	size_t emitted = 0;
	for (size_t lineCount : lineCounts)
	{
		size_t emittedForCount = 0;
		for (size_t firstLine = 0;
			firstLine + lineCount <= static_cast<size_t>(m_height); ++firstLine)
		{
			if (emittedForCount >= 2) break;
			StructuredPairedWindowProblem problem;
			if (!ExtractStructuredPairedWindowProblem(baselineA, baselineB,
					firstLine, lineCount, 1, options, problem)
				|| !evaluator.PopulateDualStructuredWindowCosts(baselineA, baselineB,
					firstLine, problem, options))
				continue;
			const StructuredPairedWindowResult joint =
				SearchStructuredPairedWindowBeam(problem.incoming_a,
					problem.incoming_b, problem.lines, options,
					&problem.required_outgoing_a, &problem.required_outgoing_b);
			Evaluator::DualStructuredWindowComparison jointComparison;
			if (joint.feasible)
				jointComparison = evaluator.CompareDualStructuredWindow(
					baselineA, baselineB, firstLine, joint);

			auto selected = problem.lines;
			for (auto& line : selected)
				for (auto& segment : line)
					segment.values = {segment.values.front()};
			StructuredPairedWindowResult alternating;
			for (int iteration = 0; iteration < 4; ++iteration)
				for (int focus = 0; focus < 2; ++focus)
				{
					auto coordinate = problem.lines;
					for (size_t line = 0; line < coordinate.size(); ++line)
						for (size_t slot = 0; slot < coordinate[line].size(); ++slot)
						{
							const auto fixed = selected[line][slot].values.front();
							auto& values = coordinate[line][slot].values;
							values.erase(std::remove_if(values.begin(), values.end(),
								[focus, fixed](const StructuredPairedSegmentValue& value) {
									return focus == 0 ? value.value_b != fixed.value_b
										: value.value_a != fixed.value_a;
								}), values.end());
						}
					alternating = SearchStructuredPairedWindowBeam(problem.incoming_a,
						problem.incoming_b, coordinate, options,
						&problem.required_outgoing_a, &problem.required_outgoing_b);
					if (!alternating.feasible) break;
					for (size_t line = 0; line < selected.size(); ++line)
					{
						size_t cursorA = 0;
						size_t cursorB = 0;
						for (size_t slot = 0; slot < selected[line].size(); ++slot)
						{
							const auto& segment = problem.lines[line][slot];
							const unsigned char valueA = segment.write_a
								? alternating.frame_a.transitions[line][cursorA++].value
								: segment.values.front().value_a;
							const unsigned char valueB = segment.write_b
								? alternating.frame_b.transitions[line][cursorB++].value
								: segment.values.front().value_b;
							for (const auto& value : problem.lines[line][slot].values)
								if (value.value_a == valueA && value.value_b == valueB)
									selected[line][slot].values = {value};
						}
					}
				}
			Evaluator::DualStructuredWindowComparison alternatingComparison;
			if (alternating.feasible)
				alternatingComparison = evaluator.CompareDualStructuredWindow(
					baselineA, baselineB, firstLine, alternating);
			size_t slots = 0;
			size_t choices = 1;
			for (const auto& line : problem.lines)
				for (const auto& segment : line)
				{
					++slots;
					choices *= segment.values.size();
				}
			const DualFrameScore baseline = jointComparison.feasible
				? jointComparison.baseline : alternatingComparison.baseline;
			csv << profileLabel << ',' << firstLine << ',' << lineCount << ','
				<< slots << ',' << choices << ',' << jointComparison.feasible << ','
				<< alternatingComparison.feasible << ',' << jointComparison.legal_a << ','
				<< jointComparison.legal_b << ',' << alternatingComparison.legal_a << ','
				<< alternatingComparison.legal_b << ',' << baseline.visual << ','
				<< baseline.flicker << ',' << baseline.total << ','
				<< jointComparison.structured.visual << ','
				<< jointComparison.structured.flicker << ','
				<< jointComparison.structured.total << ','
				<< alternatingComparison.structured.visual << ','
				<< alternatingComparison.structured.flicker << ','
				<< alternatingComparison.structured.total << ','
				<< (jointComparison.structured.total
					- alternatingComparison.structured.total) << '\n';
			++emitted;
			++emittedForCount;
		}
	}
	return emitted > 0 && static_cast<bool>(csv);
}


void RastaConverter::reconfigureAcceptanceHistory()
{
	bool optimizer_changed = cfg.resume_optimizer_changed;
	bool metric_changed = cfg.resume_distance_changed || cfg.resume_predistance_changed || cfg.resume_dither_changed || cfg.resume_objective_changed;
	bool solutions_changed = cfg.resume_solutions_changed;
	if (!optimizer_changed && !solutions_changed && !metric_changed) {
		m_needs_history_reconfigure = false;
		return;
	}

	if (m_evaluators.empty()) {
		cfg.resume_optimizer_changed = false;
		cfg.resume_solutions_changed = false;
		cfg.resume_distance_changed = false;
		cfg.resume_predistance_changed = false;
		cfg.resume_dither_changed = false;
		cfg.resume_objective_changed = false;
		m_needs_history_reconfigure = false;
		return;
	}

	std::vector<double>& history = m_eval_gstate.m_previous_results;
	if (history.empty()) history.push_back(m_eval_gstate.m_best_result);

	size_t target_len = static_cast<size_t>(std::max(1, solutions));
	double previous_max = history.empty() ? m_eval_gstate.m_best_result.load(std::memory_order_relaxed) : *std::max_element(history.begin(), history.end());

	auto recompute_single_cost = [this]() -> double {
		Evaluator& ev = m_evaluators.front();
		const std::shared_ptr<const EvalGlobalState::PublishedBestSnapshot> snapshot =
			std::atomic_load_explicit(&m_eval_gstate.m_best_snapshot,
				std::memory_order_acquire);
		raster_picture pic = snapshot ? snapshot->picture : m_eval_gstate.m_best_pic;
		std::vector<const line_cache_result*> res(m_height, nullptr);
		ev.RecachePicture(&pic);
		double cost = (double)ev.EvaluateSingle(&pic, res.data());
		return cost;
	};

	auto recompute_dual_cost = [this]() -> double {
		Evaluator& ev = m_evaluators.front();
		raster_picture picA = m_eval_gstate.m_best_pic;
		std::vector<const line_cache_result*> res(m_height, nullptr);
		std::vector<const unsigned char*> otherRows((size_t)m_height, nullptr);
		bool anyRow = false;
		for (int y = 0; y < m_height; ++y) {
			if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
				otherRows[y] = m_created_picture_B[y].data();
				anyRow = true;
			}
		}
		ev.RecachePicture(&picA);
		double cost;
		if (anyRow && m_dual_tables_ready) {
			cost = (double)ev.ExecuteRasterProgramDual(&picA, res.data(), otherRows, /*mutateB*/false);
		} else {
			cost = (double)ev.ExecuteRasterProgram(&picA, res.data());
		}
		return cost;
	};

	double baseline_cost = previous_max;
	if (metric_changed) {
		baseline_cost = cfg.dual_mode ? recompute_dual_cost() : recompute_single_cost();
		m_eval_gstate.m_best_result = baseline_cost;
	}

	history.assign(target_len, baseline_cost);
	m_eval_gstate.m_previous_results_index = 0;
	m_eval_gstate.m_cost_max = baseline_cost;
	m_eval_gstate.m_current_cost = baseline_cost;
	m_eval_gstate.m_N = static_cast<int>(target_len);

	cfg.resume_optimizer_changed = false;
	cfg.resume_solutions_changed = false;
	cfg.resume_distance_changed = false;
	cfg.resume_predistance_changed = false;
	cfg.resume_dither_changed = false;
	cfg.resume_objective_changed = false;
	m_needs_history_reconfigure = false;
}

void RastaConverter::SaveRasterProgram(string name, raster_picture *pic)
{
    Message("Saving Raster Program");

    {
        std::ofstream iniOut(Utf8Path(name + ".ini"), std::ios::out | std::ios::trunc);
        if (!iniOut)
            Error("Error saving Raster Program");

        iniOut << "; ---------------------------------- \n";
        iniOut << "; RastaConverter by Ilmenit version " << program_version << '\n';
        iniOut << "; ---------------------------------- \n";
        iniOut << "\n; Initial values \n";

        iniOut << std::uppercase << std::hex;
        for (size_t y = 0; y < sizeof(pic->mem_regs_init); ++y)
        {
			if (pic->graphics_mode == GraphicsMode::AnticE && y == E_COLOR3)
				continue;
            iniOut << "\tlda #$" << std::setw(2) << std::setfill('0') << static_cast<int>(pic->mem_regs_init[y]) << '\n';
            iniOut << "\tsta " << mem_regs_names[y] << '\n';
        }
        iniOut << std::nouppercase << std::dec;

        iniOut << "\tlda #$0\n";
        iniOut << "\ttax\n";
        iniOut << "\ttay\n";

		if (pic->graphics_mode == GraphicsMode::AnticE)
		{
			iniOut << "\n; Set proper count of wsyncs \n";
			iniOut << "\n\t:2 sta wsync\n";
		}

        if (!iniOut)
            Error("Error finalizing Raster Program ini file");
    }

    {
        std::ofstream heightOut(Utf8Path(name + ".h"), std::ios::out | std::ios::trunc);
        if (!heightOut)
            Error("Error saving picture height header file");
        heightOut << "; Set proper picture height\n";
        heightOut << "PIC_HEIGHT = " << m_height << '\n';
        if (!heightOut)
            Error("Error finalizing picture height header file");
    }

    std::ofstream asmOut(Utf8Path(name), std::ios::out | std::ios::trunc);
    if (!asmOut)
        Error("Error saving DLI handler");

    asmOut << "; ---------------------------------- \n";
    asmOut << "; RastaConverter by Ilmenit v." << program_version << '\n';
    asmOut << "; InputName: " << cfg.input_file << '\n';
    asmOut << "; CmdLine: " << cfg.command_line << '\n';
	if (!details_mask.Empty())
	{
		asmOut << "; Details Source Hash: " << details_mask.SourceHash() << '\n';
		asmOut << "; Details Effective Hash: " << details_mask.EffectiveHash() << '\n';
		asmOut << "; Details Mode: " << cfg.details_mode << '\n';
		asmOut << "; Details Score: " << (cfg.details_score ? "on" : "off") << '\n';
		asmOut << "; Details Allocation: " << (cfg.details_allocate ? "on" : "off") << '\n';
		asmOut << "; Details Global Period: " << cfg.details_global_period << '\n';
	}
	asmOut << "; Mask Edited: " << (m_mask_edited ? "yes" : "no") << '\n';
	asmOut << "; Destination Edited: "
		<< (m_destination_edited ? "yes" : "no") << '\n';
	asmOut << "; Target Hash: " << m_target_hash << '\n';
	asmOut << "; Snapshots: " << m_snapshot_count << '\n';
    asmOut << "; Evaluations: " << static_cast<unsigned long long>(m_eval_gstate.m_evaluations) << '\n';
    asmOut << "; Score: " << NormalizeScore(m_eval_gstate.m_best_result) << '\n';
	if (!cfg.dual_mode && !m_evaluators.empty())
	{
		const std::streamsize scorePrecision = asmOut.precision();
		asmOut << std::setprecision(17)
			<< "; Unweighted Source OKLab Mean: "
			<< UnweightedSourceOklabMean(pic) << '\n'
			<< std::setprecision(scorePrecision);
	}
    const unsigned long long lockSamples = m_eval_gstate.m_single_state_lock_samples.load(std::memory_order_relaxed);
    const unsigned long long copySamples = m_eval_gstate.m_single_copy_samples.load(std::memory_order_relaxed);
	asmOut << "; Optimizer Accepted: " << m_eval_gstate.m_single_accepted.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Optimizer Global Improvements: " << m_eval_gstate.m_single_global_improvements.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Optimizer Migrations: " << m_eval_gstate.m_single_migrations.load(std::memory_order_relaxed) << '\n';
    asmOut << "; State Lock Samples: " << lockSamples << '\n';
    asmOut << "; State Lock Mean Wait Ns: " << (lockSamples ? m_eval_gstate.m_single_state_lock_wait_ns.load(std::memory_order_relaxed) / lockSamples : 0ULL) << '\n';
    asmOut << "; State Lock Mean Hold Ns: " << (lockSamples ? m_eval_gstate.m_single_state_lock_hold_ns.load(std::memory_order_relaxed) / lockSamples : 0ULL) << '\n';
    asmOut << "; Accepted Copy Samples: " << copySamples << '\n';
    asmOut << "; Accepted Copy Mean Ns: " << (copySamples ? m_eval_gstate.m_single_copy_ns.load(std::memory_order_relaxed) / copySamples : 0ULL) << '\n';
	const unsigned long long publicationCopyEvents = m_eval_gstate.m_publication_copy_events.load(std::memory_order_relaxed);
	const unsigned long long publicationCopyNs = m_eval_gstate.m_publication_copy_ns.load(std::memory_order_relaxed);
	const unsigned long long migrationCopyEvents = m_eval_gstate.m_migration_copy_events.load(std::memory_order_relaxed);
	const unsigned long long migrationCopyNs = m_eval_gstate.m_migration_copy_ns.load(std::memory_order_relaxed);
	asmOut << "; Publication Copy Events: " << publicationCopyEvents << '\n';
	asmOut << "; Publication Copy Total Ns: " << publicationCopyNs << '\n';
	asmOut << "; Publication Copy Mean Ns: " << (publicationCopyEvents ? publicationCopyNs / publicationCopyEvents : 0ULL) << '\n';
	asmOut << "; Migration Copy Events: " << migrationCopyEvents << '\n';
	asmOut << "; Migration Copy Total Ns: " << migrationCopyNs << '\n';
	asmOut << "; Migration Copy Mean Ns: " << (migrationCopyEvents ? migrationCopyNs / migrationCopyEvents : 0ULL) << '\n';
	asmOut << "; Migration Lines Copied: " << m_eval_gstate.m_migration_lines_copied.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Migration Lines Reused: " << m_eval_gstate.m_migration_lines_reused.load(std::memory_order_relaxed) << '\n';
	const unsigned long long improvementEvents = m_eval_gstate.m_improvement_events.load(std::memory_order_relaxed);
	const double improvementTotal = m_eval_gstate.m_improvement_total.load(std::memory_order_relaxed);
	const std::streamsize metadataPrecision = asmOut.precision();
	asmOut << std::setprecision(17);
	asmOut << "; Improvement Events: " << improvementEvents << '\n';
	asmOut << "; Improvement Total: " << improvementTotal << '\n';
	asmOut << "; Improvement Mean: " << (improvementEvents ?
		improvementTotal / static_cast<double>(improvementEvents) : 0.0) << '\n';
	asmOut << "; Improvement Max: " << m_eval_gstate.m_improvement_max.load(std::memory_order_relaxed) << '\n';
	asmOut << std::setprecision(metadataPrecision);
    asmOut << "; Cache Partial Clears: " << m_eval_gstate.m_single_cache_partial_clears.load(std::memory_order_relaxed) << '\n';
    asmOut << "; Cache Full Clears: " << m_eval_gstate.m_single_cache_full_clears.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Candidate Full Copies: " << m_eval_gstate.m_single_candidate_full_copies.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Undo Candidates: " << m_eval_gstate.m_single_undo_candidates.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Undo Line Snapshots: " << m_eval_gstate.m_single_undo_line_snapshots.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Undo Restores: " << m_eval_gstate.m_single_undo_restores.load(std::memory_order_relaxed) << '\n';
	const unsigned long long cacheLookups = m_eval_gstate.m_cache_lookups.load(std::memory_order_relaxed);
	const unsigned long long cacheEvaluations = m_eval_gstate.m_cache_evaluations.load(std::memory_order_relaxed);
	const unsigned long long lruUpdates = m_eval_gstate.m_lru_updates.load(std::memory_order_relaxed);
	asmOut << "; Cache Lookups: " << cacheLookups << '\n';
	asmOut << "; Cache Hits: " << m_eval_gstate.m_cache_hits.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Misses: " << m_eval_gstate.m_cache_misses.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Lookup Probes: " << m_eval_gstate.m_cache_lookup_probes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Mean Lookup Probes: " << (cacheLookups ?
		static_cast<double>(m_eval_gstate.m_cache_lookup_probes.load(std::memory_order_relaxed)) / cacheLookups : 0.0) << '\n';
	asmOut << "; Cache Max Lookup Probes: " << m_eval_gstate.m_cache_max_lookup_probes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Inserts: " << m_eval_gstate.m_cache_inserts.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Hash Blocks: " << m_eval_gstate.m_cache_hash_blocks.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Entry Bytes: " << m_eval_gstate.m_cache_entry_bytes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Hash Block Bytes: " << m_eval_gstate.m_cache_hash_block_bytes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Color Row Bytes: " << m_eval_gstate.m_cache_color_row_bytes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Target Row Bytes: " << m_eval_gstate.m_cache_target_row_bytes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Insn Cache Hash Block Bytes: " << m_eval_gstate.m_insn_cache_hash_block_bytes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Insn Cache Data Bytes: " << m_eval_gstate.m_insn_cache_data_bytes.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Evaluations: " << cacheEvaluations << '\n';
	asmOut << "; Cache Recomputed Lines: " << m_eval_gstate.m_cache_recomputed_lines.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Mean Recomputed Lines: " << (cacheEvaluations ?
		static_cast<double>(m_eval_gstate.m_cache_recomputed_lines.load(std::memory_order_relaxed)) / cacheEvaluations : 0.0) << '\n';
	if (pic->graphics_mode == GraphicsMode::Antic4)
	{
		const unsigned long long attributeEvaluations =
			m_eval_gstate.m_antic4_attribute_cache_evaluations.load(
				std::memory_order_relaxed);
		const unsigned long long attributeLines =
			m_eval_gstate.m_antic4_attribute_recomputed_lines.load(
				std::memory_order_relaxed);
		asmOut << "; ANTIC4 Attribute Cache Evaluations: "
			<< attributeEvaluations << '\n';
		asmOut << "; ANTIC4 Attribute Recomputed Lines: "
			<< attributeLines << '\n';
		asmOut << "; ANTIC4 Attribute Mean Recomputed Lines: "
			<< (attributeEvaluations
				? static_cast<double>(attributeLines) / attributeEvaluations
				: 0.0) << '\n';
	}
	asmOut << "; Cache Max Recomputed Lines: " << m_eval_gstate.m_cache_max_recomputed_lines.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Propagation Span: " << m_eval_gstate.m_cache_propagation_span.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache Mean Propagation Span: " << (cacheEvaluations ?
		static_cast<double>(m_eval_gstate.m_cache_propagation_span.load(std::memory_order_relaxed)) / cacheEvaluations : 0.0) << '\n';
	asmOut << "; Cache Max Propagation Span: " << m_eval_gstate.m_cache_max_propagation_span.load(std::memory_order_relaxed) << '\n';
	asmOut << "; Cache PMG Restarts: " << m_eval_gstate.m_cache_pmg_restarts.load(std::memory_order_relaxed) << '\n';
	asmOut << "; LRU Updates: " << lruUpdates << '\n';
	asmOut << "; LRU Search Steps: " << m_eval_gstate.m_lru_search_steps.load(std::memory_order_relaxed) << '\n';
	asmOut << "; LRU Mean Search Steps: " << (lruUpdates ?
		static_cast<double>(m_eval_gstate.m_lru_search_steps.load(std::memory_order_relaxed)) / lruUpdates : 0.0) << '\n';
	unsigned long long timingProgramCycles = 0;
	unsigned timingMaxProgramCycles = 0;
	unsigned timingLinesAtLimit = 0;
	for (size_t y = 0; y < pic->raster_lines.size(); ++y)
	{
		const raster_line& line = pic->raster_lines[y];
		const RasterLineSchedule schedule =
			GetRasterLineSchedule(pic->graphics_mode, static_cast<int>(y),
				static_cast<int>(pic->raster_lines.size()));
		assert(line.cycles >= 0 && line.cycles <= schedule.optimizer_cycle_limit);
		assert((line.cycles & 1) == 0);
		timingProgramCycles += static_cast<unsigned>(line.cycles);
		timingMaxProgramCycles = std::max(timingMaxProgramCycles, static_cast<unsigned>(line.cycles));
		if (line.cycles == schedule.optimizer_cycle_limit)
			++timingLinesAtLimit;
	}
	if (pic->graphics_mode == GraphicsMode::AnticE)
	{
		asmOut << "; Timing CPU Slots: " << raster_cpu_slots << '\n';
		asmOut << "; Timing Program Cycle Limit: " << raster_program_cycle_limit << '\n';
		asmOut << "; Timing Tail Cycles: " << raster_tail_cycles << '\n';
	}
	else
	{
		asmOut << "; Graphics Mode: ANTIC 4\n";
		asmOut << "; Timing CPU Slots: per-line 11/13/52/53\n";
		asmOut << "; Timing Program Cycle Limit: per-line 6/8/48/48/44\n";
	}
	asmOut << "; Timing Mean Program Cycles: " << (pic->raster_lines.empty() ? 0.0 :
		static_cast<double>(timingProgramCycles) / pic->raster_lines.size()) << '\n';
	asmOut << "; Timing Max Program Cycles: " << timingMaxProgramCycles << '\n';
	asmOut << "; Timing Lines At Limit: " << timingLinesAtLimit << '\n';
	for (unsigned y = 0; y < m_eval_gstate.m_cache_hits_by_line.size(); ++y)
	{
		asmOut << "; Cache Line " << y << " Hits: " << m_eval_gstate.m_cache_hits_by_line[y]
			<< " Misses: " << m_eval_gstate.m_cache_misses_by_line[y] << '\n';
	}
	const std::streamsize mutationPrecision = asmOut.precision();
	asmOut << std::setprecision(17);
	for (int i = 0; i < E_MUTATION_MAX; ++i)
	{
		if (pic->graphics_mode != GraphicsMode::Antic4
			&& i == E_MUTATION_TOGGLE_ANTIC4_ATTRIBUTE)
			continue;
		const unsigned long long improving =
			m_eval_gstate.m_mutation_improving[i].load(std::memory_order_relaxed);
		const double improvementCredit =
			m_eval_gstate.m_mutation_improvement_credit_total[i].load(std::memory_order_relaxed);
		asmOut << "; Mutation " << mutation_names[i]
			<< " Attempted: " << m_eval_gstate.m_mutation_attempted[i].load(std::memory_order_relaxed)
			<< " Applied: " << m_eval_gstate.m_mutation_applied[i].load(std::memory_order_relaxed)
			<< " Accepted: " << m_eval_gstate.m_mutation_accepted[i].load(std::memory_order_relaxed)
			<< " Improving: " << improving
			<< " Improvement Credit Total: " << improvementCredit
			<< " Improvement Credit Mean: " << (improving ?
				improvementCredit / static_cast<double>(improving) : 0.0)
			<< " Improvement Credit Max: "
			<< m_eval_gstate.m_mutation_improvement_credit_max[i].load(std::memory_order_relaxed)
			<< '\n';
	}
	asmOut << std::setprecision(mutationPrecision);
    asmOut << "; ---------------------------------- \n";

	if (pic->graphics_mode == GraphicsMode::AnticE)
	{
		asmOut << "; Proper offset \n";
		asmOut << "\tnop\n\tnop\n\tnop\n\tnop\n\tcmp byt2;\n";
	}

    int h = FreeImage_GetHeight(input_bitmap);

    asmOut << std::uppercase << std::hex;
    for (int y = 0; y < h; ++y)
    {
        asmOut << "line" << y << '\n';
		const RasterLineSchedule schedule =
			GetRasterLineSchedule(pic->graphics_mode, y, h);
        size_t prog_len = pic->raster_lines[y].instructions.size();
        for (size_t i = 0; i < prog_len; ++i)
        {
            SRasterInstruction instr = pic->raster_lines[y].instructions[i];
            bool save_target = false;
            bool save_value = false;
            asmOut << "\t";
            switch (instr.loose.instruction)
            {
            case E_RASTER_LDA:
                asmOut << "lda ";
                save_value = true;
                break;
            case E_RASTER_LDX:
                asmOut << "ldx ";
                save_value = true;
                break;
            case E_RASTER_LDY:
                asmOut << "ldy ";
                save_value = true;
                break;
            case E_RASTER_NOP:
                asmOut << "nop ";
                break;
            case E_RASTER_STA:
                asmOut << "sta ";
                save_target = true;
                break;
            case E_RASTER_STX:
                asmOut << "stx ";
                save_target = true;
                break;
            case E_RASTER_STY:
                asmOut << "sty ";
                save_target = true;
                break;
            default:
                Error("Unknown instruction!");
            }
            if (save_value)
            {
        asmOut << std::uppercase << std::hex << "#$" << std::setw(2) << std::setfill('0') << static_cast<int>(instr.loose.value)
               << std::nouppercase << std::dec
               << " ; " << static_cast<int>(instr.loose.value)
               << " (spr=" << static_cast<int>(instr.loose.value - 48) << ")";
            }
            else if (save_target)
            {
                if (instr.loose.target >= E_TARGET_MAX)
                    Error("Unknown target in instruction!");
                asmOut << mem_regs_names[instr.loose.target];
            }
            asmOut << '\n';
        }
        asmOut << std::dec;
        for (int cycle = pic->raster_lines[y].cycles;
			cycle < schedule.optimizer_cycle_limit; cycle += 2)
        {
            asmOut << "\tnop ; filler\n";
        }
		// The suffix has to spend exactly fixed_suffix_cycles: the kernel never
		// resynchronizes with WSYNC, so a line that is one cycle short or long
		// shifts every remaining line of the frame.
		if (pic->graphics_mode == GraphicsMode::AnticE)
		{
			assert(schedule.fixed_suffix_cycles == 3);
			asmOut << "\tcmp byt2; on zero page so 3 cycles\n";
		}
		else if (schedule.chbase_transition)
		{
			// 3 + 2 + 4: the store writes CHBASE on the line's last CPU slot,
			// ANTIC cycle 104. The final character-data fetch of this line
			// happens at 105 and still sees the old charset; the queued update
			// becomes effective at 106, before the next line fetches anything.
			assert(schedule.fixed_suffix_cycles == 9);
			const int charset = y / 24 + 1;
			asmOut << "; ANTIC4_FIXED_CHBASE_BEGIN\n";
			asmOut << "\tbit byt2 ; 3-cycle CHBASE safety delay\n";
			asmOut << "\tlda #>charset_" << charset << '\n';
			asmOut << "\tsta CHBASE\n";
			asmOut << "; ANTIC4_FIXED_CHBASE_END\n";
		}
		else if (schedule.fixed_suffix_cycles == 5)
		{
			asmOut << "\tbit byt2 ; ANTIC4_FIXED_LINE_END, 3 cycles\n";
			asmOut << "\tnop ; ANTIC4_FIXED_LINE_END, 2 cycles\n";
		}
		else
		{
			assert(schedule.fixed_suffix_cycles == 4);
			asmOut << "\tbit $ffff ; ANTIC4_FIXED_LINE_END, 4 cycles\n";
		}
        asmOut << std::uppercase << std::hex;
    }
    asmOut << std::nouppercase << std::dec;
    asmOut << "; ---------------------------------- \n";

    if (!asmOut)
        Error("Error finalizing DLI handler");
}

double RastaConverter::NormalizeScore(double raw_score)
{
	return raw_score / (((double)m_width*(double)m_height)*(MAX_COLOR_DISTANCE/10000));
}

double RastaConverter::UnweightedSourceOklabMean(raster_picture* pic)
{
	if (pic == nullptr || !m_reporting_evaluator || m_width <= 0 || m_height <= 0)
		return 0.0;
	constexpr double kOklabEnergyScale = 200000.0;
	const distance_accum_t total =
		m_reporting_evaluator->EvaluateUnweightedSource(pic);
	return static_cast<double>(total)
		/ (static_cast<double>(m_width) * m_height * kOklabEnergyScale);
}
