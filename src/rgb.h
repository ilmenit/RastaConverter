#ifndef RGB_H
#define RGB_H

struct rgb {
	unsigned char b;
	unsigned char g;
	unsigned char r;
	unsigned char a;
	bool operator==(const rgb &ar)
	{
		if (r==ar.r && g==ar.g && b==ar.b)
			return true;
		else
			return false;
	}
};

struct LabLch
{
                struct{
                        double L;
                        double a;
                        double b;
                }lab;
                struct{
                        double L;
                        double c;
                        double h;
                }lch;
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

#endif
