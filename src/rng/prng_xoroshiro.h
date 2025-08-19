#ifndef PRNG_XOROSHIRO_H
#define PRNG_XOROSHIRO_H

// Fast PRNG (xoroshiro128++) implementation exposing the legacy interface
// void init_genrand(unsigned long seed);
// unsigned long genrand_int32();
// double genrand_real2(void);

void init_genrand(unsigned long seed);
unsigned long genrand_int32();
double genrand_real2(void);

#endif


