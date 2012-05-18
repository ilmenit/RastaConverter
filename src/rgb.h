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

struct rgb_error {
	int b;
	int g;
	int r;
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
