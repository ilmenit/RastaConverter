#include <stdint.h>
#include "prng_xoroshiro.h"

// xoroshiro128++ by David Blackman and Sebastiano Vigna
// Public domain: see http://vigna.di.unimi.it

static uint64_t s_state[2] = { 0x9E3779B97F4A7C15ULL, 0xBF58476D1CE4E5B9ULL };

static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

// SplitMix64 for seeding
static inline uint64_t splitmix64(uint64_t *x) {
	uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

void init_genrand(unsigned long seed)
{
	uint64_t sm = (uint64_t)seed;
	// Ensure non-zero state
	if (sm == 0) sm = 1;
	s_state[0] = splitmix64(&sm);
	s_state[1] = splitmix64(&sm);
	// Avoid all-zero state
	if ((s_state[0] | s_state[1]) == 0) {
		s_state[1] = 1;
	}
}

static inline uint64_t next_u64()
{
	const uint64_t s0 = s_state[0];
	uint64_t s1 = s_state[1];
	const uint64_t result = rotl(s0 + s1, 17) + s0; // xoroshiro128++ output

	s1 ^= s0;
	s_state[0] = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
	s_state[1] = rotl(s1, 28); // c
	return result;
}

unsigned long genrand_int32()
{
	// Return lower 32 bits (unsigned long is 32-bit on Windows)
	return (unsigned long)(next_u64() & 0xFFFFFFFFu);
}

double genrand_real2(void)
{
	// [0,1) with 53-bit resolution using two draws
	uint64_t a = next_u64() >> 11; // 53 bits
	return (double)a * (1.0 / 9007199254740992.0); // 1/2^53
}


