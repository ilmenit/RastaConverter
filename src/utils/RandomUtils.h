#ifndef RANDOM_UTILS_H
#define RANDOM_UTILS_H

#include "../mt19937int.h"

/**
 * Generate a random integer in a range
 * 
 * @param range Upper bound (exclusive)
 * @return Random integer between 0 and range-1
 */
inline int random(int range)
{
    if (range == 0)
        return 0;
    // Mersenne Twister is used for longer period
    return genrand_int32() % range;
}

#endif // RANDOM_UTILS_H