#ifndef RGB_H
#define RGB_H

struct rgb {
	unsigned char b;
	unsigned char g;
	unsigned char r;
	unsigned char a;
	bool operator==(const rgb &ar) const
	{
		if (r==ar.r && g==ar.g && b==ar.b)
			return true;
		else
			return false;
	}
};

struct rgb_hash
{
	size_t operator()(const rgb& c) const
	{
		return c.b + ((size_t)c.g << 8) + ((size_t)c.r << 16) + ((size_t)c.a << 24);
	}
};

struct Lab
{
    double L;
    double a;
    double b;
};


struct rgb_error {
	double b;
	double g;
	double r;
	bool operator==(const rgb &ar)
	{
		if (r==ar.r && g==ar.g && b==ar.b)
			return true;
		else
			return false;
	}
	void zero()
	{
		b=0;
		g=0;
		r=0;
	}
};

bool operator<(const rgb &l, const rgb &r);

#endif
