#ifndef MT_RANDOM
#define MT_RANDOM

void init_genrand(unsigned long seed);
unsigned long genrand_int32();
double genrand_real2(void);

#endif