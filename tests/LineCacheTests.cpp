#include "Program.h"
#include "LineCache.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void Require(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

void TestPackedTargetRow(std::size_t width)
{
	std::vector<unsigned char> source(width);
	for (std::size_t x = 0; x < width; ++x)
		source[x] = static_cast<unsigned char>(x & 7);

	std::vector<unsigned char> packed(line_cache_result::packed_target_bytes(width));
	line_cache_result::pack_target_row(packed.data(), source.data(), width);

	line_cache_result result{};
	result.packed_target_row = packed.data();
	std::vector<unsigned char> decoded(width, 0xff);
	result.copy_target_row(decoded.data(), width);
	Require(decoded == source, "packed target row must round-trip exactly");
}

void TestReclaimableLineArena()
{
	linear_allocator::statistics stats;
	linear_allocator arena(65536, &stats);
	line_cache cache;

	line_cache_key key{};
	key.entry_state.reg_a = 7;
	const uint32_t hash = key.hash();
	line_cache_result& inserted = cache.insert(key, hash, arena);
	inserted.line_error = 123;
	inserted.color_row = static_cast<unsigned char *>(
		arena.allocate(160, linear_allocator::LINE_CACHE_COLOR_ROW));

	Require(cache.find(key, hash) == &inserted, "inserted line result must be found");
	Require(arena.size() > 0, "line arena must own resident storage after insert");
	Require(stats.resident_bytes == arena.size(), "shared resident accounting must include line arena");
	const size_t allocated_entries = stats.allocated_by_type[linear_allocator::LINE_CACHE_ENTRY];

	cache.clear();
	arena.clear();
	Require(cache.find(key, hash) == NULL, "cleared line result must not remain reachable");
	Require(arena.size() == 0, "clearing one line must reclaim its arena");
	Require(stats.resident_bytes == 0, "resident accounting must fall when a line is reclaimed");
	Require(stats.allocated_by_type[linear_allocator::LINE_CACHE_ENTRY] == allocated_entries,
		"allocation attribution must remain cumulative after reclaim");
}

void TestAntic4AttributeRowIsPartOfKey()
{
	line_cache_key normal{};
	line_cache_key alternate = normal;
	alternate.antic4_attribute_row = uint64_t{1} << 39;
	Require(!(normal == alternate),
		"ANTIC 4 attribute-row changes must not reuse a line result");
	Require(normal.hash() != alternate.hash(),
		"ANTIC 4 attribute-row changes should perturb the cache hash");
}
}

int main()
{
	TestPackedTargetRow(160);
	TestPackedTargetRow(159);
	TestReclaimableLineArena();
	TestAntic4AttributeRowIsPartOfKey();
	std::cout << "LineCache tests passed\n";
	return 0;
}
