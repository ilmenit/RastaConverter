#include <float.h>
#include "TargetPicture.h"
#include "rgb.h"
#include "Distance.h"

using namespace std;

rgb atari_palette[128]; // 128 colors in mode 15!
f_rgb_distance distance_function;

bool LoadAtariPalette(const std::string& filename)
{
	size_t i;
	rgb col;
	col.a=0;
	FILE *fp=fopen(filename.c_str(),"rb");
	if (!fp)
	{
		fp=fopen((string("Palettes/")+filename).c_str(),"rb");
		if (!fp)
		{
			fp=fopen((string("Palettes/")+filename+string(".act")).c_str(),"rb");
			if (!fp)
			{
				fp=fopen((string("Palettes/")+filename+string(".pal")).c_str(),"rb");
				if (!fp)
					return false;
			}
		}
	}
	for (i=0;i<256;++i)
	{
		col.r=fgetc(fp);
		col.g=fgetc(fp);
		col.b=fgetc(fp);
		// limit it to 128 colors!
		// use every second color
		if (i%2==0)
			atari_palette[i >> 1] = col;
	}
	fclose(fp);
	return true;
}

void SetDistanceFunction(e_distance_function dst)
{
	//////////////////////////////////////////////////////////////////////////
	// Set color distance

	switch (dst)
	{
	case E_DISTANCE_EUCLID:
		distance_function = RGBEuclidianDistance;
		break;
	case E_DISTANCE_CIEDE:
		distance_function = RGBCIEDE2000Distance;
		break;
	case E_DISTANCE_CIE94:
		distance_function = RGBCIE94Distance;
		break;
	case E_DISTANCE_OKLAB:
		distance_function = RGBOklabScaledDistance;
		break;
	case E_DISTANCE_RASTA:
		distance_function = RGBRastaDistance;
		break;
	default:
		distance_function = RGByuvDistance;
	}

}

unsigned char FindAtariColorIndex(const rgb &col)
{
	unsigned char i;
	// Find the most similar color in the Atari Palette
	unsigned char most_similar=0;
	double distance;
	double min_distance=DBL_MAX;
	for(i=0;i<128;++i)
	{
		distance = distance_function(col,atari_palette[i]);
		if (distance < min_distance)
		{
			min_distance=distance;
			most_similar=i;
		}
	}
	return most_similar;
}
