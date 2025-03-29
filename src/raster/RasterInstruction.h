#ifndef RASTERINSTRUCTION_H
#define RASTERINSTRUCTION_H

union SRasterInstruction {
	struct {
		/*e_raster_instruction*/ unsigned short instruction;
		/*e_target*/ unsigned char target;
		unsigned char value;
	} loose;

	unsigned int packed;

	bool operator==(const SRasterInstruction& other) const
	{
		return packed == other.packed;
	}

	size_t hash() const
	{
		return packed;
	}
};

#endif
