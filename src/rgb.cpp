#include "rgb.h"

// simple sort to distinguish different colors for maps, sets etc
bool operator<(const rgb &l, const rgb &r) 
{
	if( l.r < r.r ) return true;
	if( l.r == r.r && l.g < r.g) return true;
	if( l.r == r.r && l.g == r.g && l.b < r.b) return true;
	return false;
}