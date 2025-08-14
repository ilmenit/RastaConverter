#ifndef TARGETPICTURE_H
#define TARGETPICTURE_H

#include <string>
#include "config/config.h"
#include "color/Distance.h"

struct rgb;

extern rgb atari_palette[128]; // 128 colors in mode 15!
extern f_rgb_distance distance_function;

bool LoadAtariPalette(const std::string& filename);
unsigned char FindAtariColorIndex(const rgb &col);
void SetDistanceFunction(e_distance_function dst);

#endif


