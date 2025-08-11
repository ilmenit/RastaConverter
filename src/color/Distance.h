#ifndef DISTANCE_H
#define DISTANCE_H

#include "rgb.h"

#define MAX_COLOR_DISTANCE (255*255*3)
#define DISTANCE_MAX 0xffffffff

typedef long long distance_accum_t;
typedef unsigned int distance_t;
typedef distance_t (fn_rgb_distance)(const rgb &col1, const rgb &col2);
typedef fn_rgb_distance *f_rgb_distance;

distance_t RGBEuclidianDistance(const rgb &col1, const rgb &col2);
distance_t RGBCIEDE2000Distance(const rgb &col1, const rgb &col2);
distance_t RGByuvDistance(const rgb &col1, const rgb &col2);
distance_t RGBCIE94Distance(const rgb &col1, const rgb &col2);

// Expose RGB -> Lab conversion for users of CIE distances and precomputations
void RGB2LAB(const rgb &c, Lab &result);


#endif
