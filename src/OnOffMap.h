#ifndef ONOFFMAP_H
#define ONOFFMAP_H

#include "raster/Program.h"

/**
 * Structure to store which registers are enabled for each scanline
 */
struct OnOffMap
{
    bool on_off[240][E_TARGET_MAX]; // global for speed-up
};

#endif // ONOFFMAP_H