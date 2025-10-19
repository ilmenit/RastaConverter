#include <math.h>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <algorithm>
#include <cmath>
#include "Distance.h"
#include "rgb.h"

#undef M_PI
const double M_PI = 3.14159265358979323846;

using namespace std;

inline void RGBtoYUV(double r, double g, double b, double &y, double &u, double &v)
{
    y = 0.299*r + 0.587*g + 0.114*b;
    u= (b-y)*0.565;
    v= (r-y)*0.713;
}

distance_t RGByuvDistance(const rgb &col1, const rgb &col2)
{
	int dr = col2.r - col1.r;
	int dg = col2.g - col1.g;
	int db = col2.b - col1.b;

	float dy = 0.299f*dr + 0.587f*dg + 0.114f*db;
	float du = (db-dy)*0.565f;
	float dv = (dr-dy)*0.713f;

	float d = dy*dy + du*du + dv*dv;

	if (d > (float)DISTANCE_MAX)
		d = (float)DISTANCE_MAX;

	return (distance_t)d;
}

/* already defined
double cbrt(double d) {
	if (d < 0.0) {
		return -cbrt(-d);
	}
	else {
		return pow(d, 1.0 / 3.0);
	}
}
*/

typedef std::unordered_map < rgb, Lab, rgb_hash > rgb_lab_map_t;
rgb_lab_map_t rgb_lab_map;

typedef std::unordered_map < rgb, Lab, rgb_hash > rgb_oklab_map_t;
rgb_oklab_map_t rgb_oklab_map;

static const double OKLAB_DISTANCE_SCALE = 200000.0; // maps perceptual energy (~0-1) to integer range
// Rasta distance: Fast YUV-based metric optimized for Atari palette conversion
// Simpler than CIEDE2000, better at avoiding unwanted gray mapping
static const float RASTA_GRAY_PENALTY = 28.0f;        // penalty for mapping chromatic source to near-gray palette
static const float RASTA_GRAY_THRESHOLD = 30.0f;      // squared chroma below this = "near gray"

static inline double srgb_comp_to_linear(double c)
{
	if (c <= 0.04045)
		return c / 12.92;
	return pow((c + 0.055) / 1.055, 2.4);
}

static inline void RGB2OKLABDirect(const rgb &c, Lab &result)
{
	const double fr = srgb_comp_to_linear(c.r / 255.0);
	const double fg = srgb_comp_to_linear(c.g / 255.0);
	const double fb = srgb_comp_to_linear(c.b / 255.0);

	const double l = 0.4122214708 * fr + 0.5363325363 * fg + 0.0514459929 * fb;
	const double m = 0.2119034982 * fr + 0.6806995451 * fg + 0.1073969566 * fb;
	const double s = 0.0883024619 * fr + 0.2817188376 * fg + 0.6299787005 * fb;

	const double l_ = cbrt(l);
	const double m_ = cbrt(m);
	const double s_ = cbrt(s);

	result.L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
	result.a = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
	result.b = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;
}

static void RGB2OKLAB(const rgb &c, Lab &result)
{
	static std::shared_mutex rwlock{};
	rwlock.lock_shared();
	rgb_oklab_map_t::iterator it = rgb_oklab_map.find(c);
	if (it != rgb_oklab_map.end())
	{
		result = it->second;
		rwlock.unlock_shared();
		return;
	}
	rwlock.unlock_shared();

	rwlock.lock();
	auto r = rgb_oklab_map.insert(rgb_oklab_map_t::value_type(c, Lab()));
	rwlock.unlock();

	if (!r.second)
	{
		result = r.first->second;
	}
	else
	{
		RGB2OKLABDirect(c, result);
		r.first->second = result;
	}
}

static inline distance_t OklabDistanceEnergy(const Lab &first, const Lab &second)
{
	const double dL = first.L - second.L;
	const double da = first.a - second.a;
	const double db = first.b - second.b;
	double dist = (dL * dL + da * da + db * db) * OKLAB_DISTANCE_SCALE;
	if (dist > (double)DISTANCE_MAX)
		dist = (double)DISTANCE_MAX;
	return (distance_t)(dist + 0.5);
}

static inline distance_t OklabScaledDistanceEnergy(const Lab &first, const Lab &second)
{
	double Lmean = 0.5 * (first.L + second.L);
	if (Lmean < 0.0)
		Lmean = 0.0;
	else if (Lmean > 1.0)
		Lmean = 1.0;

	const double beta = 3.0;
	const double scale = 1.0 + beta * (1.0 - Lmean);

	const double dL = first.L - second.L;
	const double da = (first.a - second.a) * scale;
	const double db = (first.b - second.b) * scale;

	double dist = (dL * dL + da * da + db * db) * OKLAB_DISTANCE_SCALE;
	if (dist > (double)DISTANCE_MAX)
		dist = (double)DISTANCE_MAX;
	return (distance_t)(dist + 0.5);
}

static void RGB2LAB(const rgb &c, Lab &result)
{
	static std::shared_mutex rwlock{};
	rwlock.lock_shared();
	rgb_lab_map_t::iterator it=rgb_lab_map.find(c);
	if (it!=rgb_lab_map.end())
	{
		result=it->second;
		rwlock.unlock_shared();
		return;
	}
	rwlock.unlock_shared();

	rwlock.lock();
	auto r = rgb_lab_map.insert(rgb_lab_map_t::value_type(c, Lab()));
	rwlock.unlock();

	if (!r.second)
	{
		result = r.first->second;
	}
	else
	{
				int ir = c.r;
				int ig = c.g;
				int ib = c.b;

				float fr = ((float) ir) / 255.0f;
				float fg = ((float) ig) / 255.0f;
				float fb = ((float) ib) / 255.0f;

				if (fr > 0.04045f)
						fr = powf((fr + 0.055f) / 1.055f, 2.4f);
				else
						fr = fr / 12.92f;

				if (fg > 0.04045f)
						fg = powf((fg + 0.055f) / 1.055f, 2.4f);
				else
						fg = fg / 12.92f;

				if (fb > 0.04045f)
						fb = powf((fb + 0.055f) / 1.055f, 2.4f);
				else
						fb = fb / 12.92f;

				// Use white = D65
				const float x = fr * 0.4124f + fg * 0.3576f + fb * 0.1805f;
				const float y = fr * 0.2126f + fg * 0.7152f + fb * 0.0722f;
				const float z = fr * 0.0193f + fg * 0.1192f + fb * 0.9505f;

				float vx = x / 0.95047f;
				float vy = y;
				float vz = z / 1.08883f;

				if (vx > 0.008856f)
						vx = (float) cbrt(vx);
				else
						vx = (7.787f * vx) + (16.0f / 116.0f);

				if (vy > 0.008856f)
						vy = (float) cbrt(vy);
				else
						vy = (7.787f * vy) + (16.0f / 116.0f);

				if (vz > 0.008856f)
						vz = (float) cbrt(vz);
				else
						vz = (7.787f * vz) + (16.0f / 116.0f);

				result.L = 116.0f * vy - 16.0f;
				result.a = 500.0f * (vx - vy);
				result.b = 200.0f * (vy - vz);
				r.first->second = result;
	}
}

double CIE94(const double L1,const double a1,const double b1, 
	const double L2,const double a2,const double b2) 
{
		const double WHTL=1; //Weighting factors depending on the application (1 = default)
		const double WHTC=1;
		const double WHTH=1;		

		double xC1 = sqrt( ( a1*a1 ) + ( b1*b1 ) );
		double xC2 = sqrt( ( a2*a2 ) + ( b2*b2 ) );
		double xDL = L2 - L1;
		double xDC = xC2 - xC1;
		double xDE = sqrt( ( ( L1 - L2 ) * ( L1 - L2 ) ) + ( ( a1 - a2 ) * ( a1 - a2 ) )+ ( ( b1 - b2 ) * ( b1 - b2 ) ) );
		double xDH;
		if ( sqrt( xDE ) > ( sqrt( abs( xDL ) ) + sqrt( abs( xDC ) ) ) ) 
		{
			xDH = sqrt( ( xDE * xDE ) - ( xDL * xDL ) - ( xDC * xDC ) );
		}
		else
		{
			xDH = 0;
		}
		double xSC = 1 + ( 0.045 * xC1 );
		double xSH = 1 + ( 0.015 * xC1 );
		xDL /= WHTL;
		xDC /= WHTC * xSC;
		xDH /= WHTH * xSH;
		return sqrt( xDL*xDL + xDC*xDC + xDH*xDH );
}


double CIEDE2000(const double L1,const double a1,const double b1, 
				const double L2,const double a2,const double b2) 
{ 
	double Lmean = (L1 + L2) / 2.0; 
	double C1 =  sqrt(a1*a1 + b1*b1); 
	double C2 =  sqrt(a2*a2 + b2*b2); 
	double Cmean = (C1 + C2) / 2.0; 

	double G =  ( 1 - sqrt( pow(Cmean, 7.0) / (pow(Cmean, 7.0) + pow(25, 7.0)) ) ) / 2; 
	double a1prime = a1 * (1 + G); 
	double a2prime = a2 * (1 + G); 

	double C1prime =  sqrt(a1prime*a1prime + b1*b1); 
	double C2prime =  sqrt(a2prime*a2prime + b2*b2); 
	double Cmeanprime = (C1prime + C2prime) / 2;  
			
	double h1prime =  atan2(b1, a1prime) + 2*M_PI * (atan2(b1, a1prime)<0 ? 1 : 0);
	double h2prime =  atan2(b2, a2prime) + 2*M_PI * (atan2(b2, a2prime)<0 ? 1 : 0);
	double Hmeanprime =  ((abs(h1prime - h2prime) > M_PI) ? (h1prime + h2prime + 2*M_PI) / 2 : (h1prime + h2prime) / 2); 
			
	double T =  1.0 - 0.17 * cos(Hmeanprime - M_PI/6.0) + 0.24 * cos(2*Hmeanprime) + 0.32 * cos(3*Hmeanprime + M_PI/30) - 0.2 * cos(4*Hmeanprime - 21*M_PI/60); 

	double deltahprime =  ((abs(h1prime - h2prime) <= M_PI) ? h2prime - h1prime : (h2prime <= h1prime) ? h2prime - h1prime + 2*M_PI : h2prime - h1prime - 2*M_PI); 

	double deltaLprime = L2 - L1; 
	double deltaCprime = C2prime - C1prime; 
	double deltaHprime =  2.0 * sqrt(C1prime*C2prime) * sin(deltahprime / 2.0); 
	double SL =  1.0 + ( (0.015*(Lmean - 50)*(Lmean - 50)) / (sqrt( 20 + (Lmean - 50)*(Lmean - 50) )) ); 
	double SC =  1.0 + 0.045 * Cmeanprime; 
	double SH =  1.0 + 0.015 * Cmeanprime * T; 

	double deltaTheta =  (30 * M_PI / 180) * exp(-((180/M_PI*Hmeanprime-275)/25)*((180/M_PI*Hmeanprime-275)/25));
	double RC =  (2 * sqrt(pow(Cmeanprime, 7.0) / (pow(Cmeanprime, 7.0) + pow(25.0, 7.0))));
	double RT =  (-RC * sin(2 * deltaTheta));

	double KL = 1;
	double KC = 1;
	double KH = 1;

	double deltaE = /*sqrt */(
	((deltaLprime/(KL*SL)) * (deltaLprime/(KL*SL))) +
	((deltaCprime/(KC*SC)) * (deltaCprime/(KC*SC))) +
	((deltaHprime/(KH*SH)) * (deltaHprime/(KH*SH))) +
	(RT * (deltaCprime/(KC*SC)) * (deltaHprime/(KH*SH)))
	);

	return deltaE;
} 

distance_t RGBCIE94Distance(const rgb &col1, const rgb &col2)
{
	Lab first,second;
	RGB2LAB(col1,first);
	RGB2LAB(col2,second);

	double dist= CIE94(first.L,first.a,first.b, 
			       second.L,second.a,second.b);
	return (distance_t)dist;
}


distance_t RGBCIEDE2000Distance(const rgb &col1, const rgb &col2)
{
	Lab first,second;
	RGB2LAB(col1,first);
	RGB2LAB(col2,second);

	double dist= CIEDE2000(first.L,first.a,first.b, 
				second.L,second.a,second.b);
	return (distance_t)dist;
}

distance_t RGBEuclidianDistance(const rgb &col1, const rgb &col2)
{
	int distance=0;

	// euclidian distance
	int dr = col1.r - col2.r;
	int dg = col1.g - col2.g;
	int db = col1.b - col2.b;

	int d = dr*dr + dg*dg + db*db;

	if (d > DISTANCE_MAX)
		d = DISTANCE_MAX;

	return (distance_t)d;
}

distance_t RGBRastaDistance(const rgb &col1, const rgb &col2)
{
	// YUV deltas
	int dr = col2.r - col1.r, dg = col2.g - col1.g, db = col2.b - col1.b;
	float dy = 0.299f*dr + 0.587f*dg + 0.114f*db;
	float du = (db - dy) * 0.565f, dv = (dr - dy) * 0.713f;

	// Source and palette luminance
	float y1 = 0.299f*col1.r + 0.587f*col1.g + 0.114f*col1.b;
	float y2 = 0.299f*col2.r + 0.587f*col2.g + 0.114f*col2.b;
	float ymean = (y1 + y2) * 0.5f;
	
	// Base YUV distance with chroma boost
	float dist = dy*dy + (du*du + dv*dv);

	// Gray penalty: if source has color but palette is near-gray, add penalty
	float u1 = (col1.b - y1) * 0.565f, v1 = (col1.r - y1) * 0.713f;
	float u2 = (col2.b - y2) * 0.565f, v2 = (col2.r - y2) * 0.713f;
	float chroma_src = u1*u1 + v1*v1;
	float chroma_pal = u2*u2 + v2*v2;
	
	if (chroma_src > 4.0f && chroma_pal < RASTA_GRAY_THRESHOLD) {
		dist += RASTA_GRAY_PENALTY * (RASTA_GRAY_THRESHOLD - chroma_pal);
	}
	
	return (dist > (float)DISTANCE_MAX) ? DISTANCE_MAX : (distance_t)dist;
}


distance_t RGBOklabDistance(const rgb &col1, const rgb &col2)
{
	Lab first, second;
	RGB2OKLAB(col1, first);
	RGB2OKLAB(col2, second);
	return OklabDistanceEnergy(first, second);
}

distance_t RGBOklabScaledDistance(const rgb &col1, const rgb &col2)
{
	Lab first, second;
	RGB2OKLAB(col1, first);
	RGB2OKLAB(col2, second);
	return OklabScaledDistanceEnergy(first, second);
}


